#ifndef GUARD_LE_GLTF_LOADER_H
#define GUARD_LE_GLTF_LOADER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_gltf_loader_o;

void register_le_gltf_loader_api( void *api );

// clang-format off
struct le_gltf_loader_api {
	static constexpr auto id      = "le_gltf_loader";
	static constexpr auto pRegFun = register_le_gltf_loader_api;

	struct gltf_loader_interface_t {

		le_gltf_loader_o *           (* create                ) ( );
		void                         (* destroy               ) ( le_gltf_loader_o* self );

		bool (*load_from_text)(le_gltf_loader_o* self, char const * path);

	};

	gltf_loader_interface_t       loader_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c
#endif

#endif
