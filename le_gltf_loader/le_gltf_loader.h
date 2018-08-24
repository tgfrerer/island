#ifndef GUARD_LE_GLTF_LOADER_H
#define GUARD_LE_GLTF_LOADER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_gltf_document_o;
struct le_renderer_o;
struct le_command_buffer_encoder_o;
struct le_resource_info_t;

struct GltfUboMvp;

LE_DEFINE_HANDLE( LeResourceHandle )

void register_le_gltf_loader_api( void *api );

// clang-format off
struct le_gltf_loader_api {
	static constexpr auto id      = "le_gltf_loader";
	static constexpr auto pRegFun = register_le_gltf_loader_api;

	struct gltf_document_interface_t {

		le_gltf_document_o * ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_gltf_document_o* self );

		bool                 ( *load_from_text            ) ( le_gltf_document_o* self, char const * path);
		void                 ( *setup_resources           ) ( le_gltf_document_o *self, le_renderer_o *renderer );
		void                 ( *get_resource_infos        ) ( le_gltf_document_o *self, le_resource_info_t **infos, LeResourceHandle const **handles, size_t *numResources );
		void                 ( *upload_resource_data      ) ( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder );
		void                 ( *draw                      ) ( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder,  GltfUboMvp const * mvp );
	};

	gltf_document_interface_t       document_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c
#endif

#endif
