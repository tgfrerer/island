#include "video_player_example_app.h"

#include "le_window.h"
#include "le_renderer.hpp"

#include "le_pipeline_builder.h"
#include "le_ui_event.h"
#include "le_video_decoder.h"
#include "le_camera.h"
#include "le_ui_event.h"
#include "le_timebase.h"
#include "le_imgui.h"
#include "3rdparty/imgui/imgui.h"
#include "le_log.h"

#include <chrono>
#include "private/le_timebase/le_timebase_ticks_type.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct video_and_texture_t {
	le::VideoPlayer*  video_player;
	le_texture_handle video_texture_handle;
};

struct video_player_example_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;
	glm::vec2    mouse_pos;

	std::vector<video_and_texture_t> video_players;

	LeCamera           camera;
	LeCameraController cameraController;
	LeTimebase         timebase;
	le_imgui_o*        gui;
};

// ----------------------------------------------------------------------

typedef video_player_example_app_o app_o;

LE_SETTING( const bool, LE_SETTING_SHOULD_USE_VALIDATION_LAYERS, true );
LE_SETTING( const bool, LE_SETTING_SHOULD_USE_QUERIES, true );

// Set USE_FIXED_TIME_INTERVAL to true to step a fixed time interval every update.
constexpr static bool USE_FIXED_TIME_INTERVAL = false;

// ----------------------------------------------------------------------

