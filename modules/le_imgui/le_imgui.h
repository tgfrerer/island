#ifndef GUARD_le_imgui_H
#define GUARD_le_imgui_H

#include "le_core.h"

struct le_imgui_o;
struct le_renderpass_o;    // declared in le_renderer.h
struct le_render_module_o; // declared in le_renderer.h
struct LeUiEvent;          // declared in le_ui_event.h

// clang-format off
struct le_imgui_api {

	struct le_imgui_interface_t {

		le_imgui_o *    ( * create            ) ( );
		void            ( * destroy           ) ( le_imgui_o* self );

		
		void            ( * begin_frame             ) ( le_imgui_o* self);
		void            ( * end_frame               ) ( le_imgui_o* self);

		void            ( * setup_resources )( le_imgui_o *self, le_render_module_o *p_render_module, float display_width, float display_height );
		void            ( * draw            )( le_imgui_o* self, le_renderpass_o* renderpass);

		void            ( * process_events    ) ( le_imgui_o* self, LeUiEvent const * events, size_t numEvents);

	};

	le_imgui_interface_t       le_imgui_i;
};
// clang-format on

LE_MODULE( le_imgui );
LE_MODULE_LOAD_DEFAULT( le_imgui );

#ifdef __cplusplus

namespace le_imgui {
static const auto& api        = le_imgui_api_i;
static const auto& le_imgui_i = api -> le_imgui_i;

} // namespace le_imgui

#endif // __cplusplus

#endif
