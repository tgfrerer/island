#include "pal_api_loader/ApiRegistry.hpp"

/// Note that the shader compiler depends on the shared lib version of libshaderc
/// this version is available through the LunarG VULKAN SDK,
///
/// But has to be compiled manually.
///
/// For this, go to $VULKAN_SDK, edit `build_tools.sh` so that, in method `buildShaderc`,
/// build type says: `-DCMAKE_BUILD_TYPE=Release`,
/// then let it create an additional symlink:
/// ln -sf "$PWD"/build/libshaderc/libshaderc_combined.so "${LIBDIR}"/libshaderc

#include "le_shader_compiler/le_shader_compiler.h"

#include "libshaderc/shaderc.h"

#include "le_renderer/le_renderer.h" // for shader type

#include <iomanip>
#include <iostream>
#include <assert.h>

struct le_shader_compiler_o {
	shaderc_compiler_t        compiler;
	shaderc_compile_options_t options;
};

struct le_shader_compilation_result_o {
	shaderc_compilation_result *result = nullptr;
};

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

static le_shader_compilation_result_o *le_shader_compiler_compile_source( le_shader_compiler_o *self, const char *sourceText, size_t sourceTextSize, LeShaderType shaderType, const char *original_file_path ) {

	auto shaderKind = convert_to_shaderc_shader_kind( shaderType );

	auto result = le_shader_compilation_result_create();

	result->result = shaderc_compile_into_spv(
	    self->compiler, sourceText, sourceTextSize, shaderKind,
	    original_file_path, "main",
	    self->options );

	// -- extract the bits from compilation result which we may want to pass on

	auto compilation_status = shaderc_result_get_compilation_status( result->result );

	if ( compilation_status != shaderc_compilation_status_success ) {

		// -- get warnings

		// -- print out warnings
	}

	// -- note: must create copy if want to hold on to anything, as elements will be released at end of this method

	// -- notify if failure or warnings happened

	// release shader compilation result
	return result;
}

// ---------------------------------------------------------------

static void le_shader_compiler_destroy( le_shader_compiler_o *self ) {
	shaderc_compiler_release( self->compiler );

	std::cout << "Destroyed shader compiler" << std::endl
	          << std::flush;
	delete self;
}

// ---------------------------------------------------------------

ISL_API_ATTR void register_le_shader_compiler_api( void *api_ ) {
	auto  le_shader_compiler_api_i = static_cast<le_shader_compiler_api *>( api_ );
	auto &compiler_i               = le_shader_compiler_api_i->compiler_i;

	compiler_i.create             = le_shader_compiler_create;
	compiler_i.destroy            = le_shader_compiler_destroy;
	compiler_i.compile_source     = le_shader_compiler_compile_source;
	compiler_i.get_result_bytes   = le_shader_compilation_result_get_result_bytes;
	compiler_i.get_result_success = le_shader_compilation_result_get_result_success;
	compiler_i.release_result     = le_shader_compilation_result_detroy;

	Registry::loadLibraryPersistently( "libshaderc_shared.so" );
}
