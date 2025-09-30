#include "le_core.h"
#include "private/le_renderer/le_rendergraph.h"
#include "le_rendergraph_visualizer.h"
#include "le_debug_print_text.h"
#include "le_renderer.h"
#include "le_renderer.hpp"
#include "le_2d.h"
#include "le_shader_compiler.h"
#include "le_pipeline_builder.h"
#include "glm/glm.hpp"
#include "le_font.h"
#include "le_tracy.h"
#include "le_path.h"
#include "le_ui_event.h"
#include "le_log.h"

#include "private/le_rendergraph_visualizer/views.h"

#include "private/le_renderer/le_resource_handle_t.inl"

#include <algorithm> // for copy_if

static constexpr size_t C_VIEWS_CACHE_CAPACITY = 100;   // Number of RenderpassViews to keep in the cache
static constexpr size_t C_DISABLE_CACHE        = true;  // Number of RenderpassViews to keep in the cache
static constexpr size_t C_RENDERGRAPH_STORE_RINGBUFFER_SIZE = 10;    // number of rendergraphs to store - max

#include "private/le_rendergraph_visualizer/shared_constants.inl"

static auto& logger() {
	static le::Log logger = le::Log( "rendergraph_visualizer" );
	return logger;
}

// ----------------------------------------------------------------------

enum class State : uint32_t {
	eInactive               = 0, // don't do anything
	eLiveVisualizeNoRecord  = 1, // visualize, do not record any data
	eLiveVisualizeAndRecord = 2, // visualize current data
	eRecordOnly,                 // record only
	eVisualizeRecorded,          // visualize recorded data
};

enum class UI_CAPTURE_STATE_BIT : uint32_t {
	eNone     = 0,
	eMouse    = 1,
	eKeyboard = uint32_t( 1 ) << 1,
};

enum class IO_STATES : uint32_t {
	eInactive = 0,
	eGrabbing = 1, // mouse has grabbed hold of canvas
};

struct io_state_t {
	glm::vec2 last_cursor_pos;      // relative to canvas_blit_pos
	uint32_t  mouse_button_pressed; // bitfield for mouse buttons
	IO_STATES state = IO_STATES::eInactive;
	bool      should_zoom = false;
};

struct le_rendergraph_visualizer_o {
	// members
	le_image_resource_handle canvas_image;   // the canvas onto which we draw the visualization.
	le_resource_info_t       canvas_image_info; // resource image information
	le_texture_handle        canvas_texture;    // the texture which we use to sample the canvas
	le::Font                 font = { "./resources/fonts/IBMPlexSans-Regular.otf", 8 };
	Le2D                     ctx_2d; // 2d drawing context

	uint32_t   ui_capture_state = 0;
	io_state_t io_state         = {};
	float      ui_zoom_level_delta = 0; // relative zoom level, 0 means 1:1

	std::unordered_map<uint64_t, RenderPassView*> rp;

	LeTransform2D artboard_to_screen; /// artboard-to-screen transform for drawing the rendergraph - this controls zoom and positioning of the diagram on screen
	LeTransform2D artboard_to_screen_initial; // initial transform for centering the artboard on the screen
	bool          is_artboard_to_screen_initial_set = false;

	glm::vec2 canvas_extents;  // dimensions of the visualization canvas
	glm::vec2 canvas_blit_pos; // where the visualization gets rendered on the final image

	State current_state = State::eLiveVisualizeNoRecord;

	std::array<size_t, C_RENDERGRAPH_STORE_RINGBUFFER_SIZE>            rendergraph_frame_number = {}; // recorded frame number of the corresponding rendergraph in rendergraph_store
	std::array<le_rendergraph_o*, C_RENDERGRAPH_STORE_RINGBUFFER_SIZE> rendergraph_store     = {}; // number of rendergraphs to store - in case we wanted to do time-travelling
	size_t                                                             rendergraph_store_pos = 0;  // one-past position of last write into the rendergraph ring buffer (wraps around C_RENDERGRAPH_STORE_RINGBUFFER_SIZE)

	size_t recorded_frames_count                       = 0; // total number of recorded frames, monotonically increasing
	size_t recorded_frame_displayed_frame_index_offset = 0; // only meaningful if state is VisualizeRecorded, and then this is a negative offset to the rendergraph_store_pos

	uint8_t epoch; // update count, used for cache
};

// ----------------------------------------------------------------------

static le_rendergraph_visualizer_o* le_rendergraph_visualizer_create() {
	auto self = new le_rendergraph_visualizer_o();

	self->canvas_image = LE_IMG_RESOURCE( "visualizer_output_image" );

	self->canvas_blit_pos = { 50, 50 };
	self->canvas_extents  = { 1080 - 100.f, 400.f };

	self->artboard_to_screen = self->artboard_to_screen_initial = {};

	self->canvas_image_info = le::ImageInfoBuilder().build();

	return self;
}

// ----------------------------------------------------------------------

