#include "le_slang_shader_compiler.h"
#include "le_shader_compiler_interface.h"

#include "le_log.h"
#include "private/le_renderer/le_renderer_types.h" // for shader type

#include <iomanip>
#include <iostream>
#include <assert.h>

#include <filesystem> // for parsing shader source file paths
#include <fstream>    // for reading shader source files
#include <sstream>
#include <cstring> // for memcpy
#include <vector>
#include <regex>
#include <unordered_map>
#include <memory> // for unique_ptr

#include "slang/slang-com-ptr.h"
#include "slang/slang.h"

static constexpr auto LOGGER_LABEL = "le_slang_shader_compiler";

static auto logger() {
	static auto logger = LeLog( LOGGER_LABEL );
	return logger;
}

struct le_shader_compiler_o {
	std::vector<std::filesystem::path const*> include_search_directories; // shader include search directories (walked in-order)
	Slang::ComPtr<slang::IGlobalSession>      globalSession;
	Slang::ComPtr<slang::ISession>            session;
};

struct le_shader_compiler_session_o {
	// placeholder
};

// ---------------------------------------------------------------

struct FileData {
	std::string       path_str; /// path to file, as std::string
	std::vector<char> contents; /// contents of file
};

// We keep IncludesList as a struct, using it as an abstract handle
// simplifies passing it to the includer callback
//
struct included_files_container_t {
	std::vector<std::string> paths; // paths to files that this translation unit depends on
};

// ---------------------------------------------------------------

struct le_shader_compilation_result_o {
	Slang::ComPtr<slang::IModule>        module          = nullptr;
	Slang::ComPtr<slang::IComponentType> component       = nullptr;
	Slang::ComPtr<slang::IBlob>          target_code     = nullptr;
	Slang::ComPtr<slang::IBlob>          out_diagnostics = nullptr;
	included_files_container_t           includes;
};

// ---------------------------------------------------------------
// Caller of this function must provide a pointer-to-iterator telling us from where
// to read - if iterator is initially set at nullptr, then this means
// to read from the beginning.
// this method will increment the iterator with each call and will write
// the incremented iterator back into the parameter.
static bool le_shader_compilation_result_get_next_included_file_path( le_shader_compilation_result_o* self, const char** str, void** it_ ) {

	auto local_it = static_cast<std::string*>( *it_ );

	if ( local_it == nullptr ) {
		local_it = self->includes.paths.data();
	}

	auto paths_end = self->includes.paths.data() + self->includes.paths.size();

	if ( local_it != paths_end ) {

		*str = local_it->c_str();

		// Store successor iterator value back into value given by parameter.
		*it_ = local_it + 1;
		return true;
	}

	// ---------- invariant: we are one past the last element

	return false;
}

// ---------------------------------------------------------------

static le_shader_compilation_result_o* le_shader_compilation_result_create() {
	auto obj = new le_shader_compilation_result_o{};
	return obj;
}

// ---------------------------------------------------------------

static void le_shader_compilation_result_destroy( le_shader_compilation_result_o* self ) {
	delete ( self );
}

// ---------------------------------------------------------------

static void le_shader_compilation_result_get_result_bytes( le_shader_compilation_result_o* res, const char** p_spir_v_bytes, size_t* pNumBytes ) {
	assert( res->component );

	res->component->getTargetCode( 0, res->target_code.writeRef(), res->out_diagnostics.writeRef() );

	if ( res->out_diagnostics ) {
		logger().warn( "%s\n", ( const char* )res->out_diagnostics->getBufferPointer() );
	}

	*p_spir_v_bytes = ( char* )res->target_code->getBufferPointer();
	*pNumBytes      = res->target_code->getBufferSize();
}

// ---------------------------------------------------------------
/// \brief returns true if compilation was a success, false otherwise
static bool le_shader_compilation_result_get_result_success( le_shader_compilation_result_o* res ) {
	// return shaderc_result_get_compilation_status( res->result ) == shaderc_compilation_status_success;
	return ( nullptr != res->component );
}

// ---------------------------------------------------------------