static void app_initialize() {
	// initialize video decoder unit
	le_video_decoder::le_video_decoder_i.init();
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

// ----------------------------------------------------------------------

static void app_reset_camera( app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

static void app_on_video_playback_complete( le_video_decoder_o* decoder, void* user_data ) {
	auto        self   = static_cast<app_o*>( user_data );
	static auto logger = le::Log( "video_app" );

	logger.info( "Video playback complete." );
};

// ----------------------------------------------------------------------

static void app_add_video_player( app_o* self ) {
	video_and_texture_t video;

	static char const* video_player_examples[] = {
	    "./local_resources/test_data/out_ref.mp4", // foreman
	    "./local_resources/test_data/milo.mp4",    // fauna
	};

	video.video_player         = new le::VideoPlayer( self->renderer, video_player_examples[ self->video_players.size() % 2 ] );
	video.video_texture_handle = le::Renderer::produceTextureHandle( nullptr );

	// We must forward the callback in case we want to be able to hot-reload this app,
	// as the callback address needs to be automatically updated if the app gets reloaded.
	//
	video.video_player->set_on_playback_complete_callback( le_core_forward_callback( video_player_example_app_api_i->video_player_example_app_i.on_video_playback_complete ), self );

	self->video_players.push_back( video );
}

// ----------------------------------------------------------------------

static void app_remove_video_player( app_o* self ) {
	if ( self->video_players.empty() ) {
		return;
	}

	// --- invariant: there is at least one video player

	auto v = self->video_players.back();
	self->video_players.pop_back();

	// Explicitly deleting the le::Video object will call
	// the destroy() method on the underlying video player object
	delete ( v.video_player );
}

// ----------------------------------------------------------------------

static video_player_example_app_o* app_create() {
	auto app = new ( video_player_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // VideoPlayerExampleApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	for ( size_t i = 0; i != 1; i++ ) {
		app_add_video_player( app );
	}

	// LE_SETTING( uint32_t, LE_SETTING_GENERATE_QUEUE_SYNC_DOT_FILES, 10 );
	// LE_SETTING( uint32_t, LE_SETTING_RENDERGRAPH_GENERATE_DOT_FILES, 10 );
	app->timebase.reset();

	app->gui = le_imgui::le_imgui_i.create();

	app_reset_camera( app );

	return app;
}

// ----------------------------------------------------------------------

static void app_reset_camera( app_o* self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( { 0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	self->camera.setViewMatrix( ( float* )( &camMatrix ) );
}

// ----------------------------------------------------------------------

static void app_process_ui_events( app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, &numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	// Note `process_and_filter_events` will update events, and `numEvents`.
	//
	// `numEvents` will contain the number of Events that have not been consumed by ImGui,
	// and `events` will contain all unconsumed events at the front of the vector.
	//
	le_imgui::le_imgui_i.process_and_filter_events( self->gui, events.data(), &numEvents );

	// Remove events that have been consumed by imgui
	events.resize( numEvents );

	bool wantsToggle = false;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					glm::mat4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eP ) {
					self->video_players[ 0 ].video_player->play();
				} else if ( e.key == LeUiEvent::NamedKey::eSpace ) {
					bool is_paused = self->video_players[ 1 ].video_player->get_pause_state();
					self->video_players[ 1 ].video_player->set_pause_state( !is_paused );

				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					app_reset_camera( self );
					glm::mat4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				}

			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
			break;
		}
	}

	auto swapchainExtent = self->renderer.getSwapchainExtent();

	self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );
	self->cameraController.processEvents( self->camera, events.data(), events.size() );

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o* pRp, void* user_data ) {
	auto renderpass = le::RenderPass{ pRp };
	auto app        = static_cast<video_player_example_app_o*>( user_data );

	renderpass
	    .addColorAttachment(
	        app->renderer.getSwapchainResource(),
	        le::ImageAttachmentInfoBuilder()
	            .setColorClearValue( { 0.2, 0.3, 0.2, 1.0 } ) // Clear the frame buffer to some sort of dark green
	            .build() )                                    // color attachment
	    ;

	// We must tell the renderpass that we will draw video images if they are available.
	//
	// This works implicitly by telling the renderpass that we will sample a texture
	// which is using the video image.
	for ( auto& v : app->video_players ) {

		le_img_resource_handle video_image = v.video_player->get_latest_available_frame();

		if ( video_image ) {
			renderpass.sampleTexture( v.video_texture_handle, video_image ); // associate video texture handle with video image for this renderpass
		}
	}

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto app = static_cast<app_o*>( user_data );

	le::GraphicsEncoder encoder{ encoder_ };

	// Note the `static` keyword in the following statements. This
	// means that shader modules will only be created the very first
	// time, or if the application gets hot-reloaded.

	static auto pipeline_manager = encoder.getPipelineManager();
	static auto pipeline_draw_texture_rect =
	    LeGraphicsPipelineBuilder( pipeline_manager )
	        .addShaderStage(
	            LeShaderModuleBuilder( pipeline_manager )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./local_resources/shaders/glsl/texture_ycbcr.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( pipeline_manager )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./local_resources/shaders/glsl/texture_ycbcr.frag" )
	                .build() )
	        .build();

	auto extents = encoder.getRenderpassExtent();

	le::Viewport viewports[ 1 ] = {
	    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	// Draw main scene ---

	// Data as it is laid out in the shader ubo. Be careful to respect
	// std430 or std140 layout here, depending on what you have
	// specified in the shader.
	//
	struct MvpUbo {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	MvpUbo mvp;
	mvp.model = glm::scale( glm::mat4( 1 ), glm::vec3( 300 ) ); // Note Scale 300x
	app->camera.getViewMatrix( ( float* )( &mvp.view ) );
	app->camera.getProjectionMatrix( ( float* )( &mvp.projection ) );

	static glm::vec3 const raw_vertex_positions[] = {
	    { 0, 1, 0 },
	    { 0, 0, 0 },
	    { 1, 0, 0 },
	    { 1, 1, 0 },
	};

	static glm::vec2 const texCoords[] = {
	    { 0, 1 },
	    { 0, 0 },
	    { 1, 0 },
	    { 1, 1 },
	};

	static uint16_t const indices[]{
	    0, 1, 2, // tri 0
	    0, 2, 3, // tri 1
	};

	int i                = 1;
	mvp.model            = glm::translate( mvp.model, glm::vec3( -0.5, -0.5, 0.0 ) );
	glm::mat4 model_orig = mvp.model;

	glm::vec3   current_top_right_corner = { 0, 0, 0 };
	const float padding                  = 0.1f;

	for ( auto& v : app->video_players ) {

		le_img_resource_handle video_frame = v.video_player->get_latest_available_frame();

		if ( video_frame ) {
			uint32_t w;
			uint32_t h;
			v.video_player->get_frame_dimensions( &w, &h );
			float aspect_ratio = float( w ) / float( h );

			// We will scale these idealized vertex positions by whatever
			// aspect ratio our current video has. Since we don't know the aspect ratio just
			// yet, we keep these vertex positions uninitialized for now.
			glm::vec3 vertex_positions[ 4 ];

			// Scale vertex positions to the correct aspect ratio
			for ( size_t i = 0; i != sizeof( raw_vertex_positions ) / sizeof( glm::vec3 ); i++ ) {
				vertex_positions[ i ] = raw_vertex_positions[ i ] * glm::vec3( aspect_ratio, 1, 1 );
			}

			// Make sure that the next video is set a little bit off
			current_top_right_corner.x += ( aspect_ratio + padding );

			// Wrap if current row is too long already
			if ( current_top_right_corner.x > 3.f ) {
				current_top_right_corner.x = 0;
				current_top_right_corner.y += 1 + padding;
			}

			encoder
			    .bindGraphicsPipeline( pipeline_draw_texture_rect )
			    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
			    .setVertexData( vertex_positions, sizeof( vertex_positions ), 0 )
			    .setVertexData( texCoords, sizeof( texCoords ), 1 )
			    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_video__ycbcr__" ), v.video_texture_handle )
			    .setIndexData( indices, sizeof( indices ) )
			    .drawIndexed( 6 );
		}

		// Place video rect
		mvp.model = glm::translate( model_orig, glm::vec3( current_top_right_corner.x, current_top_right_corner.y, 0 ) );
		i++;
	}
}

// ----------------------------------------------------------------------

static void app_update_gui( app_o* self, uint64_t current_ticks ) {
	le_imgui::le_imgui_i.begin_frame( self->gui );

	ImGui::SetNextWindowSize( ImVec2(0,0) ); // setting to 0 meant to auto-fit
	ImGui::Begin( "Video Example" );

	if ( ImGui::Button( "Add Video Player" ) ) {
		app_add_video_player( self );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Remove Video Player" ) ) {
		app_remove_video_player( self );
	}

	int i = 0;
	ImGui::Text( "Current app seconds: % 8.2f", std::chrono::duration<double>( le::Ticks( current_ticks ) ).count() );

	for ( auto& v : self->video_players ) {

		ImGui::Separator();

		char imgui_id[ 10 ] = {};
		snprintf( imgui_id, sizeof( imgui_id ), "imgui_%d", i );

		ImGui::PushID( imgui_id );

		uint64_t current_pos_ticks;
		float    current_pos_normalised;
		uint64_t current_frame_index;

		v.video_player->get_current_playhead_position( &current_pos_ticks, &current_pos_normalised );

		ImGui::Text( "Current playhead position: %20lu : % 8.2f", current_pos_ticks, current_pos_normalised );
		if ( v.video_player->get_latest_available_frame_index( &current_frame_index ) ) {
			ImGui::Text( "Current Frame Index: %lu", current_frame_index );
		};

		{
			char play_pause_label[ 10 ];
			bool pause_state = v.video_player->get_pause_state();
			snprintf( play_pause_label, sizeof( play_pause_label ), "%s", pause_state ? "play" : "pause" );

			if ( ImGui::Button( play_pause_label, ImVec2( 100, 25 ) ) ) {
				v.video_player->set_pause_state( pause_state ^ true );
			}
		}
		ImGui::SameLine();

		char seek_start_label[ 30 ];
		snprintf( seek_start_label, sizeof( seek_start_label ), "seek start" );
		if ( ImGui::Button( seek_start_label ) ) {
			v.video_player->seek( 0 );
		}
		ImGui::SameLine();
		char seek_target_label[ 40 ];
		snprintf( seek_target_label, sizeof( seek_target_label ), "should loop" );

		bool loop_state = v.video_player->get_playback_should_loop();
		if ( ImGui::Checkbox( seek_target_label, &loop_state ) ) {
			v.video_player->set_playback_should_loop( loop_state );
		}

		{
			float fraction = 0;
			v.video_player->get_current_playhead_position( nullptr, &fraction );
			char progress_text[ 20 ]       = {};
			char slider_float_label[ 250 ] = {};

			snprintf( slider_float_label, sizeof( slider_float_label ), "<- seek to " );

			if ( ImGui::SliderFloat( slider_float_label, &fraction, 0, 1, "% 4.2f" ) ) {
				uint64_t total_ticks  = v.video_player->get_total_duration_in_ticks();
				double   target_ticks = total_ticks * fraction;
				v.video_player->seek( target_ticks, false );
			}
		}
		i++;
		ImGui::PopID();
	}

	ImGui::End();

	le_imgui::le_imgui_i.end_frame( self->gui );
}

// ----------------------------------------------------------------------

static bool app_update( app_o* self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	if ( USE_FIXED_TIME_INTERVAL ) {
		self->timebase.update(
		    std::chrono::duration_cast<le::Ticks>(
		        std::chrono::duration<float, std::ratio<1, 60 * 1>>( 1 ) )
		        .count() );
	} else {
		self->timebase.update();
	}

	uint64_t current_ticks = self->timebase.getCurrentTicks();

	// Process user interface events such as mouse, keyboard
	app_process_ui_events( self );

	le::Extent2D swapchain_extent = self->renderer.getSwapchainExtent();

	{
		le::RenderGraph rg{};

		le_imgui::le_imgui_i.setup_resources( self->gui, rg, swapchain_extent.width, swapchain_extent.height );

		app_update_gui( self, current_ticks );

		for ( auto& v : self->video_players ) {
			v.video_player->update( rg, current_ticks );
		}

		auto draw_pass = le::RenderPass{
		    "draw",
		    le::QueueFlagBits::eGraphics,
		    pass_main_setup,
		    pass_main_exec,
		    self,
		};

		le_imgui::le_imgui_i.draw( self->gui, draw_pass );
		rg.addRenderPass( draw_pass );

		self->renderer.update( rg );
	}

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void app_destroy( video_player_example_app_o* self ) {

	for ( auto& v : self->video_players ) {
		delete v.video_player;
	}

	self->video_players.clear();

	le_imgui::le_imgui_i.destroy( self->gui );
	self->gui = nullptr;

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( video_player_example_app, api ) {

	auto  video_player_example_app_api_i = static_cast<video_player_example_app_api*>( api );
	auto& video_player_example_app_i     = video_player_example_app_api_i->video_player_example_app_i;

	video_player_example_app_i.initialize = app_initialize;
	video_player_example_app_i.terminate  = app_terminate;

	video_player_example_app_i.create  = app_create;
	video_player_example_app_i.destroy = app_destroy;
	video_player_example_app_i.update  = app_update;

	video_player_example_app_i.on_video_playback_complete = app_on_video_playback_complete;
}