static void le_rendergraph_visualizer_destroy( le_rendergraph_visualizer_o *self ) {

	for ( auto& v : self->rp ) {
		delete v.second;
	}
	self->rp.clear();

	for ( auto& r : self->rendergraph_store ) {
		if ( r ) {
			le_renderer_api_i->le_rendergraph_i.destroy( r );
		}
	}

	delete self;
}

// ----------------------------------------------------------------------

static void list_renderpasses_as_text( le_rendergraph_o* rp_src ) {
	le::DebugPrint::setBgColour( { 0.f, 0.f, 0.f, 1.f } );
	le::DebugPrint::setColour( { 1.f, 1, 1, 1 } );
	le::DebugPrint( "|Renderpasses:\n" );
	le::DebugPrint( "|-------------\n" );

	for ( auto const& p : rp_src->passes ) {
		p->is_contributing ? le::DebugPrint::setColour( { 1, 1, 1, 1 } ) : le::DebugPrint::setColour( { .60, .60, .7, 1 } );
		le::DebugPrint( "|+\t%s%s\n", p->debug_name.c_str(), p->is_contributing ? " " : " -- (optimised away) " );
	}
}

// ----------------------------------------------------------------------

static void le_rendergraph_visualizer_update_renderpass_view_cache( le_rendergraph_visualizer_o* self, le_rendergraph_o const* rp_src, std::vector<uint64_t>& renderpass_hashes ) {
	for ( auto const& p : rp_src->passes ) {

		uint64_t rp_hash = le_renderer_api_i->le_renderpass_i.get_hash( p );

		// we store the renderpass hash locally, so that we don't have to re-calculate the hash again later
		renderpass_hashes.push_back( rp_hash );

		auto [ it, did_emplace ] = self->rp.emplace( rp_hash, nullptr );

		if ( did_emplace ) {
			it->second = new RenderPassView( &self->font, p, self->epoch );
		} else {
			// Mark this RenderpassView as being used in this epoch -
			// this means that cache control should not delete it yet...
			it->second->epoch = self->epoch;
		}
	}
}

// ----------------------------------------------------------------------

static void rendergraph_visualizer_renderpass_view_cache_maintain( le_rendergraph_visualizer_o*& self ) {

	// This maintains renderpass view cache -- you need to use all resources and their
	// properties as the control for whether a renderpass is the same -- not just the name.
	//
	// You should then also kill any elements from the cache that have not been used/drawn for
	// the last 10 epochs.

	size_t num_elements_in_cache = self->rp.size();

	if ( num_elements_in_cache > C_VIEWS_CACHE_CAPACITY || C_DISABLE_CACHE ) {
		for ( auto it = self->rp.begin(); it != self->rp.end(); ) {
			uint8_t age = uint32_t( self->epoch - it->second->epoch );
			if ( C_DISABLE_CACHE || age > 3 ) {
				// If an element is older than three epochs, we may evict it from the cache
				// immediately if we need space.
				delete it->second;         // delete the cached RenderpassView
				it = self->rp.erase( it ); // delete the cache entry
				if ( --num_elements_in_cache <= C_VIEWS_CACHE_CAPACITY && !C_DISABLE_CACHE ) {
					break;
				} else {
					continue;
				}
			}
			it++;
		}
	}

	self->epoch++;
}

// ----------------------------------------------------------------------

