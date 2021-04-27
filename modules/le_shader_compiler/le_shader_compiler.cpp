#include "le_core/le_core.h"
#include "le_shader_compiler/le_shader_compiler.h"

#include "shaderc/shaderc.hpp"
#include "le_log/le_log.h"
#include "le_renderer/le_renderer.h" // for shader type

#include <iomanip>
#include <iostream>
#include <assert.h>

#include <filesystem> // for parsing shader source file paths
#include <fstream>    // for reading shader source files
#include <sstream>
#include <cstring> // for memcpy
#include <vector>
#include <set>
#include <regex>

static constexpr auto LOGGER_LABEL = "le_shader_compiler";

struct le_shader_compiler_o {
	shaderc_compiler_t        compiler;
	shaderc_compile_options_t options;
};

// ---------------------------------------------------------------

struct FileData {
	std::string       path_str; /// path to file, as std::string
	std::vector<char> contents; /// contents of file
};

// We keep IncludesList as a struct, using it as an abstract handle
// simplifies passing it to the includer callback
//
struct IncludesList {
	std::set<std::string>           paths;                    // paths to files that this translation unit depends on
	std::set<std::string>::iterator paths_it = paths.begin(); // iterator to current element
};

// ---------------------------------------------------------------

struct le_shader_compilation_result_o {
	shaderc_compilation_result *result = nullptr;
	IncludesList                includes;
};

// ---------------------------------------------------------------

static shaderc_shader_kind convert_to_shaderc_shader_kind( const le::ShaderStage &type ) {
	static auto logger = LeLog( LOGGER_LABEL );

	shaderc_shader_kind result{};

	switch ( type ) {
	case ( le::ShaderStage::eVertex ):
		result = shaderc_glsl_vertex_shader;
		break;
	case ( le::ShaderStage::eTessellationControl ):
		result = shaderc_tess_control_shader;
		break;
	case ( le::ShaderStage::eTessellationEvaluation ):
		result = shaderc_tess_evaluation_shader;
		break;
	case ( le::ShaderStage::eGeometry ):
		result = shaderc_geometry_shader;
		break;
	case ( le::ShaderStage::eFragment ):
		result = shaderc_glsl_fragment_shader;
		break;
	case ( le::ShaderStage::eCompute ):
		result = shaderc_glsl_compute_shader;
		break;
	case ( le::ShaderStage::eRaygenBitNv ):
		result = shaderc_raygen_shader;
		break;
	case ( le::ShaderStage::eAnyHitBitNv ):
		result = shaderc_anyhit_shader;
		break;
	case ( le::ShaderStage::eClosestHitBitNv ):
		result = shaderc_closesthit_shader;
		break;
	case ( le::ShaderStage::eMissBitNv ):
		result = shaderc_miss_shader;
		break;
	case ( le::ShaderStage::eIntersectionBitNv ):
		result = shaderc_intersection_shader;
		break;
	case ( le::ShaderStage::eCallableBitNv ):
		result = shaderc_callable_shader;
		break;
	case ( le::ShaderStage::eTaskBitNv ):
		result = shaderc_task_shader;
		break;
	case ( le::ShaderStage::eMeshBitNv ):
		result = shaderc_mesh_shader;
		break;

	default: {
		logger.warn( "Unknown shader type %d", uint32_t( ( type ) ) );
		assert( false && "unknown shader type" );
	} break;
	}
	return result;
}

// ---------------------------------------------------------------

static bool le_shader_compilation_result_get_next_includes_path( le_shader_compilation_result_o *self, const char **str, size_t *strSz ) {

	if ( self->includes.paths_it != self->includes.paths.end() ) {

		*str   = self->includes.paths_it->c_str();
		*strSz = self->includes.paths_it->size();

		self->includes.paths_it++;
		return true;
	}

	// ---------- invariant: we are one past the last element

	return false;
}

// ---------------------------------------------------------------

static le_shader_compilation_result_o *le_shader_compilation_result_create() {
	auto obj = new le_shader_compilation_result_o{};
	return obj;
}

// ---------------------------------------------------------------

static void le_shader_compilation_result_detroy( le_shader_compilation_result_o *self ) {
	if ( self->result != nullptr ) {
		shaderc_result_release( self->result );
	}
	delete ( self );
}

