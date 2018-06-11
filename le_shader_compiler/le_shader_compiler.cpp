#include "pal_api_loader/ApiRegistry.hpp"

/// Note that the shader compiler depends on the shared lib version of libshaderc
/// this version is available through the LunarG VULKAN SDK,
///
/// But has to be compiled manually.
///
/// For this, go to $VULKAN_SDK, edit `build_tools.sh` so that, in method `buildShaderc`,
/// build type says: `-DCMAKE_BUILD_TYPE=Release`,
/// then let it create an additional symlink:
/// ln -sf "$PWD"/build/libshaderc/libshaderc_shared.so "${LIBDIR}"/libshaderc

#include "le_shader_compiler/le_shader_compiler.h"

#include "libshaderc/shaderc.h"

#include "le_renderer/le_renderer.h" // for shader type

#include <iomanip>
#include <iostream>
#include <assert.h>

#include "experimental/filesystem" // for parsing shader source file paths
#include <fstream>                 // for reading shader source files
#include <cstring>                 // for memcpy
#include <vector>
#include <set>

namespace std {
using namespace experimental; // bring filesystem into std::namespace
}

struct le_shader_compiler_o {
	shaderc_compiler_t        compiler;
	shaderc_compile_options_t options;
};

// ---------------------------------------------------------------

struct FileData {
	std::filesystem::path path;     /// path to file
	std::vector<char>     contents; /// contents of file
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

static shaderc_shader_kind convert_to_shaderc_shader_kind( LeShaderType type ) {
	shaderc_shader_kind result;

	switch ( type ) {
	case ( LeShaderType::eFrag ):
		result = shaderc_shader_kind::shaderc_glsl_fragment_shader;
	    break;
	case ( LeShaderType::eNone ):
		result = shaderc_shader_kind::shaderc_glsl_default_vertex_shader;
	    break;
	case ( LeShaderType::eVert ):
		result = shaderc_shader_kind::shaderc_glsl_vertex_shader;
	    break;
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

static void le_shader_compilation_result_get_result_bytes( le_shader_compilation_result_o *res, const char **pAddr, size_t *pNumBytes ) {

	assert( res->result );

	*pAddr     = shaderc_result_get_bytes( res->result );
	*pNumBytes = shaderc_result_get_length( res->result );
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
		auto &o = obj->options = shaderc_compile_options_initialize();
		shaderc_compile_options_set_generate_debug_info( o );
		shaderc_compile_options_set_source_language( o, shaderc_source_language::shaderc_source_language_glsl );
	}

	return obj;
}

// ---------------------------------------------------------------

static void le_shader_compiler_destroy( le_shader_compiler_o *self ) {
	shaderc_compiler_release( self->compiler );

	std::cout << "Destroyed shader compiler" << std::endl
	          << std::flush;
	delete self;
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path &file_path, bool *success ) {

	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		std::cerr << "Unable to open file: " << std::filesystem::canonical( file_path ) << std::endl
		          << std::flush;
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

static shaderc_include_result *
le_shaderc_include_result_create(
    void *      user_data,
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

		includesList->paths.insert( requested_source_path );
		fileData->path = requested_source_path;

		// -- load file contents into fileData
		fileData->contents = load_file( fileData->path, &loadSuccess );

	} else {
		// empty path singals failed inclusion to shaderc
		fileData->path = "";
	}

	if ( false == loadSuccess ) {

		// Store error message instead of file contents.
		const std::string error_message{"Could not load file specified: '" + fileData->path.string() + "'"};

		fileData->contents.assign( error_message.begin(), error_message.end() );
	}

	self->user_data          = fileData;
	self->content            = fileData->contents.data();
	self->content_length     = fileData->contents.size();
	self->source_name        = fileData->path.c_str();
	self->source_name_length = fileData->path.string().size();

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

static le_shader_compilation_result_o *le_shader_compiler_compile_source( le_shader_compiler_o *self, const char *sourceFileText, size_t sourceFileNumBytes, LeShaderType shaderType, const char *original_file_path ) {

	auto shaderKind = convert_to_shaderc_shader_kind( shaderType );

	auto result = le_shader_compilation_result_create();

	// Make a copy of compiler options so that we can add callback pointers only for
	// this compilation.
	auto local_options = shaderc_compile_options_clone( self->options );

	shaderc_compile_options_set_include_callbacks( local_options,
	                                               le_shaderc_include_result_create,
	                                               le_shaderc_include_result_destroy,
	                                               &result->includes );

	// -- First preprocess GLSL source
	auto preprocessorResult = shaderc_compile_into_preprocessed_text(
	    self->compiler, sourceFileText, sourceFileNumBytes, shaderKind,
	    original_file_path, "main",
	    local_options );

	// -- free local compiler options - this will also free any pointers to our callbacks.
	shaderc_compile_options_release( local_options );

	// Refresh iterator for include paths so that it points to the first element
	// in the sorted set.
	result->includes.paths_it = result->includes.paths.begin();

	// If preprocessor step was not successful - return preprocessor result
	// to upkeep the promise of always returning a result object.
	if ( shaderc_result_get_compilation_status( preprocessorResult ) != shaderc_compilation_status_success ) {

		std::cerr << "ERROR: Shader preprocessor failed: " << std::endl
		          << shaderc_result_get_error_message( preprocessorResult ) << std::flush;

		result->result = preprocessorResult;
		return result;
	}

	// ---------| Invariant: Preprocessor step was successful

	// -- Get preprocessed text
	auto preprocessorText         = shaderc_result_get_bytes( preprocessorResult );
	auto preprocessorTextNumBytes = shaderc_result_get_length( preprocessorResult );

	// -- Compile preprocessed GLSL into SPIRV
	result->result = shaderc_compile_into_spv(
	    self->compiler, preprocessorText, preprocessorTextNumBytes, shaderKind,
	    original_file_path, "main",
	    self->options );

	// -- free preprocessor compilation result
	shaderc_result_release( preprocessorResult );

	return result;
}

// ---------------------------------------------------------------

ISL_API_ATTR void register_le_shader_compiler_api( void *api_ ) {
	auto  le_shader_compiler_api_i = static_cast<le_shader_compiler_api *>( api_ );
	auto &compiler_i               = le_shader_compiler_api_i->compiler_i;

	compiler_i.create              = le_shader_compiler_create;
	compiler_i.destroy             = le_shader_compiler_destroy;
	compiler_i.compile_source      = le_shader_compiler_compile_source;
	compiler_i.get_result_bytes    = le_shader_compilation_result_get_result_bytes;
	compiler_i.get_result_success  = le_shader_compilation_result_get_result_success;
	compiler_i.get_result_includes = le_shader_compilation_result_get_next_includes_path;
	compiler_i.release_result      = le_shader_compilation_result_detroy;

	Registry::loadLibraryPersistently( "libshaderc_shared.so" );
}
