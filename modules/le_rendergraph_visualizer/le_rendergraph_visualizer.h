#ifndef GUARD_le_rendergraph_visualizer_H
#define GUARD_le_rendergraph_visualizer_H

#include "le_core.h"

/*

    Keyboard Shortcuts:

    SPACE         :	 Toggle Live/Playback state
    LEFT  CURSOR  :  When in Playback state, move backward one frame
    RIGHT CURSOR  :  When in Playback state, move forward  one frame
    R             :  Reset Zoom and center the graph
    Z             :  Toggle magnifying glass
    F10           :  Toggle show / hide (deactivated when hidden)

    Mouse Wheel   :  Zoom
    Mouse grab    :  Move Diagram


    Note that the visualizer is stateful, and will respond to
    windowExtent events.


// API USE

    1) Add a le::RendergraphVisualizer object to your app_o

    ```
    le::RendergraphVisualizer rendergraph_visualizer;
    ```

    2) In the app's update method, just before updating the rendergraph, call:

    ```
    app->rendergraph_visualizer.update(rendergraph);
    ```

    3) Add to the ui event loop:

    ```
    app->rendergraph_visualizer.processAndFilterEvents(events.data(), &events_sz);
    events.resize(events_sz);
    ```



 */

struct le_renderer_o;
struct le_rendergraph_o;
struct le_rendergraph_visualizer_o;
struct le_image_resource_handle_t;
struct LeUiEvent;

// clang-format off
struct le_rendergraph_visualizer_api {


	struct le_rendergraph_visualizer_interface_t {

		le_rendergraph_visualizer_o *    ( * create                   ) ( le_renderer_o* renderer, uint32_t initial_window_w, uint32_t initial_window_h);
		void                 			 ( * destroy                  ) ( le_rendergraph_visualizer_o* self );
		void                 			 ( * update                   ) ( le_rendergraph_visualizer_o* self, le_rendergraph_o* rendergraph, le_image_resource_handle_t* target_image);
	
		void 							 ( * set_is_active            ) (le_rendergraph_visualizer_o* self, bool is_active);
		bool 							 ( * get_is_active            ) (le_rendergraph_visualizer_o* self);

		void                			 ( * process_events           ) ( le_rendergraph_visualizer_o* self, LeUiEvent const * events, uint32_t num_events);
        
        /// Process events, and filter out any events which have been captured
        /// 
        /// Events which have not been captured are moved to the front; sort 
        /// order is preserved.`num_events` is updated to count only these 
        /// events which have not been captured.
        /// 
        /// No re-allocation is going to happen.
		void	            ( * process_and_filter_events ) (le_rendergraph_visualizer_o*self, LeUiEvent *events, uint32_t* num_events);

	};

	le_rendergraph_visualizer_interface_t       le_rendergraph_visualizer_i;
};
// clang-format on

LE_MODULE( le_rendergraph_visualizer );
LE_MODULE_LOAD_DEFAULT( le_rendergraph_visualizer );

#ifdef __cplusplus

namespace le_rendergraph_visualizer {
	static const auto &api = le_rendergraph_visualizer_api_i;
	static const auto &le_rendergraph_visualizer_i = api->le_rendergraph_visualizer_i;
} // namespace

class LeRendergraphVisualizer : NoCopy, NoMove {

	le_rendergraph_visualizer_o *self;

  public:
	static constexpr uint32_t C_WINDOW_W_FALLBACK = 800; /// < This is the fallback value that is used in case no initial window width is specified
	static constexpr uint32_t C_WINDOW_H_FALLBACK = 600; ///< fallback value in case no initial window height is specified

	LeRendergraphVisualizer( le_renderer_o* renderer, bool active_by_default = true, uint32_t const& initial_window_w = C_WINDOW_W_FALLBACK, uint32_t const& initial_window_h = C_WINDOW_H_FALLBACK )
	    : self( le_rendergraph_visualizer::le_rendergraph_visualizer_i.create( renderer, initial_window_w, initial_window_h ) ) {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.set_is_active( self, active_by_default );
	}

	~LeRendergraphVisualizer() {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.destroy( self );
	}

	void update( le_rendergraph_o* rendergraph, le_image_resource_handle_t* target_image ) {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.update( self, rendergraph, target_image );
	}

	void processEvents( LeUiEvent const* events, uint32_t num_events ) {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.process_events( self, events, num_events );
	};

	void processAndFilterEvents( LeUiEvent* events, uint32_t* num_events ) {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.process_and_filter_events( self, events, num_events );
	};

	void show() {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.set_is_active( self, true );
	}

	void hide() {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.set_is_active( self, false );
	}

	bool is_active() {
		return le_rendergraph_visualizer::le_rendergraph_visualizer_i.get_is_active( self );
	}

	operator auto () {
		return self;
	}

};

namespace le {

using RendergraphVisualizer = LeRendergraphVisualizer;

}

#endif // __cplusplus

#endif
