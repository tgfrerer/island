#ifndef GUARD_LE_SHADER_COMPILER_H
#define GUARD_LE_SHADER_COMPILER_H

#include "le_core.h"

#include "le_shader_compiler_interface.h"

// clang-format off
struct le_shader_compiler_api {
	le_shader_compiler_interface_t*       compiler_i = nullptr;
};
// clang-format on

LE_MODULE( le_shader_compiler );
LE_MODULE_LOAD_DEFAULT( le_shader_compiler );

#ifdef __cplusplus

namespace le_shader_compiler {
static const auto& api        = le_shader_compiler_api_i;
} // namespace le_shader_compiler

#endif

#endif
