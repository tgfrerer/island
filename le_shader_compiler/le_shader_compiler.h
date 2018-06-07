#ifndef GUARD_LE_SHADER_COMPILER_H
#define GUARD_LE_SHADER_COMPILER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_shader_compiler_o;

void register_le_shader_compiler_api( void *api );

// clang-format off

struct le_shader_compiler_api {
    static constexpr auto id      = "le_shader_compiler";
    static constexpr auto pRegFun = register_le_shader_compiler_api;

    struct compiler_interface_t {
        le_shader_compiler_o* (* create  ) ( );
        void                  (* destroy ) ( le_shader_compiler_o* self );

    };

    compiler_interface_t compiler_i;
};

// clang-format on

#ifdef __cplusplus
}
#endif

#endif
