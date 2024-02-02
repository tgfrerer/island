#ifndef GUARD_le_imgui_H
#define GUARD_le_imgui_H

#include "le_core.h"

struct le_imgui_o;
struct le_renderpass_o;  // declared in le_renderer.h
struct le_rendergraph_o; // declared in le_renderer.h
struct LeUiEvent;        // declared in le_ui_event.h

// clang-format off
struct le_imgui_api {

	struct le_imgui_interface_t {

		le_imgui_o *    ( * create            ) ( );
		void            ( * destroy           ) ( le_imgui_o* self );

		
		void            ( * begin_frame             ) ( le_imgui_o* self);
		void            ( * end_frame               ) ( le_imgui_o* self);

		void            ( * setup_resources )( le_imgui_o *self, le_rendergraph_o *p_rendergraph, float display_width, float display_height );
		void            ( * draw            )( le_imgui_o* self, le_renderpass_o* renderpass);


		void            ( * process_events    ) ( le_imgui_o* self, LeUiEvent const * events, uint32_t num_events);
        
        /// Process events, and filter out any events which have been captured by imgui
        /// 
        /// Call `process_events`, then update `events` so that events which have not been 
        /// captured by imgui are moved to the front.
        /// Update `num_events` to count only events which have not been captured by imgui. 
        /// No re-allocation is going to happen.
		void	            ( * process_and_filter_events ) (le_imgui_o*self, LeUiEvent *events, uint32_t* num_events);

        void (*register_set_clipboard_string_cb)(le_imgui_o* self, void* cb_addr);
        void (*register_get_clipboard_string_cb)(le_imgui_o* self, void* cb_addr);
	};


	le_imgui_interface_t       le_imgui_i;
};
// clang-format on

LE_MODULE( le_imgui );
LE_MODULE_LOAD_DEFAULT( le_imgui );

#ifdef __cplusplus

namespace le_imgui {
static const auto& api        = le_imgui_api_i;
static const auto& le_imgui_i = api->le_imgui_i;

} // namespace le_imgui

#endif // __cplusplus

#if ( WIN32 )
#	pragma comment( lib, "bin/modules/imgui.lib " )
#endif

#endif
