#ifndef GUARD_LE_SHADER_COMPILER_H
#define GUARD_LE_SHADER_COMPILER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_shader_compiler_o;

struct le_shader_compilation_result_o;

enum class LeShaderType : uint64_t; // defined in renderer.h

void register_le_shader_compiler_api( void *api );

// clang-format off
struct le_shader_compiler_api {
    static constexpr auto id      = "le_shader_compiler";
    static constexpr auto pRegFun = register_le_shader_compiler_api;

    struct compiler_interface_t {

		le_shader_compiler_o*           (* create                ) ( );
		void                            (* destroy               ) ( le_shader_compiler_o* self );

		le_shader_compilation_result_o* (* compile_source        ) ( le_shader_compiler_o *compiler, const char *sourceText, size_t sourceTextSize, LeShaderType shaderType, const char *original_file_path );

		/// \brief  iterate over include paths in current compilation result
		/// \return false if no more paths, otherwise: true, and updates to pPath and pStrSz as side-effect
		/// \note   lifetime of any pointers is tied to life-time of result object
		bool                            (* get_result_includes   ) ( le_shader_compilation_result_o* res, const char** pPath, size_t* pStrSz);

		bool                            (* get_result_success    ) ( le_shader_compilation_result_o* res);
		void                            (* get_result_bytes      ) ( le_shader_compilation_result_o* res, const char** pAddr, size_t* pNumBytes);
		void                            (* release_result        ) ( le_shader_compilation_result_o* res);
    };

	compiler_interface_t       compiler_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c
#endif

#endif