static void draw_visualizer( le_rendergraph_visualizer_o*& self, le_rendergraph_o* rendergraph, le_image_resource_handle target_image, le_rendergraph_o* rp_src ) {

	// list_renderpasses_as_text( rp_src );

	le::Encoder2D encoder_renderpass_views{};

	self->canvas_blit_pos = { 50, 50 };
	self->canvas_extents  = { 1080 - 100, 1080 - 100 };

	//
	struct connection_t {
		int16_t            renderpass_idx_from; // renderpass that provides resource used for connection
		int16_t            renderpass_idx_to;   // destination; renderpass that uses the resource in this connection
		int16_t            extra_lane;          // which extra lane to use to route this connection if we can't route directly
		int16_t            resource_idx;        // index in list of resources of the destination renderpass
		le_resource_handle resource;
	};

	std::vector<connection_t> connections;

	std::vector<connection_t> active_connections; // any connection that, on an extra lane, reaches forward

	// We keep track of extra lanes that connect cards that are not immediate neighbours.
	// - There can only be one connection per lane.
	// - Lanes should be re-used as much as possible to avoid vertical spread of the diagram.
	//
	// index of this vector corresponds to lane index
	// value in this vector corresponds to source renderpass id
	std::vector<int16_t> occupied_lanes;

	{
		// ---------- Build a vector of connections ----------
		//
		// Connections go from target (right) forward to their original source. Only one connection may
		// go from a target to an origin, but an origin may have multiple (outgoing) connections.
		//
		//

		ZoneScoped;
		auto get_resource_idx = []( le_rendergraph_o const* rg, le_resource_handle const r ) -> uint32_t {
			// Will find resource in linear time; the more resources we have, the more time it
			// may take; We could cache this locally; But we're effectively comparing pointers;
			// so this should be plenty fast.
			uint32_t i = 0;
			for ( ; i != rg->unique_resources.size(); i++ ) {
				if ( rg->unique_resources[ i ] == r ) {
					break;
				}
			}
			return i;
		};

		uint32_t rp_src_unique_resources_size = rp_src->unique_resources.size();

		for ( int16_t i = rp_src->passes.size() - 1; i >= 0; i-- ) {
			auto& p = *( rp_src->passes[ i ] );
			auto& n = rp_src->nodes[ i ];

			if ( false == p.is_contributing ) {
				continue;
			}

			// Mark any lanes that have the current renderpass as its source as un-occupied.
			// because their lanes start from the curent renderpass.

			for ( auto& l : occupied_lanes ) {
				if ( l >= i ) {
					l = -1;
				}
			}

			// ----------| invariant: this pass contributes

			connection_t c{
			    .renderpass_idx_from = -1,
			    .renderpass_idx_to   = i,
			};

			int16_t resource_idx = 0;
			int16_t extra_lanes  = 0;
			for ( auto& r : p.resources ) {

				c.resource_idx = resource_idx;
				c.resource     = r;
				// we only care if the resource is a read resource
				uint32_t res_idx = get_resource_idx( rp_src, r );
				if ( res_idx == rp_src_unique_resources_size ) {
					// Resource could not be found for some reason
					// this should not happen, but if it happens,
					// we ignore this resource.
					continue;
				}
				// ----------| Invariant: resource was found

				if ( n.reads.test( res_idx ) ) {
					// we have a read -- now we need to find any previous
					// passes that might have written to this

					for ( int j = i - 1; j >= 0; j-- ) {
						auto& n_dest = rp_src->nodes[ j ];

						if ( n_dest.explicit_writes.test( res_idx ) ) {
							// we have found our connecsion
							c.renderpass_idx_from = j;
							if ( j == i - 1 ) {
								// Connection goes directly to the left neighbour --
								// We must not use an extra lane, we flag this by
								// setting extra_lane to -1.
								c.extra_lane = -1;
							} else {

								int16_t lane_idx           = 0;
								int16_t occupied_lanes_end = occupied_lanes.size();
								for ( ; lane_idx != occupied_lanes_end; lane_idx++ ) {
									// If lane is unoccupied, then select it
									if ( -1 == occupied_lanes[ lane_idx ] ) {
										occupied_lanes[ lane_idx ] = c.renderpass_idx_from;
										break;
									}
								}
								if ( lane_idx == occupied_lanes_end ) {
									// no unoccupied lane found, we must insert a new lane.
									occupied_lanes.push_back( c.renderpass_idx_from );
								}

								// Since we use 0 as a signal to not use an extra
								// lane, we must add 1 to indicate an extra lane
								// is being used.
								c.extra_lane = lane_idx;
							}
							connections.push_back( c );
							break;
						}
					}
				}
				resource_idx++;
			}
		}
	}

	// DE-SPAGHETTIFICATION
	//
	// Re-assign lanes for connectors to minimize path crossings.
	//
	// We want to have resources that come at the top use the highest lanes, and resources that
	// are listed further down should use the lower lanes
	// but this should only affect connections that use the extra lanes.
	// but for this, we need to exclude any connections that are using lane 0.
	//
	// This looks and feels a bit hacky, and I'm sure this can be made better or more performant
	// but for now, this will have to do. If it ever becomes too slow, we can disable this step.
	//
	if ( true ) {
		auto c_end = connections.end();
		for ( auto it = connections.begin(); it != c_end; ) {

			// we must find how long the current group is
			// the current group is all connectors that have the
			// same renderpass_idx_to.

			auto     it_group_end = it;
			uint32_t idx_to       = it->renderpass_idx_to;

			while ( it_group_end != c_end && it_group_end->renderpass_idx_to == idx_to ) {
				it_group_end++;
			}

			// invariant - it_group_end now at one plus last element of current group

			std::sort( it, it_group_end, []( connection_t const& lhs, connection_t const& rhs ) {
				return lhs.extra_lane < rhs.extra_lane;
			} );

			// now get all lanes, and then sort the lanes

			// advance it so that it only covers items that have extra lane < 0
			while ( it != it_group_end && it->extra_lane == -1 ) {
				it++;
			}
			std::vector<uint32_t> lanes;
			lanes.reserve( it_group_end - it );

			for ( auto it_tmp = it; it_tmp != it_group_end; it_tmp++ ) {
				lanes.push_back( it_tmp->extra_lane );
			}

			for ( int i = lanes.size() - 1; it != it_group_end; it++, i-- ) {
				it->extra_lane = lanes[ i ];
			}

			it = it_group_end;
		}
	}

	std::vector<uint64_t> renderpass_hashes;
	le_rendergraph_visualizer_update_renderpass_view_cache( self, rp_src, renderpass_hashes );

	// ---------- Draw Renderpass Views ----------
	//
	// We first draw Renderpass Views, so that we can find out the total dimensions of our
	// diagram, and where to place connection points.
	//
	std::vector<LeTransform2D>   per_pass_transforms;
	std::vector<RenderPassView*> rp_views;

	glm::vec2 right_most_point = {};
	{
		ZoneScoped;
		int           i = 0;
		LeTransform2D t = {};
		for ( auto const& p : rp_src->passes ) {
			uint64_t pass_id = renderpass_hashes[ i ]; // this was updated when updating the cache
			auto&    pass    = self->rp.at( pass_id );
			pass->draw( encoder_renderpass_views, t );
			per_pass_transforms.push_back( t );
			rp_views.push_back( pass );
			LeTransform2D t_local{ .translation = { pass->get_right_most_x() + h_spacing, 0 } };
			t = t * t_local;
			i++;
		}

		right_most_point = t.translation;
	}

	// ---------- Draw Connections ----------
	//
	// Encode connections draw instructions into a separate encoder. We then
	// append the renderpass views onto this encoder, which means that even though
	// we encode connections after we encode the renderpass views in the end, connections
	// will get drawn before the renderpass views.
	//
	//

	le::Encoder2D encoder_connections{};

	for ( auto const& c : connections ) {

		// first, just draw circles over the given points of connection - this is so
		// that we can tell whether our geometry is correct.

		auto from_view = rp_views[ c.renderpass_idx_from ];
		auto to_view   = rp_views[ c.renderpass_idx_to ];

		glm::vec2 from_port = from_view->getPortForResource( c.resource, false );
		glm::vec2 to_port   = to_view->getPortForResource( c.resource, true );

		LeTransform2D from_transform = { .translation = ( -1 == c.extra_lane ) ? from_port : glm::vec2{ from_port.x + h_spacing, -( c.extra_lane + 1 ) * c_line_height } };
		LeTransform2D to_transform   = { .translation = ( -1 == c.extra_lane ) ? to_port : glm::vec2{ to_port.x - h_spacing, -( c.extra_lane + 1 ) * c_line_height } };

		from_transform = per_pass_transforms[ c.renderpass_idx_from ] * from_transform;
		to_transform   = per_pass_transforms[ c.renderpass_idx_to ] * to_transform;

		// Now, if i want to draw a connection between the two positions,
		// how would i do this?

		// We need to draw either in from_transform space or to_transform space.
		// we choose from_space; and therefore we must transform all points
		// to be relative to this space.

		auto pt_in_from_transform_space = ( from_transform.inverse() * to_transform );

		// these are only needed if we have extra lanes
		auto c0 = ( from_transform.inverse() * per_pass_transforms[ c.renderpass_idx_from ] * LeTransform2D{ .translation = from_port } ).translation;
		auto c1 = ( from_transform.inverse() * per_pass_transforms[ c.renderpass_idx_to ] * LeTransform2D{ .translation = to_port } ).translation;

		glm::vec2 to_pos = pt_in_from_transform_space.translation;

		for ( int i = 0; i != 2; i++ ) {

			float bendiness = .55f;

			if ( c.extra_lane != -1 ) {
				encoder_connections
				    .transform( from_transform )
				    .colour_rgba( i == 0 ? c_colour_connector_outline : c_colour_connector_fill )
				    .path_begin( { .width = i == 0 ? 6.f : 4.f } )
				    .move_to( c0 )
				    .cubic_to( c0 + ( glm::vec2{} - c0 ) * glm::vec2{ bendiness, 0. }, glm::vec2{} - ( glm::vec2{} - c0 ) * glm::vec2{ bendiness, 0. }, glm::vec2{} )
				    .line_to( to_pos )
				    .cubic_to( to_pos + ( c1 - to_pos ) * glm::vec2{ bendiness, 0. }, c1 - ( c1 - to_pos ) * glm::vec2{ bendiness, 0. }, c1 )
				    .path_end();
			} else {

				encoder_connections
				    .transform( from_transform )
				    .colour_rgba( i == 0 ? c_colour_connector_outline : c_colour_connector_fill )
				    .path_begin( { .width = i == 0 ? 6.f : 4.f } )
				    //.colour_abgr( 0xff5f711e )
				    //.path_begin( { .width = 5.f } )
				    .move_to( { 0, 0 } )
				    .cubic_to( { to_pos.x * bendiness, 0 }, { to_pos.x * ( 1.f - bendiness ), to_pos.y }, to_pos )
				    .path_end();
			}
		}
	}

	// ---------- Draw Cached RenderpassViews ---------
	//
	//

	encoder_connections.append( encoder_renderpass_views );

	self->canvas_image_info.image.extent.width  = self->canvas_extents.x;
	self->canvas_image_info.image.extent.height = self->canvas_extents.y;

	le::Encoder2D encoder_artboard{};

	if ( false == self->is_artboard_to_screen_initial_set ) {
		//
		float z                                 = self->canvas_extents.x / right_most_point.x;
		self->artboard_to_screen_initial        = { .transform = { z, 0, 0, z }, .translation = glm::vec2( ( h_spacing * 0.5 - c_padding_left_right ) * z, float( self->canvas_extents.y ) * 0.5f ) };
		self->artboard_to_screen                = self->artboard_to_screen_initial;
		self->is_artboard_to_screen_initial_set = true;
	}

	encoder_artboard.append( encoder_connections, self->artboard_to_screen );

	constexpr bool USE_ARTBOARD_MAGNIFIER = true;

	if ( USE_ARTBOARD_MAGNIFIER && self->io_state.should_zoom ) {
		LeTransform2D artboard_to_magnified_screen;
		float         zoom           = 2;
		LeTransform2D zoom_transform = { .transform = { 1.f + zoom, 0, 0, 1.f + zoom } };

		// i need to transform from mouse space into into artboard space
		// mouse space is where the mouse is at the centre of all things
		// artboard space is where the artboard is centred.

		LeTransform2D mouse_to_screen{ .translation = { self->io_state.last_cursor_pos.x, self->io_state.last_cursor_pos.y } };
		LeTransform2D mouse_space_to_artboard = self->artboard_to_screen.inverse() * mouse_to_screen; // screen_to_artboard <- mouse-to-screen

		// This applies zooms around where the mouse is:
		// Read this right-to-left.
		// 1) First we center the art board to where the mouse is - mouse_space_to_artboard.inverse()
		// 2) Then we apply the zoom
		// Then we undo the centering
		// Then we move from artboard to screen.

		artboard_to_magnified_screen = self->artboard_to_screen * mouse_space_to_artboard * zoom_transform * mouse_space_to_artboard.inverse();

		// draw magnifier glass clip circle

		encoder_artboard
		    .transform( self->artboard_to_screen ) // apply canvas to screen transform
		    .path_begin( le_2d::FillStyle::NonZero )
		    .circle( { mouse_space_to_artboard.translation[ 0 ], mouse_space_to_artboard.translation[ 1 ] }, 300 ) // we draw this in canvas space
		    .path_end()
		    .begin_clip( le_2d::BlendMode{}, 1.f )

		    .colour_abgr( 0xffffffff )
		    .path_begin( le_2d::FillStyle::NonZero )
		    .circle( { mouse_space_to_artboard.translation[ 0 ], mouse_space_to_artboard.translation[ 1 ] }, 300 ) // we draw this in canvas space
		    .path_end();

		encoder_artboard.append( encoder_connections, artboard_to_magnified_screen );
		encoder_artboard.end_clip();
	}

	encoder_artboard.transform();
	for ( int i = 0; i != C_RENDERGRAPH_STORE_RINGBUFFER_SIZE; i++ ) {

		for ( int j = 0; j != 2; j++ ) {
			uint32_t outline_colour = 0x0;
			uint32_t fill_colour    = 0x0;

			if ( i == 0 && self->current_state == State::eLiveVisualizeAndRecord ) {
				// the first symbol is the record button in case we are recording
				fill_colour    = c_colour_red;
				outline_colour = c_colour_pass_video;
			} else {
				outline_colour = c_colour_connector_fill;

				if ( self->recorded_frame_displayed_frame_index_offset == i ) {
					fill_colour = c_colour_connector_fill;
				} else {
					fill_colour = c_colour_card_bg;
				}
				// encoder_artboard.colour_rgba(fill_colour);
			}

			encoder_artboard.colour_rgba( ( j == 0 ) ? fill_colour : outline_colour );

			if ( j == 0 ) {
				encoder_artboard.path_begin( le_2d::FillStyle::NonZero )
				    .circle( { self->canvas_extents.x - 15 - ( i * 30 ), self->canvas_extents.y - 15 }, 10 );
			} else {

				encoder_artboard.path_begin( { .width = 2.f } )
				    .circle( { self->canvas_extents.x - 15 - ( i * 30 ), self->canvas_extents.y - 15 }, 9 );
			}

			encoder_artboard
			    .path_end();
		}
		if ( self->current_state != State::eVisualizeRecorded ) {
			break;
		}
	}

	self->ctx_2d.update( rendergraph, encoder_artboard, self->canvas_image, &self->canvas_image_info, le_2d_colour( 255, 255, 255, 128 ).to_premult_rgba_u32() );

	// Now, we need to draw the image into the output image -- that way we can be sure that it will be visible
	//
	// All this is just to draw the image created via the 2d context into the final swapchain image:

	auto draw_visuals =
	    le::RenderPass( "draw_visualizer" )
	        .addColorAttachment( target_image, le::ImageAttachmentInfoBuilder().setLoadOp( le::AttachmentLoadOp::eLoad ).build() )
	        .sampleTexture( self->canvas_texture, self->canvas_image )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
		        auto                self = static_cast<le_rendergraph_visualizer_o*>( user_data );
		        le::GraphicsEncoder encoder{ encoder_ };

		        le::Extent2D extents = encoder.getRenderpassExtent();

		        // Blit visualization onto the background image.

		        static auto pipeline_draw_brush =
		            LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		                .addShaderStage(
		                    LeShaderModuleBuilder( encoder.getPipelineManager() )
		                        .setSourceFilePath( "./local_resources/rendergraph_visualizer/visualizer_blit.vert" )
		                        .setShaderStage( le::ShaderStage::eVertex )
		                        .build() )
		                .addShaderStage(
		                    LeShaderModuleBuilder( encoder.getPipelineManager() )
		                        .setSourceFilePath( "./local_resources/rendergraph_visualizer/visualizer_blit.frag" )
		                        .setShaderStage( le::ShaderStage::eFragment )
		                        .build() )
		                .withAttachmentBlendState()
		                .usePreset( le::AttachmentBlendPreset::ePremultipliedAlpha )
		                .end()
		                .build();

		        struct ShaderParams {
			        glm::vec2 u_resolution;
			        glm::vec2 u_quad_position;
			        glm::vec2 u_quad_extents;
		        };

		        ShaderParams params{};

		        params.u_resolution = {
		            extents.width,
		            extents.height,
		        };

		        params.u_quad_position = self->canvas_blit_pos;

		        params.u_quad_extents = {
		            self->canvas_image_info.image.extent.width,
		            self->canvas_image_info.image.extent.height,
		        };

		        static const float vertexPositions[ 4 ][ 3 ] = {
		            { -0.5, 0.5, 0 },
		            { -0.5, -0.5, 0 },
		            { 0.5, -0.5, 0 },
		            { 0.5, 0.5, 0 },
		        };

		        static const uint16_t indices[] = {
		            0, 1, 2,
		            0, 2, 3, //
		        };

		        encoder
		            .bindGraphicsPipeline( pipeline_draw_brush )
		            .setVertexData( vertexPositions, sizeof( vertexPositions ), 0 )
		            .setIndexData( indices, sizeof( indices ), le::IndexType::eUint16 )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), self->canvas_texture ) //
		            ;

		        encoder
		            .setPushConstantData( &params, sizeof( ShaderParams ) )
		            .drawIndexed( 6 ) //
		            ;
	        } );

	le_renderer_api_i->le_rendergraph_i.add_renderpass( rendergraph, draw_visuals );
}