static le_shader_compiler_o* le_shader_compiler_create() {
	auto        obj    = new le_shader_compiler_o();
	auto        result = createGlobalSession( obj->globalSession.writeRef() );
	// assert( SLANG_SUCCEEDED( result ) );
	logger().info( "Created slang core shader compiler" );
	return obj;
}

// ---------------------------------------------------------------

static void le_shader_compiler_add_shader_include_directory( le_shader_compiler_o* self, char const* path ) {
	// Todo: should we test whether a directory is already
	// part of include directories?
	self->include_search_directories.emplace_back( new std::filesystem::path( path ) );
}

// ---------------------------------------------------------------

static void le_shader_compiler_destroy( le_shader_compiler_o* self ) {

	for ( auto& p : self->include_search_directories ) {
		delete p;
	}
	self->include_search_directories.clear();

	logger().info( "Destroyed slang core shader compiler" );
	delete self;
}

// ---------------------------------------------------------------

static void le_shader_compiler_maintain_cache( le_shader_compiler_o* self ) {
	//
	// We must reset the session here, so that only shaders that are compiled
	// in the same batch share the same session cache.
	//
	// We do this so that hot-reloading may work, otherwise there will be
	// artifacts kept around from a pervious version of a shader module
	// that needs to be recompiled, and this will not hot-reload, but use the
	// cached version. this is why we clear cache once we have finished compiling
	// a current batch of shaders.
	//
	self->session.setNull();
}

