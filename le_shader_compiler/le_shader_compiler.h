#ifndef GUARD_LE_SHADER_COMPILER_H
#define GUARD_LE_SHADER_COMPILER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_shader_compiler_o;
struct shaderc_include_result;

struct le_shader_compilation_result_o;

enum class LeShaderType : uint64_t; // defined in renderer.h

void register_le_shader_compiler_api( void *api );

// clang-format off
struct le_shader_compiler_api {
    static constexpr auto id      = "le_shader_compiler";
    static constexpr auto pRegFun = register_le_shader_compiler_api;

    struct compiler_interface_t {
        le_shader_compiler_o* (* create  ) ( );
        void                  (* destroy ) ( le_shader_compiler_o* self );

		le_shader_compilation_result_o* (*compile_source)( le_shader_compiler_o *compiler, const char *sourceText, size_t sourceTextSize, LeShaderType shaderType, const char *original_file_path );

		void (*get_result_bytes)(le_shader_compilation_result_o* res, const char** pAddr, size_t* pNumBytes);
		bool (*get_result_success)(le_shader_compilation_result_o* res);
		void (*release_compilation_result)(le_shader_compilation_result_o* res);
		void (*release_result)(le_shader_compilation_result_o* res);
    };

	// function pointer signature for shader include callback
	typedef shaderc_include_result *( *shaderc_include_resolve_fn )(
	    void *      user_data,
	    const char *requested_source,
	    int         type,
	    const char *requesting_source,
	    size_t      include_depth );

	compiler_interface_t       compiler_i;
	shaderc_include_resolve_fn include_resolve_pfn;
};
// clang-format on

#ifdef __cplusplus
}
#endif

#endif