// ---------------------------------------------------------------

static void le_shader_compilation_result_get_result_bytes( le_shader_compilation_result_o *res, const char **p_spir_v_bytes, size_t *pNumBytes ) {
	assert( res->result );

	*p_spir_v_bytes = shaderc_result_get_bytes( res->result );
	*pNumBytes      = shaderc_result_get_length( res->result );
}

// ---------------------------------------------------------------
/// \brief returns true if compilation was a success, false otherwise
static bool le_shader_compilation_result_get_result_success( le_shader_compilation_result_o *res ) {
	assert( res->result );
	return shaderc_result_get_compilation_status( res->result ) == shaderc_compilation_status_success;
}

// ---------------------------------------------------------------

static le_shader_compiler_o *le_shader_compiler_create() {
	auto obj      = new le_shader_compiler_o();
	obj->compiler = shaderc_compiler_initialize();

	{
		obj->options = shaderc_compile_options_initialize();
		shaderc_compile_options_set_generate_debug_info( obj->options );
		shaderc_compile_options_set_source_language( obj->options, shaderc_source_language::shaderc_source_language_glsl );
		shaderc_compile_options_set_optimization_level( obj->options, shaderc_optimization_level_performance );
	}

	return obj;
}

// ---------------------------------------------------------------

static void le_shader_compiler_destroy( le_shader_compiler_o *self ) {
	static auto logger = LeLog( LOGGER_LABEL );
	shaderc_compile_options_release( self->options );
	shaderc_compiler_release( self->compiler );
	logger.info( "Destroyed shader compiler" );
	delete self;
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path &file_path, bool *success ) {
	static auto logger = LeLog( LOGGER_LABEL );

	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		logger.error( "Unable to open file: '%s'", std::filesystem::canonical( file_path ).c_str() );
		*success = false;
		return contents;
	}

	// ----------| invariant: file is open, file seeker is std::ios::ate (at end)

	auto endOfFilePos = file.tellg();

	if ( endOfFilePos >= 0 ) {
		fileSize = size_t( endOfFilePos );
	} else {
		*success = false;
		file.close();
		return contents;
	}

	// ----------| invariant: file has some bytes to read
	contents.resize( fileSize );

	file.seekg( 0, std::ios::beg );
	file.read( contents.data(), endOfFilePos );
	file.close();

	*success = true;
	return contents;
}

// ---------------------------------------------------------------

static shaderc_include_result *le_shaderc_include_result_create( void *      user_data,
                                                                 const char *requested_source,
                                                                 int         type,
                                                                 const char *requesting_source,
                                                                 size_t      include_depth ) {

	auto self         = new shaderc_include_result();
	auto includesList = reinterpret_cast<IncludesList *>( user_data );

	std::filesystem::path requested_source_path;

	if ( shaderc_include_type_relative == type ) {
		auto basename         = std::filesystem::path( requesting_source ).remove_filename();
		requested_source_path = basename.append( requested_source );
	} else {
		requested_source_path = std::filesystem::path( requested_source );
	}

	bool requestedFileExists = std::filesystem::exists( requested_source_path );
	bool loadSuccess         = false;

	auto fileData = new FileData();

	if ( requestedFileExists ) {
		requested_source_path = std::filesystem::canonical( requested_source_path );

		includesList->paths.insert( requested_source_path.string() );
		fileData->path_str = requested_source_path.string();

		// -- load file contents into fileData
		fileData->contents = load_file( requested_source_path, &loadSuccess );

	} else {
		// Empty path is understood as a signal in shaderc: failed inclusion
		fileData->path_str = "";
	}

	if ( false == loadSuccess ) {

		// Store error message instead of file contents.
		const std::string error_message{ "Could not load file specified: '" + fileData->path_str + "'" };

		fileData->contents.assign( error_message.begin(), error_message.end() );
	}

	self->user_data          = fileData;
	self->content            = fileData->contents.data();
	self->content_length     = fileData->contents.size();
	self->source_name        = fileData->path_str.c_str();
	self->source_name_length = fileData->path_str.size();

	return self;
}

// ---------------------------------------------------------------

