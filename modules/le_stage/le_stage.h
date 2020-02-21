#ifndef GUARD_le_stage_H
#define GUARD_le_stage_H

#include <stdint.h>
#include "le_core/le_core.h"

struct le_renderer_o; // from le_renderer
struct le_timebase_o; // from le_timebase

struct le_stage_o;
struct le_render_module_o;
struct le_buffer_view_info;
struct le_accessor_info;
struct le_material_info;
struct le_mesh_info;
struct le_node_info;
struct le_sampler_info;
struct le_animation_sampler_info;
struct le_texture_info;
struct le_camera_settings_info;
struct le_animation_info;
struct le_skin_info;
struct le_camera_o; // from module::le_camera

struct LeSamplerInfo; // from le_renderer

// clang-format off
struct le_stage_api {

		struct draw_params_t {
			le_stage_o* stage;
			le_camera_o* camera;
		};

	struct le_stage_interface_t {

		le_stage_o *    ( * create                   ) ( le_renderer_o* renderer, le_timebase_o* timebase);
		void            ( * destroy                  ) ( le_stage_o* self );
		void            ( * update                   ) ( le_stage_o* self );
		
		void			( * update_rendermodule )(le_stage_o* self, le_render_module_o* module);
		void            ( * draw_into_module )(draw_params_t* self, le_render_module_o* module);

		void            ( * setup_pipelines)(le_stage_o* self);

		uint32_t (* create_image_from_memory)( le_stage_o* stage, unsigned char const * image_file_memory, uint32_t image_file_sz, char const * debug_name, uint32_t mip_levels);
		uint32_t (* create_image_from_file_path)( le_stage_o* stage, char const * image_file_path, char const * debug_name, uint32_t mip_levels);

		uint32_t (* create_sampler)(le_stage_o* stage, LeSamplerInfo const * info);
		uint32_t (* create_texture)(le_stage_o* stage, le_texture_info const * info);

		uint32_t (* create_buffer)( le_stage_o *stage, void *mem, uint32_t sz, char const *debug_name );
		uint32_t (* create_buffer_view)( le_stage_o *self, le_buffer_view_info const *info );
		uint32_t (* create_accessor)( le_stage_o *self, le_accessor_info const *info );
		uint32_t (* create_material)(le_stage_o* self, le_material_info const * info);
		uint32_t (* create_mesh)(le_stage_o* self, le_mesh_info const * info);

		uint32_t (* create_nodes)( le_stage_o *self, le_node_info const *info, size_t num_nodes );
		uint32_t (* create_camera_settings) (le_stage_o * self, le_camera_settings_info const * info, size_t num_camera_settings);
		uint32_t (* create_animation)(le_stage_o* self, le_animation_info const * info);
		uint32_t (* create_skin)(le_stage_o* self, le_skin_info const * info);

		void     (* node_set_skin)(le_stage_o*, uint32_t node_idx, uint32_t skin_idx);

		uint32_t (* create_scene)( le_stage_o *self, uint32_t *node_idx, uint32_t node_idx_count );
	};

	le_stage_interface_t       le_stage_i;
};
// clang-format on

LE_MODULE( le_stage );
LE_MODULE_LOAD_DEFAULT( le_stage );

#ifdef __cplusplus

namespace le_stage {
static const auto &api        = le_stage_api_i;
static const auto &le_stage_i = api -> le_stage_i;
} // namespace le_stage

class LeStage : NoCopy, NoMove {

	le_stage_o *self;

  public:
	LeStage( le_renderer_o *renderer, le_timebase_o *timebase = nullptr )
	    : self( le_stage::le_stage_i.create( renderer, timebase ) ) {
	}

	~LeStage() {
		le_stage::le_stage_i.destroy( self );
	}

	void update() {
		le_stage::le_stage_i.update( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
