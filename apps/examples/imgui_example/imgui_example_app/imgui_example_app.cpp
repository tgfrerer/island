#include "imgui_example_app.h"

#include "le_window.h"
#include "le_renderer.hpp"

#include "le_camera.h"
#include "le_pipeline_builder.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include "le_imgui.h"
#include "3rdparty/imgui/imgui.h"
#include <vector>

#include "le_ui_event.h"

struct imgui_example_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	glm::vec4 backgroundColor{ 0, 0, 0, 1 };

	le_imgui_o*        gui;
	LeCamera           camera;
	LeCameraController cameraController;
};

typedef imgui_example_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void app_reset_camera( imgui_example_app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

static imgui_example_app_o* app_create() {
	auto app = new ( imgui_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // ImguiExampleApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	// Set up the camera
	app_reset_camera( app );

	app->gui = le_imgui::le_imgui_i.create();

	return app;
}

// ----------------------------------------------------------------------

static void app_reset_camera( imgui_example_app_o* self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( { 0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 view_matrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	self->camera.setViewMatrix( ( float* )( &view_matrix ) );
}

// ----------------------------------------------------------------------
static void app_process_ui_events( app_o* self, std::vector<LeUiEvent> const& events ) {
	using namespace le_window;

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

static void renderpass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto                app = static_cast<imgui_example_app_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	auto extents = encoder.getRenderpassExtent();

	le::Viewport viewports[ 1 ] = {
	    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	// Data as it is laid out in the shader ubo
	struct MvpUbo {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene

	static auto pipelineImguiExample =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./resources/shaders/default.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./resources/shaders/default.frag" )
	                .build() )
	        .build();

	float norm_angle = ( app->frame_counter % 360 ) / float( 360 );

	MvpUbo mvp;
	mvp.model = glm::mat4( 1.f ); // identity matrix
	mvp.model = glm::rotate( mvp.model, glm::two_pi<float>() * norm_angle, glm::vec3{ 0, 0, 1 } );
	mvp.model = glm::scale( mvp.model, glm::vec3( 4.5 ) );

	app->camera.getViewMatrix( ( float* )( &mvp.view ) );
	app->camera.getProjectionMatrix( ( float* )( &mvp.projection ) );

	glm::vec3 imgui_examplePositions[] = {
	    { -50, -50, 0 },
	    { 50, -50, 0 },
	    { 0, 50, 0 },
	};

	glm::vec4 imgui_exampleColors[] = {
	    { 1, 0, 0, 1.f },
	    { 0, 1, 0, 1.f },
	    { 0, 0, 1, 1.f },
	};

	encoder
	    .bindGraphicsPipeline( pipelineImguiExample )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
	    .setVertexData( imgui_examplePositions, sizeof( imgui_examplePositions ), 0 )
	    .setVertexData( imgui_exampleColors, sizeof( imgui_exampleColors ), 1 )
	    .draw( 3 );
}

// ----------------------------------------------------------------------

static bool app_update( imgui_example_app_o* self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	{
		// -- Process UI events

		LeUiEvent const* pEvents;
		uint32_t         numEvents = 0;
		self->window.getUIEventQueue( &pEvents, &numEvents );

		// create a local copy current events
		std::vector<LeUiEvent> current_events{ pEvents, pEvents + numEvents };

		// "Consume" events that affect imgui - all events which are handled by imgui
		// are removed from the event queue by placing them at the end, past numEvents.
		le_imgui::le_imgui_i.process_and_filter_events( self->gui, current_events.data(), &numEvents );

		// Filtered events will be at front,
		// numEvents is number of filtered events
		current_events.resize( numEvents );

		app_process_ui_events( self, current_events );
	}

	uint32_t swapchainWidth = 0, swapchainHeight = 0;
	self->renderer.getSwapchainExtent( &swapchainWidth, &swapchainHeight );

	static auto texture_handle_1                        = LE_TEXTURE( "src_texture_handle_1" );
	static auto render_target_image_1                   = LE_IMG_RESOURCE( "render_target_image_1" );
	ImVec2      render_target_image_1_requested_extents = {};

	le::RenderGraph rendergraph{};

	// You must set up imgui's rendergraph resources before you begin an imgui frame
	le_imgui::le_imgui_i.setup_resources( self->gui, rendergraph, float( swapchainWidth ), float( swapchainHeight ) );

	{
		// All ImGui Drawing happens inside this block.

		le_imgui::le_imgui_i.begin_frame( self->gui );

		ImGui::ShowMetricsWindow();
		ImGui::ShowDemoWindow();

		ImGui::Begin( "Rendered Image" );
		{
			ImGui::Text( "This image has been rendered in a different renderpass:" );
			render_target_image_1_requested_extents = ImGui::GetContentRegionAvail();
			ImGui::Image( texture_handle_1, render_target_image_1_requested_extents ); // You must remember to make this texture available to the renderpass that draws the GUI!
		}
		ImGui::End();

		ImGui::Begin( "Background Color Chooser" ); // begin window
		{
			// Background colour edit
			if ( ImGui::ColorEdit3( "Background Color", &self->backgroundColor.x ) ) {
			}

			if ( ImGui::Button( "White Background" ) ) {
				self->backgroundColor = { 1, 1, 1, 1 };
			}
		}
		ImGui::End(); // end window

		le_imgui::le_imgui_i.end_frame( self->gui );
	}

	// This renderpass will draw a rgb triangle into image render_target_image_1
	// Note that we specify the width and height of the renderpass, which will
	// set the image dimensions automatically to these values.
	//
	auto pass_draw_into_render_target_image_1 =
	    le::RenderPass( "to_image", le::QueueFlagBits::eGraphics )
	        .setWidth( render_target_image_1_requested_extents.x )
	        .setHeight( render_target_image_1_requested_extents.y )
	        .addColorAttachment( render_target_image_1 )      //
	        .setExecuteCallback( self, renderpass_main_exec ) //
	    ;

	// This is the root renderpass, that is the renderpass that will render to screen.
	// we use it to draw the GUI.
	//
	// Note:  Since the GUI will draw the image that we rendered
	//        into in pass_draw_into_render_target_image_1, we must make this
	//        image available as a texture.
	//
	// Note:  We do not set an execute callback: We could draw into this renderpass here,
	//        but the most important draw calls will be the ones that are added via
	//        `le_imgui_i.draw()`:
	//
	auto pass_to_screen =
	    le::RenderPass( "to_screen", le::QueueFlagBits::eGraphics )
	        .addColorAttachment( self->renderer.getSwapchainResource(),
	                             le::ImageAttachmentInfoBuilder()
	                                 .setColorClearValue( reinterpret_cast<le::ClearValue&>( self->backgroundColor ) )
	                                 .build() )                       // swapchain color attachment is the color attachment that goes to the swapchain and therefore becomes visible.
	        .sampleTexture( texture_handle_1, render_target_image_1 ) // associate texture_handle_1 with render_target_image_1, and make it available for sampling inside the renderpass
	    ;

	//
	le_imgui::le_imgui_i.draw( self->gui, pass_to_screen );

	// Add renderpasses to rendergraph

	rendergraph
	    .addRenderPass( pass_draw_into_render_target_image_1 )
	    .addRenderPass( pass_to_screen ) //
	    ;

	self->renderer.update( rendergraph );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void app_destroy( imgui_example_app_o* self ) {
	if ( self ) {
		le_imgui::le_imgui_i.destroy( self->gui );
	}
	delete ( self );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( imgui_example_app, api ) {
	auto  imgui_example_app_api_i = static_cast<imgui_example_app_api*>( api );
	auto& imgui_example_app_i     = imgui_example_app_api_i->imgui_example_app_i;

	imgui_example_app_i.initialize = app_initialize;
	imgui_example_app_i.terminate  = app_terminate;

	imgui_example_app_i.create  = app_create;
	imgui_example_app_i.destroy = app_destroy;
	imgui_example_app_i.update  = app_update;
}