static void le_shaderc_include_result_destroy( void *user_data, shaderc_include_result *self ) {

	// --cleanup include result
	auto fileData = reinterpret_cast<FileData *>( self->user_data );
	delete fileData;

	delete self;
}

// ---------------------------------------------------------------

static inline bool checkForLineNumberModifier( const std::string &line, uint32_t &lineNumber, std::string &currentFilename, std::string &lastFilename ) {

	if ( line.find( "#line", 0 ) != 0 )
		return false;

	// --------| invariant: current line is a line number marker

	std::istringstream is( line );

	// ignore until first whitespace, then parse linenumber, then parse filename
	std::string quotedFileName;
	is.ignore( std::numeric_limits<std::streamsize>::max(), ' ' ) >> lineNumber >> quotedFileName;
	// decrease line number by one, as marker line is not counted
	--lineNumber;
	// store last filename when change occurs
	std::swap( lastFilename, currentFilename );
	// remove double quotes around filename, if any
	currentFilename.assign( quotedFileName.begin() + quotedFileName.find_first_not_of( '"' ), quotedFileName.begin() + quotedFileName.find_last_not_of( '"' ) + 1 );
	return true;
};

// ---------------------------------------------------------------

static void le_shader_compiler_print_error_context( const char *errMsg, const std::string &shaderSource, const std::string &sourceFileName ) {
	static auto logger = LeLog( LOGGER_LABEL );

	std::string errorFileName;  // Will contain the name of the file which contains the error
	std::string errorMessage;   // Will contain error message
	uint32_t    lineNumber = 0; // Will contain error line number after successful parse
	bool        scanResult = false;
	/*
	
	errMsg has the form:  "./triangle.frag:28: error: '' :  syntax error"
	
	Or, on Windows:

	C:\Users\tim\Documents\dev\island\apps\examples\hello_triangle\resources\shaders\default.frag:24: error: 'vertexColor2' : no such field in structure
	C:\Users\tim\Documents\dev\island\apps\examples\hello_triangle\resources\shaders\default.frag:24: error: 'assign' :  cannot convert from 'layout( location=0) in block{ in highp 2-component vector of float texCoord,  in highp 4-component vector of float vertexColor}' to 'layout( location=0) out highp 4-component vector of float'
		
	Note that on Windows, the colon ':' character may be part of the file path, as in "c:\", we therefore 
	use a slightly more involved regular expression instead of sscanf.
	
	*/

	{
		std::cmatch cm;
		scanResult = std::regex_search( errMsg, cm, std::regex( R"regex((.*?):(\d+):\s*error: ?(.*))regex" ) );

		if ( scanResult ) {
			errorFileName = cm[ 1 ].str();
			lineNumber    = std::stoul( cm[ 2 ].str() );
			errorMessage  = cm[ 3 ].str();
		}
	}

	auto errorFilePath  = std::filesystem::canonical( std::filesystem::path( errorFileName ) );
	auto sourceFilePath = std::filesystem::canonical( std::filesystem::path( sourceFileName ) );

	logger.error( "Shader module compilation failed." );
	if ( errorFilePath != sourceFilePath ) {
		// error happened in include file.
		logger.error( "%s contains error in included file:", std::filesystem::relative( std::filesystem::path( sourceFileName ) ).c_str() );
		logger.error( "%s:%d : %s", std::filesystem::relative( errorFilePath ).c_str(), lineNumber, errorMessage.c_str() );
	} else {
		logger.error( "%s:%d : %s", std::filesystem::relative( errorFilePath ).c_str(), lineNumber, errorMessage.c_str() );
	}

	std::istringstream sourceCode( shaderSource );
	std::string        currentLine;

	if ( scanResult ) {

		std::getline( sourceCode, currentLine );

		uint32_t    currentLineNumber = 1; /* Line numbers start counting at 1 */
		std::string currentFilename   = sourceFileName;
		std::string lastFilename      = sourceFileName;

		while ( sourceCode.good() ) {

			// Check for lines inserted by the preprocessor which hold line numbers for included files
			// Such lines have the pattern: '#line 21 "path/to/include.frag"' (without single quotation marks)
			auto wasLineMarker = checkForLineNumberModifier( currentLine, currentLineNumber, currentFilename, lastFilename );

			if ( 0 == strcmp( errorFileName.c_str(), currentFilename.c_str() ) ) {
				if ( ( currentLineNumber > 0 ) && ( currentLineNumber + 3 > lineNumber ) ) {
					std::ostringstream sourceContext;

					const auto shaderSourceCodeLine = wasLineMarker ? "#include \"" + lastFilename + "\"" : currentLine;

					if ( currentLineNumber == lineNumber ) {
						// set console color
						sourceContext << char( 0x1B ) << "[38;5;209m";
					}

					sourceContext << std::right << std::setw( 4 ) << currentLineNumber << " | " << shaderSourceCodeLine;

					if ( currentLineNumber == lineNumber ) {
						// reset console color to defaults
						sourceContext << char( 0x1B ) << "[0m";
					}

					logger.error( "%s", sourceContext.str().c_str() );
				}

				if ( currentLineNumber >= lineNumber + 2 ) {
					logger.error( "" ); // add line break for better readability
					break;
				}
			}
			std::getline( sourceCode, currentLine );
			++currentLineNumber;
		}
	}
}