// ----------------------------------------------------------------------

/// `target_image` tells us where to draw the visualization into.
/// Usually you would want to set this to the current swapchain image.
static void le_rendergraph_visualizer_update( le_rendergraph_visualizer_o* self, le_rendergraph_o* rendergraph, le_image_resource_handle target_image ) {

	ZoneScoped;

	if ( self->current_state != State::eInactive ) {
		// ----------| invariant: visualizer is active

		le_rendergraph_o* rp_src = rendergraph;

		// We must first evaluate the rendergraph by calling its setup callbacks.
		//
		// This is so that all resources which are dynamically declared become
		// available to the rendergraph.
		//
		// Note that this means that any subsequent
		// calls to setup_passes will have no effects for passes whose callback
		// functions have been called at this point. Callbacks are one-shot. This
		// is so that we can guarantee that any side-effects of calling setup()
		// only happens once, and that the rendergraph can not change if
		// `setup_passes` is called on it more than once (first call is canonical).
		le_renderer_api_i->le_rendergraph_private_i.setup_passes( rp_src );
		le_renderer_api_i->le_rendergraph_private_i.build( rp_src, 0 );

		if ( self->current_state == State::eLiveVisualizeAndRecord || self->current_state == State::eRecordOnly ) {

			auto& rg_store = self->rendergraph_store[ self->rendergraph_store_pos ];

			if ( nullptr != rg_store ) {
				// we must evict the last element from the ring buffer
				le_renderer_api_i->le_rendergraph_i.destroy( rg_store );
				rg_store = nullptr;
			}

			// store a clone of the renderpass into our ring buffer
			rg_store = le_renderer_api_i->le_rendergraph_private_i.clone( rp_src );
			// store the current frame number into its corresponding position
			self->rendergraph_frame_number[ self->rendergraph_store_pos ] = ++self->recorded_frames_count;

			self->rendergraph_store_pos = ( self->rendergraph_store_pos + 1 ) % C_RENDERGRAPH_STORE_RINGBUFFER_SIZE;
		}

		if ( self->current_state == State::eVisualizeRecorded ) {
			size_t            seek_frame_idx = ( self->rendergraph_store_pos + self->recorded_frame_displayed_frame_index_offset + C_RENDERGRAPH_STORE_RINGBUFFER_SIZE ) % C_RENDERGRAPH_STORE_RINGBUFFER_SIZE;
			le_rendergraph_o* rg             = self->rendergraph_store[ seek_frame_idx ];
			if ( nullptr != rg ) {
				rp_src = rg;
			}
		}

		if ( self->current_state == State::eLiveVisualizeAndRecord ||
		     self->current_state == State::eLiveVisualizeNoRecord ||
		     self->current_state == State::eVisualizeRecorded ) {

			draw_visualizer( self, rendergraph, target_image, rp_src );
		}

	} // end if current_state != eInactive

	// we maintain the cache whether the visualizer is active or not
	rendergraph_visualizer_renderpass_view_cache_maintain( self );
}

