#ifndef GUARD_le_rendergraph_visualizer_H
#define GUARD_le_rendergraph_visualizer_H

#include "le_core.h"

struct le_rendergraph_o;
struct le_rendergraph_visualizer_o;
struct le_image_resource_handle_t;
struct LeUiEvent;

// clang-format off
struct le_rendergraph_visualizer_api {

	struct le_rendergraph_visualizer_interface_t {

		le_rendergraph_visualizer_o *    ( * create                   ) ( );
		void                 			 ( * destroy                  ) ( le_rendergraph_visualizer_o* self );
		void                 			 ( * update                   ) ( le_rendergraph_visualizer_o* self, le_rendergraph_o* rendergraph_to_draw_into, le_image_resource_handle_t* target_image, le_rendergraph_o* rendergraph_to_visualize );
	
		void 							 ( * set_is_active            ) (le_rendergraph_visualizer_o* self, bool is_active);
		bool 							 ( * get_is_active            ) (le_rendergraph_visualizer_o* self);

		void                ( * process_events    ) ( le_rendergraph_visualizer_o* self, LeUiEvent const * events, uint32_t num_events);
        
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
	LeRendergraphVisualizer( bool active_by_default = true )
	    : self( le_rendergraph_visualizer::le_rendergraph_visualizer_i.create() ) {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.set_is_active( self, active_by_default );
	}

	~LeRendergraphVisualizer() {
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.destroy( self );
	}

	void update( le_rendergraph_o* rendergraph_to_draw_into, le_image_resource_handle_t* target_image, le_rendergraph_o* rendergraph_to_visualize = nullptr ) {
		le_rendergraph_o* to_viz = rendergraph_to_visualize ? rendergraph_to_visualize : rendergraph_to_draw_into;
		le_rendergraph_visualizer::le_rendergraph_visualizer_i.update( self, rendergraph_to_draw_into, target_image, to_viz );
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