// ---------------------------------------------------------------
static void print_error_context( const char* errMsg, const std::string& shaderSource, const std::string& sourceFileName ) {
	static auto logger = LeLog( LOGGER_LABEL );

	struct error_entry_t {

		std::string errorFileName;  // Will contain the name of the file which contains the error
		std::string errorMessage;   // Will contain error message
		std::string errorSquiggle;  // squiggle indicating where the error happened
		uint32_t    lineNumber = 0; // Will contain error line number after successful parse
	};

	/*

	errMsg has the form:  "/home/tim/Documents/dev/island_home/island/apps/dev/test_slang/resources/shaders/slang/test_0.slang(9): error 20001: unexpected identifier, expected ','"

	Or, on Windows:

	C:\Users\tim\Documents\dev\island\apps\examples\hello_triangle\resources\shaders\default.frag(24): error 20001: 'vertexColor2' : no such field in structure
	C:\Users\tim\Documents\dev\island\apps\examples\hello_triangle\resources\shaders\default.frag(24): error 20001: 'assign' :  cannot convert from 'layout( location=0) in block{ in highp 2-component vector of float texCoord,  in highp 4-component vector of float vertexColor}' to 'layout( location=0) out highp 4-component vector of float'

	Note that on Windows, the colon ':' character may be part of the file path, as in "c:\", we therefore
	use a slightly more involved regular expression instead of sscanf.

	*/
	std::unordered_map<std::string, std::vector<error_entry_t>> errors_per_file;

	{
		std::vector<error_entry_t> errors;

		std::istringstream errMsgStream( errMsg );
		std::string        error_line;

		std::getline( errMsgStream, error_line );

		uint32_t currentLineNumber = 1; /* Line numbers start counting at 1 */

		// get all errror messages by going over all entries

		while ( errMsgStream.good() ) {

			bool        scanResult = false;
			std::cmatch cm;
			scanResult = std::regex_search( error_line.c_str(), cm, std::regex( R"regex((.*?)\((\d+)\):\s*error \d+:(.*))regex" ) );

			if ( scanResult ) {
				error_entry_t error_entry{};
				error_entry.errorFileName = cm[ 1 ].str() != "0" ? cm[ 1 ].str() : "";
				error_entry.lineNumber    = std::stoul( cm[ 2 ].str() );
				error_entry.errorMessage  = cm[ 3 ].str();

				uint32_t parsed_line_number = currentLineNumber;

				while ( errMsgStream.good() && currentLineNumber < parsed_line_number + 1 ) {
					std::getline( errMsgStream, error_line );
					currentLineNumber++;
				}
				while ( errMsgStream.good() && currentLineNumber < parsed_line_number + 2 ) {
					std::getline( errMsgStream, error_line );
					error_entry.errorSquiggle = error_line;
					currentLineNumber++;
				}
				errors.emplace_back( std::move( error_entry ) );
			}

			std::getline( errMsgStream, error_line );
			currentLineNumber++;
		}

		// Group errors per-filename, so that we can show each offending incident
		// per file.

		for ( auto& e : errors ) {
			errors_per_file[ e.errorFileName ].push_back( e );
		}
		errors.clear();
	}

	for ( auto const& [ source_file_name, errors ] : errors_per_file ) {

		std::unique_ptr<std::istream> sourceCode;

		if ( source_file_name == sourceFileName ) {
			sourceCode = std::make_unique<std::istringstream>( shaderSource );
		} else {
			// we must load the given source file into memory
			if ( source_file_name.empty() ) {
				continue;
			}
			sourceCode = std::make_unique<std::ifstream>( source_file_name );
		}

		std::string currentLine;

		if ( !errors.empty() ) {

			logger.warn( "In file: '%s'", source_file_name.c_str() );

			std::getline( *sourceCode, currentLine );

			uint32_t currentLineNumber = 1; /* Line numbers start counting at 1 */

			// Note: it's possible that there are more than one errors for a given line number.
			// which is why we look forward whenever we print an error message to see if there is more to come.
			auto current_error = errors.begin();

			while ( sourceCode->good() ) {

				if ( ( currentLineNumber > 0 ) && ( currentLineNumber + 3 > current_error->lineNumber ) ) {
					std::ostringstream sourceContext;

					const auto shaderSourceCodeLine = currentLine;

					if ( currentLineNumber == current_error->lineNumber ) {
						// set console color
						sourceContext << char( 0x1B ) << "[38;5;209m";
					}

					sourceContext << std::right << std::setw( 4 ) << currentLineNumber << " | " << shaderSourceCodeLine;

					if ( currentLineNumber == current_error->lineNumber ) {
						// reset console color to defaults
						sourceContext << char( 0x1B ) << "[0m";
					}

					logger.warn( "%s", sourceContext.str().c_str() );

					if ( currentLineNumber == current_error->lineNumber ) {
						auto next_error = current_error;
						do {
							std::ostringstream squiggle;
							current_error = next_error;
							squiggle << std::right << std::setw( 4 ) << " " << " | " << current_error->errorSquiggle << current_error->errorMessage;
							logger.warn( "%s", squiggle.str().c_str() );
							next_error = current_error + 1;
						} while ( next_error != errors.end() && next_error->lineNumber == current_error->lineNumber );
					}
				}

				if ( currentLineNumber >= current_error->lineNumber + 2 ) {
					logger.warn( "" ); // add line break for better readability
					break;
				}
				std::getline( *sourceCode, currentLine );
				++currentLineNumber;
			}
		}
	}
}

// ---------------------------------------------------------------