// ----------------------------------------------------------------------

static void le_rendergraph_visualizer_process_events( le_rendergraph_visualizer_o* self, LeUiEvent const* events, uint32_t numEvents ) {

	// CONSIDER: should this be written a state machine?

	if ( self->current_state == State::eInactive ) {
		return;
	}
	// ----------| invariant: rendergraph visualizer is active

	LeUiEvent const* const events_end = events + numEvents; // end iterator

	auto is_inside_rect = []( glm::vec2 pt, glm::vec2 bottom_right ) {
		return ( pt.x < bottom_right.x ) &&
		       ( pt.x > 0 ) &&
		       ( pt.y > 0 ) &&
		       ( pt.y < bottom_right.y );
	};

	glm::vec2 cursor_delta = {};

	for ( LeUiEvent const* event = events; event != events_end; event++ ) {
		// Process events in sequence

		switch ( event->event ) {
		case LeUiEvent::Type::eKey: {
			auto& e = event->key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {

				switch ( e.key ) {
				case ( LeUiEvent::NamedKey::eR ): {
					// Reset all view transforms
					self->is_artboard_to_screen_initial_set = false;
					self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eKeyboard );
					break;
				}
				case ( LeUiEvent::NamedKey::eZ ): {
					self->io_state.should_zoom ^= true;
					self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eKeyboard );
					break;
				}
				case ( LeUiEvent::NamedKey::eSpace ): {
					if ( self->current_state == State::eLiveVisualizeAndRecord ) {
						self->current_state                               = State::eVisualizeRecorded;
						self->recorded_frame_displayed_frame_index_offset = 0;
					} else if ( self->current_state == State::eVisualizeRecorded ) {
						self->current_state = State::eLiveVisualizeAndRecord;
					}
					self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eKeyboard );
					break;
				}
				case ( LeUiEvent::NamedKey::eLeft ): {
					if ( self->current_state == State::eVisualizeRecorded ) {
						// we are navigating recorded frames.
						if ( self->recorded_frame_displayed_frame_index_offset < C_RENDERGRAPH_STORE_RINGBUFFER_SIZE - 1 &&
						     self->recorded_frame_displayed_frame_index_offset < self->recorded_frames_count - 1 ) {
							self->recorded_frame_displayed_frame_index_offset++;
						}
					}
					self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eKeyboard );
					break;
				}
				case ( LeUiEvent::NamedKey::eRight ): {
					if ( self->current_state == State::eVisualizeRecorded ) {
						// we are navigating recorded frames.
						if ( self->recorded_frame_displayed_frame_index_offset > 0 ) {
							self->recorded_frame_displayed_frame_index_offset--;
						}
					}
					self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eKeyboard );
					break;
				}
				default:
					break;
				}
			}
			break;
		}
		case LeUiEvent::Type::eCursorPosition: {
			auto& e = event->cursorPosition;

			glm::vec2 cursor_pos        = glm::vec2{ float( e.x ), float( e.y ) };
			glm::vec2 canvas_cursor_pos = cursor_pos - self->canvas_blit_pos;

			if ( self->io_state.state == IO_STATES::eGrabbing ) {
				cursor_delta += canvas_cursor_pos - self->io_state.last_cursor_pos;
				self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eMouse );
			}

			if ( is_inside_rect( canvas_cursor_pos, self->canvas_extents ) ) {
				self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eMouse );
				// logger().info( "inside" );
			} else {
				// logger().info( "outside" );
			}

			self->io_state.last_cursor_pos = canvas_cursor_pos;
		} break;
		case LeUiEvent::Type::eCursorEnter: {
			auto& e = event->cursorEnter;
		} break;
		case LeUiEvent::Type::eMouseButton: {
			auto& e = event->mouseButton;

			if ( self->io_state.state == IO_STATES::eInactive && // if state is inactive
			     is_inside_rect( self->io_state.last_cursor_pos, self->canvas_extents ) &&
			     !( self->io_state.mouse_button_pressed & ( uint32_t( 1 ) << 0 ) ) &&                            // first button not yet pressed
			     ( e.button == 0 ) &&                                                                            // button that is being pressed it button 0 (primary mouse)
			     ( e.action == LeUiEvent::ButtonAction::ePress || e.action == LeUiEvent::ButtonAction::eRepeat ) // button is actually being pressed
			) {
				// logger().info( "button pressed" );
				self->io_state.state = IO_STATES::eGrabbing;
				self->io_state.mouse_button_pressed |= uint32_t( 1 ) << 0;
				self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eMouse );
			} else if ( self->io_state.state == IO_STATES::eGrabbing &&                     // if state is inactive
			            ( self->io_state.mouse_button_pressed & ( uint32_t( 1 ) << 0 ) ) && // first button is currently pressed
			            ( e.button == 0 ) &&                                                // button that is being pressed it button 0 (primary mouse)
			            ( e.action == LeUiEvent::ButtonAction::eRelease )                   // button is actually being released
			) {
				// logger().info( "grab complete" );
				self->io_state.state = IO_STATES::eInactive;
				self->io_state.mouse_button_pressed &= ~( uint32_t( 1 ) << 0 );
				self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eMouse );
			}

		} break;
		case LeUiEvent::Type::eScroll: {
			auto& e = event->scroll;
			if ( is_inside_rect( self->io_state.last_cursor_pos, self->canvas_extents ) ) {
				self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eMouse );

				self->ui_zoom_level_delta = e.y_offset * 0.125 * 0.25;
				// logger().info( "scroll: %f", self->ui_zoom_level_delta );
			}
		} break;
		default:
			break;
		} // end switch event->event
	}

	// Apply view transforms ------------------------------------------------------------

	if ( self->io_state.state == IO_STATES::eGrabbing ) {

		// signal that the mouse has been captured
		// self->ui_capture_state |= uint32_t( UI_CAPTURE_STATE_BIT::eMouse );

		LeTransform2D grab_transform = { .translation = { cursor_delta.x, cursor_delta.y } };

		self->artboard_to_screen = grab_transform * self->artboard_to_screen;

		// logger().info( "grab delta: %f,%f", cursor_delta.x, cursor_delta.y );
	}

	if ( fabsf( self->ui_zoom_level_delta ) > std::numeric_limits<float>::epsilon() ) {

		float       zoom             = self->ui_zoom_level_delta;
		LeTransform2D zoom_transform   = { .transform = { 1.f + zoom, 0, 0, 1.f + zoom } };

		// i need to transform from mouse space into into artboard space
		// mouse space is where the mouse is at the centre of all things
		// artboard space is where the artboard is centred.

		LeTransform2D mouse_to_screen{ .translation = { self->io_state.last_cursor_pos.x, self->io_state.last_cursor_pos.y } };
		LeTransform2D mouse_space_to_artboard = self->artboard_to_screen.inverse() * mouse_to_screen; // screen_to_artboard <- mouse-to-screen

		// This applies zooms around where the mouse is:
		// Read this right-to-left.
		// 1) First we center the art board to where the mouse is - mouse_space_to_artboard.inverse()
		// 2) Then we apply the zoom
		// Then we undo the centering
		// Then we move from artboard to screen.

		self->artboard_to_screen = self->artboard_to_screen * mouse_space_to_artboard * zoom_transform * mouse_space_to_artboard.inverse();

		self->ui_zoom_level_delta = 0;
	}

	// self->transform = {};
}

