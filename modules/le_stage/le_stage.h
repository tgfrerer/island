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
struct le_mesh_info;

void register_le_stage_api( void *api );

// clang-format off
struct le_stage_api {


	static constexpr auto id      = "le_stage";
	static constexpr auto pRegFun = register_le_stage_api;

	struct le_stage_interface_t {

		le_stage_o *    ( * create                   ) ( le_renderer_o* renderer);
		void            ( * destroy                  ) ( le_stage_o* self );
		void            ( * update                   ) ( le_stage_o* self );
		
		void			( * update_rendermodule )(le_stage_o* self, le_render_module_o* module);

		uint32_t (* create_buffer)( le_stage_o *stage, void *mem, uint32_t sz, char const *debug_name );
		uint32_t (* create_buffer_view)( le_stage_o *self, le_buffer_view_info const *info );
		uint32_t (* create_accessor)( le_stage_o *self, le_accessor_info const *info );
		uint32_t (* create_mesh)(le_stage_o* self, le_mesh_info const * info);
		
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