// ---------------------------------------------------------------

static inline void debug_print_macro_definition( char const *def_start, size_t def_sz, char const *val_start, size_t val_sz ) {
#ifndef NDEBUG
	char def_str[ 256 ]{};
	char val_str[ 256 ]{};

	snprintf( def_str, def_sz + 1, "%s", def_start );
	snprintf( val_str, val_sz + 1, "%s", val_start );
	static auto logger = LeLog( LOGGER_LABEL );
	logger.info( "Inserting macro #define '%s', value: '%s'", def_str, val_str );
#endif
}

// ---------------------------------------------------------------
// Parse macro definitions from macroDefinitionsStr and update given
// `shader_c_compile_options` object with any macro definitions extracted.
//
// Options given as a string + length
// Options string format: "value=12,value_a,value_a=TRUE,,"
//
static void shader_options_parse_macro_definitions_string( shaderc_compile_options *options, char const *macroDefinitionsStr, size_t macroDefinitionsStrSz ) {

	// macroDefinitionsStr   = "value=12,value_a,value_a=TRUE,,";
	// macroDefinitionsStrSz = strlen( macroDefinitionsStr );

	const char *      c       = macroDefinitionsStr;
	const char *const str_end = c + macroDefinitionsStrSz;

	if ( macroDefinitionsStr && macroDefinitionsStrSz != 0 ) {

		// ',' or end of string: triggers new macro being issued.
		// '=' : triggers end of macro definition, start of macro value

		char const *def_start = macroDefinitionsStr; // start of definition string slice
		char const *val_start = nullptr;             // start of value string (may be nullptr)
		size_t      def_sz    = 0;                   // number or characters used for definition (must be >0)
		size_t      val_sz    = 0;                   // number of characters used for value (may be 0)

		for ( ;; c++ ) {

			if ( *c == '=' ) {
				def_sz    = c - def_start;
				val_start = c + 1; // value must follow
				val_sz    = 0;     // reset

			} else if ( *c == ',' || c == str_end ) {

				if ( val_start ) {
					// We're closing a value
					val_sz = c - val_start;
				} else {
					// We're closing a definition
					def_sz = c - def_start;
					val_sz = 0;
				}

				// -- Issue current options state

				if ( def_sz > 0 ) {
					debug_print_macro_definition( def_start, def_sz, val_start, val_sz );
					shaderc_compile_options_add_macro_definition( options, def_start, def_sz, val_start, val_sz );
				}

				// -- reset state for next options

				def_start = c + 1; // next element must be definition
				def_sz    = 0;
				val_start = nullptr; // we don't know value yet.
				val_sz    = 0;
			}

			if ( c == str_end ) {
				// No more characters left to parse.
				break;
			}

		} // end for
	}
}

// ---------------------------------------------------------------

