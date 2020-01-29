#ifndef GUARD_le_stage_H
#define GUARD_le_stage_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_stage_o;
struct le_renderer_o;
struct le_render_module_o;
struct le_buffer_view_info;
struct le_accessor_info;
struct le_material_info;
struct le_mesh_info;
struct le_node_info;
struct le_sampler_info;
struct le_camera_settings_info;
struct le_camera_o; // from module::le_camera

void register_le_stage_api( void *api );

// clang-format off
struct le_stage_api {


	static constexpr auto id      = "le_stage";
	static constexpr auto pRegFun = register_le_stage_api;

		struct draw_params_t {
			le_stage_o* stage;
			le_camera_o* camera;
		};

	struct le_stage_interface_t {

		le_stage_o *    ( * create                   ) ( le_renderer_o* renderer);
		void            ( * destroy                  ) ( le_stage_o* self );
		void            ( * update                   ) ( le_stage_o* self );
		
		void			( * update_rendermodule )(le_stage_o* self, le_render_module_o* module);
		void            ( * draw_into_module )(draw_params_t* self, le_render_module_o* module);

		void            ( * setup_pipelines)(le_stage_o* self);

		uint32_t (* create_image_from_memory)( le_stage_o* stage, unsigned char const * image_file_memory, uint32_t image_file_sz, char const * debug_name);
		uint32_t (* create_image_from_file_path)( le_stage_o* stage, char const * image_file_path, char const * debug_name);

		uint32_t (*create_sampler)(le_stage_o* stage, le_sampler_info* info);
		uint32_t (* create_buffer)( le_stage_o *stage, void *mem, uint32_t sz, char const *debug_name );
		uint32_t (* create_buffer_view)( le_stage_o *self, le_buffer_view_info const *info );
		uint32_t (* create_accessor)( le_stage_o *self, le_accessor_info const *info );
		uint32_t (* create_material)(le_stage_o* self, le_material_info const * info);
		uint32_t (* create_mesh)(le_stage_o* self, le_mesh_info const * info);
		uint32_t (* create_nodes)( le_stage_o *self, le_node_info *info, size_t num_nodes );
		uint32_t (* create_camera_settings) (le_stage_o * self, le_camera_settings_info* info, size_t num_camera_settings);
		uint32_t (* create_scene)( le_stage_o *self, uint32_t *node_idx, uint32_t node_idx_count );
	};

	le_stage_interface_t       le_stage_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_stage {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_stage_api>( true );
#	else
const auto api = Registry::addApiStatic<le_stage_api>();
#	endif

static const auto &le_stage_i = api -> le_stage_i;

} // namespace le_stage

class LeStage : NoCopy, NoMove {

	le_stage_o *self;

  public:
	LeStage( le_renderer_o *renderer )
	    : self( le_stage::le_stage_i.create( renderer ) ) {
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
