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

struct le_shader_compiler_o {
	shaderc_compiler_t compiler;
};

// ---------------------------------------------------------------

static le_shader_compiler_o *le_shader_compiler_create() {
	auto obj      = new le_shader_compiler_o();
	obj->compiler = shaderc_compiler_initialize();
	return obj;
}

// ---------------------------------------------------------------

static void le_shader_compiler_destroy( le_shader_compiler_o *self ) {
	shaderc_compiler_release( self->compiler );
	delete self;
	self = nullptr;
}

// ---------------------------------------------------------------

ISL_API_ATTR void register_le_shader_compiler_api( void *api_ ) {
	auto  le_shader_compiler_api_i = static_cast<le_shader_compiler_api *>( api_ );
	auto &compiler_i               = le_shader_compiler_api_i->compiler_i;

	compiler_i.create  = le_shader_compiler_create;
	compiler_i.destroy = le_shader_compiler_destroy;

	Registry::loadLibraryPersistently( "libshaderc_shared.so" );
}
