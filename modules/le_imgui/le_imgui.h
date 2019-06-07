#ifndef GUARD_le_imgui_H
#define GUARD_le_imgui_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_imgui_o;

struct le_renderpass_o;    // declared in le_renderer.h
struct le_render_module_o; // declared in le_renderer.h
struct LeUiEvent;          // declared in le_ui_event.h

void register_le_imgui_api( void *api );

// clang-format off
struct le_imgui_api {
	static constexpr auto id      = "le_imgui";
	static constexpr auto pRegFun = register_le_imgui_api;

	struct le_imgui_interface_t {

		le_imgui_o *    ( * create            ) ( );
		void            ( * destroy           ) ( le_imgui_o* self );

		void            ( * setup_gui_resources )( le_imgui_o *self, le_render_module_o *p_render_module, float display_width, float display_height );
		
		void            ( * begin_frame             ) ( le_imgui_o* self);
		void            ( * end_frame               ) ( le_imgui_o* self);
		void            ( * draw_gui          ) ( le_imgui_o* self, le_renderpass_o* renderpass);
		void            ( * process_events    ) ( le_imgui_o* self, LeUiEvent const * events, size_t numEvents);

	};

	le_imgui_interface_t       le_imgui_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_imgui {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_imgui_api>( true );
#	else
const auto api = Registry::addApiStatic<le_imgui_api>();
#	endif

static const auto &le_imgui_i = api -> le_imgui_i;

} // namespace

#endif // __cplusplus

#endif