static bool le_shader_compiler_compile_source( le_shader_compiler_o *self, const char *sourceFileText,
                                               size_t sourceFileNumBytes, const LeShaderStageEnum &shaderType,
                                               const char *original_file_path,
                                               char const *macroDefinitionsStr, size_t macroDefinitionsStrSz,
                                               le_shader_compilation_result_o *result ) {
	static auto logger = LeLog( LOGGER_LABEL );

	logger.info( "Compiling shader file: '%s'", original_file_path );

	auto shaderKind = convert_to_shaderc_shader_kind( shaderType );

	// Make a copy of compiler options so that we can add callback pointers only for
	// this compilation.
	auto local_options = shaderc_compile_options_clone( self->options );

	shader_options_parse_macro_definitions_string( local_options, macroDefinitionsStr, macroDefinitionsStrSz );

	shaderc_compile_options_set_include_callbacks(
	    local_options,
	    le_shaderc_include_result_create,
	    le_shaderc_include_result_destroy,
	    &result->includes );

	shaderc_compile_options_set_target_env( local_options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2 );
	shaderc_compile_options_set_target_spirv( local_options, shaderc_spirv_version_1_5 );

	// -- Preprocess GLSL source - this will expand macros and includes
	auto preprocessorResult =
	    shaderc_compile_into_preprocessed_text(
	        self->compiler, sourceFileText, sourceFileNumBytes, shaderKind,
	        original_file_path, "main",
	        local_options );

	// Setup iterator for include paths so that it points to the first element
	// in the sorted set of inlcude paths
	// Once the preprocessor step has completed, the set of include paths for
	// this result object will not ever change again, and the read-out iterator
	// can be set to the start position.
	result->includes.paths_it = result->includes.paths.begin();

	if ( shaderc_result_get_compilation_status( preprocessorResult ) != shaderc_compilation_status_success ) {
		// If preprocessor step was not successful - return preprocessor result
		// to keep the promise of always returning a result object.
		{
			char const *errMsg = shaderc_result_get_error_message( preprocessorResult );
			std::cmatch cm;
			bool        scanResult = std::regex_search( errMsg, cm, std::regex( R"regex((.*?):(\d+):\s*error: ?(.*))regex" ) );

			if ( scanResult ) {
				auto errorFileName = cm[ 1 ].str();
				auto lineNumber    = std::stoul( cm[ 2 ].str() );
				auto errorMessage  = cm[ 3 ].str();
				logger.error( "Shader preprocessor failed: %s:%d",
				              std::filesystem::relative( std::filesystem::path( errorFileName ) ).c_str(), lineNumber );
				logger.error( "%s", errorMessage.c_str() );
			}
		}
		result->result = preprocessorResult;
		shaderc_compile_options_release( local_options );
		return false;
	}

	// ---------| Invariant: Preprocessor step was successful

	// -- Get preprocessed text
	auto preprocessorText         = shaderc_result_get_bytes( preprocessorResult );
	auto preprocessorTextNumBytes = shaderc_result_get_length( preprocessorResult );

	// -- Compile preprocessed GLSL into SPIRV
	result->result =
	    shaderc_compile_into_spv(
	        self->compiler,
	        preprocessorText, preprocessorTextNumBytes,
	        shaderKind,
	        original_file_path,
	        "main",
	        local_options );

	// -- Print error message with context if compilation failed
	if ( shaderc_result_get_compilation_status( result->result ) != shaderc_compilation_status_success ) {
		const char *err_msg = shaderc_result_get_error_message( result->result );
		le_shader_compiler_print_error_context( err_msg, preprocessorText, original_file_path );
	}

	shaderc_compile_options_release( local_options );

	return true;
}

// ---------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_shader_compiler, api_ ) {
	auto  le_shader_compiler_api_i = static_cast<le_shader_compiler_api *>( api_ );
	auto &compiler_i               = le_shader_compiler_api_i->compiler_i;

	compiler_i.create         = le_shader_compiler_create;
	compiler_i.destroy        = le_shader_compiler_destroy;
	compiler_i.compile_source = le_shader_compiler_compile_source;

	compiler_i.result_create       = le_shader_compilation_result_create;
	compiler_i.result_get_bytes    = le_shader_compilation_result_get_result_bytes;
	compiler_i.result_get_success  = le_shader_compilation_result_get_result_success;
	compiler_i.result_get_includes = le_shader_compilation_result_get_next_includes_path;
	compiler_i.result_destroy      = le_shader_compilation_result_detroy;

#ifdef PLUGINS_DYNAMIC
	le_core_load_library_persistently( "libshaderc_shared.so" );
#endif
}