// ----------------------------------------------------------------------

static void le_rendergraph_visualizer_process_and_filter_events( le_rendergraph_visualizer_o* self, LeUiEvent* events, uint32_t* num_events ) {

	if ( self->current_state == State::eInactive || self->current_state == State::eRecordOnly ) {
		return;
	}
	// ----------| invariant: rendergraph visualizer is active

	if ( nullptr == num_events || *num_events == 0 ) {
		return;
	}
	// ----------| invariant: num_events > 0

	self->ui_capture_state = 0;

	le_rendergraph_visualizer_process_events( self, events, *num_events );

	uint32_t ioFilterFlags = 0;

	if ( self->ui_capture_state & uint32_t( UI_CAPTURE_STATE_BIT::eMouse ) ) {
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eCursorEnter );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eCursorPosition );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eScroll );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eMouseButton );
	}

	if ( self->ui_capture_state & uint32_t( UI_CAPTURE_STATE_BIT::eKeyboard ) ) {
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eKey );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eCharacter );
	}

	// Filter out events which have been captured as these
	// should not further propagate to other ui elements.
	//
	// We do this by copying only events ones which pass our filter in a new
	// events queue, which we then pass on for further processing.
	//
	std::vector<LeUiEvent> ev{};
	std::copy_if( events, events + *num_events, std::back_inserter( ev ), [ &ioFilterFlags ]( LeUiEvent const& e ) -> bool {
		// This will only return true if none of the filter flags were found
		// in the current event type.
		return !( uint32_t( e.event ) & ioFilterFlags );
	} );
	memcpy( events, ev.data(), sizeof( LeUiEvent ) * ev.size() );

	*num_events = ev.size();
}

