#ifndef GUARD_LE_SHADER_COMPILER_H
#define GUARD_LE_SHADER_COMPILER_H

#include <stdint.h>
#include "le_core.h"

struct le_shader_compiler_o;
struct le_shader_compilation_result_o;
struct LeShaderStageEnum; // defined in renderer_types.h
struct LeShaderSourceLanguageEnum;

// clang-format off
struct le_shader_compiler_api {

    struct compiler_interface_t {

		le_shader_compiler_o*           (* create                ) ( );
		void                            (* destroy               ) ( le_shader_compiler_o* self );

		bool                            (* compile_source        ) ( le_shader_compiler_o *compiler, const char *sourceText, size_t sourceTextSize, const LeShaderSourceLanguageEnum& shader_source_language, const LeShaderStageEnum& shaderType, const char *original_file_path, char const * macroDefinitionsStr, size_t macroDefinitionsStrSz, le_shader_compilation_result_o* result );

        // create a compilation result object - this is needed for compile_source 
		le_shader_compilation_result_o* (* result_create         ) ( );
		
        /// \brief  iterate over include paths in current compilation result
		/// \return false if no more paths, otherwise: true, and updates to pPath and pStrSz as side-effect
		/// \note   lifetime of any pointers is tied to life-time of result object
		bool                            (* result_get_includes   ) ( le_shader_compilation_result_o* res, const char** pPath, size_t* pStrSz);
		bool                            (* result_get_success    ) ( le_shader_compilation_result_o* res);

        // pAddr receives a pointer to spir-v binary code - this is guaranteed to be castable to uint32_t. 
		void                            (* result_get_bytes      ) ( le_shader_compilation_result_o* res, const char** p_spir_v_bytes, size_t* pNumBytes);

		void                            (* result_destroy        ) ( le_shader_compilation_result_o* res);
    };

	compiler_interface_t       compiler_i;
};
// clang-format on

LE_MODULE( le_shader_compiler );
LE_MODULE_LOAD_DEFAULT( le_shader_compiler );

#ifdef __cplusplus

namespace le_shader_compiler {
static const auto &api        = le_shader_compiler_api_i;
static const auto &compiler_i = api -> compiler_i;
} // namespace le_shader_compiler

#endif

#endif
