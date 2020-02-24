#ifndef GUARD_LE_GLTF_LOADER_H
#define GUARD_LE_GLTF_LOADER_H

#include "le_core/le_core.h"

#ifdef __cplusplus
extern "C" {
#endif

struct le_gltf_document_o;
struct le_renderer_o;
struct le_command_buffer_encoder_o;
struct le_resource_info_t;
struct le_pipeline_manager_o;

struct GltfUboMvp;

struct le_resource_handle_t;

// clang-format off
struct le_gltf_loader_api {

	struct le_gltf_loader_interface_t {

		le_gltf_document_o * ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_gltf_document_o* self );

		bool                 ( *load_from_text            ) ( le_gltf_document_o* self, char const * path);
		void                 ( *setup_resources           ) ( le_gltf_document_o *self, le_renderer_o *renderer, le_pipeline_manager_o* pipeline_manager );
		void                 ( *get_resource_infos        ) ( le_gltf_document_o *self, le_resource_info_t **infos, le_resource_handle_t const **handles, size_t *numResources );
		void                 ( *upload_resource_data      ) ( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder );
		void                 ( *draw                      ) ( le_gltf_document_o *self, le_command_buffer_encoder_o *encoder,  GltfUboMvp const * mvp );
	};

	le_gltf_loader_interface_t le_gltf_loader_api_i;
};
// clang-format on
LE_MODULE( le_gltf_loader );
LE_MODULE_LOAD_DEFAULT( le_gltf_loader );

#ifdef __cplusplus
} // extern c

namespace le_gltf_loader {
static const auto &api             = le_gltf_loader_api_i;
static const auto &gltf_document_i = api -> le_gltf_loader_api_i;
} // namespace le_gltf_loader

#endif

#endif