static bool le_shader_compiler_compile_source(
    le_shader_compiler_o*           self,
    const char*                     sourceFileText,
    size_t                          sourceFileNumBytes,
    const uint32_t&                 shader_source_language,
    const le::ShaderStage&          shaderType,
    const char*                     original_file_path,
    char const*                     macroDefinitionsStr,
    size_t                          macroDefinitionsStrSz,
    le_shader_compilation_result_o* result ) {

	if ( nullptr == self->session ) {
		slang::SessionDesc session_desc{};

		// std::vector<slang::CompilerOptionEntry> compiler_options;
		// slang::CompilerOptionEntry              e{};
		// e.name            = slang::CompilerOptionName::NoMangle;
		// e.value.intValue0 = 0;
		// e.value.kind      = slang::CompilerOptionValueKind::Int;

		slang::TargetDesc targetDesc = {};
		targetDesc.format            = SLANG_SPIRV;
		targetDesc.profile           = self->globalSession->findProfile( "spirv_1_5" );
		// targetDesc.compilerOptionEntries    = compiler_options.data();
		// targetDesc.compilerOptionEntryCount = compiler_options.size();

		// TODO: let's check if we need to set some other
		// states for compiling -- perhaps we might even
		// make the settings something that can be set from
		// the client-side.

		session_desc.targets     = &targetDesc;
		session_desc.targetCount = 1;

		session_desc.allowGLSLSyntax = false;

		// we need to translate the file paths into an array of const char*
		// this is super annoying; and because of window's implementation of
		// std::filepath (which stores filepaths internally as (wchar const *),
		// we can't directly point at `c_str()` of the file-path, but must
		// allocate some temporary strings, and store some
		// temporary pointers to these strings ... gnarly.

		std::vector<std::string> include_dir_paths;
		std::vector<char const*> p_include_dir_paths;
		include_dir_paths.reserve( self->include_search_directories.size() );
		p_include_dir_paths.reserve( self->include_search_directories.size() );

		for ( auto& p : self->include_search_directories ) {
			include_dir_paths.push_back( p->string() );
			p_include_dir_paths.push_back( include_dir_paths.back().c_str() );
		}

		session_desc.searchPaths     = p_include_dir_paths.data();
		session_desc.searchPathCount = p_include_dir_paths.size();

		self->globalSession->createSession( session_desc, self->session.writeRef() );
	}

	// Fetch the module name from the given filename -- if compiling from source
	// you should still pass in a module name so that the compiler can internally
	// create a cache key for this module based on the "file name"
	std::string module_name = std::filesystem::path( original_file_path ).filename().string();

	// This is super annoying, we must do this because our sourceFileText is not zero-terminated,
	// but the slang api requires a zero-terminated string.
	std::string source_file_contents_as_string = std::string( sourceFileText, sourceFileText + sourceFileNumBytes );

	logger().info( "Compiling shader file: '%s'", original_file_path );

	result->module = self->session->loadModuleFromSourceString( module_name.c_str(), original_file_path, source_file_contents_as_string.c_str(), result->out_diagnostics.writeRef() );

	if ( result->module ) {

		uint32_t num_dependency_files = result->module->getDependencyFileCount();
		for ( uint32_t i = 0; i != num_dependency_files; i++ ) {
			result->includes.paths.push_back( result->module->getDependencyFilePath( i ) );
		}
	}

	if ( result->out_diagnostics ) {
		print_error_context( ( char const* )result->out_diagnostics->getBufferPointer(), source_file_contents_as_string, original_file_path );
	}

	if ( result->module ) {
		result->module->link( result->component.writeRef(), result->out_diagnostics.writeRef() );

		// check whether linking was successful
		if ( result->out_diagnostics ) {
			logger().warn( "%s\n", ( const char* )result->out_diagnostics->getBufferPointer() );
		}
	} else {
		logger().warn( "module '%s' could not be compiled", module_name.c_str() );
	}

	return ( result->module != nullptr );
}

// ---------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_slang_shader_compiler, api_ ) {

	auto& compiler_i = static_cast<le_slang_shader_compiler_api*>( api_ )->compiler_i;

	if ( compiler_i == nullptr ) {
		compiler_i = new le_shader_compiler_interface_t{};
	} else {
		// The interface already existed - we have been reloaded and only just need to update
		// function pointer addresses.
		//
		// This is important as by not re-allocating a new interface object
		// but by updating the existing interface object by-value, we keep the *public
		// address for the interface*, while updating its function pointers.
		*compiler_i = le_shader_compiler_interface_t();
	}

	compiler_i->create                       = le_shader_compiler_create;
	compiler_i->destroy                      = le_shader_compiler_destroy;
	compiler_i->add_shader_include_directory = le_shader_compiler_add_shader_include_directory;
	compiler_i->compile_source               = le_shader_compiler_compile_source;
	compiler_i->maintain_cache               = le_shader_compiler_maintain_cache;

	compiler_i->result_create             = le_shader_compilation_result_create;
	compiler_i->result_get_bytes          = le_shader_compilation_result_get_result_bytes;
	compiler_i->result_get_success        = le_shader_compilation_result_get_result_success;
	compiler_i->result_get_included_files = le_shader_compilation_result_get_next_included_file_path;
	compiler_i->result_destroy            = le_shader_compilation_result_destroy;

#ifdef PLUGINS_DYNAMIC
	le_core_load_library_persistently( "libslang.so" );
#endif
}