// ----------------------------------------------------------------------

static bool le_rendergraph_visualizer_get_is_active( le_rendergraph_visualizer_o* self ) {
	return self->current_state != State::eInactive;
};

// ----------------------------------------------------------------------

static void le_rendergraph_visualizer_set_is_active( le_rendergraph_visualizer_o* self, bool is_active ) {

	if ( is_active ) {
		self->current_state = State::eLiveVisualizeAndRecord;
	} else {
		self->current_state = State::eInactive;
	}
};

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_rendergraph_visualizer, api ) {
	auto &le_rendergraph_visualizer_i = static_cast<le_rendergraph_visualizer_api *>( api )->le_rendergraph_visualizer_i;

	le_rendergraph_visualizer_i.create                    = le_rendergraph_visualizer_create;
	le_rendergraph_visualizer_i.destroy                   = le_rendergraph_visualizer_destroy;
	le_rendergraph_visualizer_i.update                    = le_rendergraph_visualizer_update;
	le_rendergraph_visualizer_i.process_and_filter_events = le_rendergraph_visualizer_process_and_filter_events;
	le_rendergraph_visualizer_i.process_events            = le_rendergraph_visualizer_process_events;
	le_rendergraph_visualizer_i.get_is_active             = le_rendergraph_visualizer_get_is_active;
	le_rendergraph_visualizer_i.set_is_active             = le_rendergraph_visualizer_set_is_active;

#ifdef LE_LOAD_TRACING_LIBRARY
	LE_LOAD_TRACING_LIBRARY;
#endif
}
