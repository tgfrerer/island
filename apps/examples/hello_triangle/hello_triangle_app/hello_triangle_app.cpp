#include "hello_triangle_app.h"

#include "le_window.h"
#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_camera.h"
#include "le_ui_event.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct hello_triangle_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCamera           camera;
	LeCameraController cameraController;
};

// We use this local typedef so spare us lots of typing
typedef hello_triangle_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void app_reset_camera( app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

static app_o* app_create() {
	auto app = new ( app_o );

	// If you want to disable validation layers in a debug build,
	// set the setting to false:
	LE_SETTING( bool, LE_SETTING_SHOULD_USE_VALIDATION_LAYERS, true );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // HelloTriangleApp" );

	// create a new window
	app->window.setup( settings );

	// create a new renderer
	app->renderer.setup(
	    le::RendererInfoBuilder()
	        .addSwapchain()
	        .asWindowSwapchain()
	        .setPresentmode( le::Presentmode::eFifoRelaxed )
	        .setWindow( app->window )
	        .end()
	        .setImagecountHint( 3 )
	        .end()
	        .build() );

	// Set up the camera
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
	window_i.get_ui_event_queue( self->window, &pEvents,&numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

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
// Records draw commmands (and their associated data) into the
// Encoder, so that this can then be executed via the backend.
static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {

	auto app = static_cast<app_o*>( user_data );

	le::Encoder encoder{ encoder_ };

	auto extents = encoder.getRenderpassExtent();

	le::Viewport viewports[ 1 ] = {
	    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	// Data as it is laid out in the shader ubo. Be careful to respect
	// std430 or std140 layout here, depending on what you have
	// specified in the shader.
	//
	struct MvpUbo {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene ---

	// Note the `static` keyword in the following statements. This
	// means that shader modules will only be created the very first
	// time, or if the application gets hot-reloaded.

	static auto pipelineHelloTriangle =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./local_resources/shaders/default.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./local_resources/shaders/default.frag" )
	                .build() )
	        .build();

	MvpUbo mvp;
	mvp.model = glm::mat4( 1.f ); // identity matrix
	mvp.model = glm::scale( mvp.model, glm::vec3( 4.5 ) );
	app->camera.getViewMatrix( ( float* )( &mvp.view ) );
	app->camera.getProjectionMatrix( ( float* )( &mvp.projection ) );

	glm::vec3 vertexPositions[] = {
	    { -50, -50, 0 },
	    { 50, -50, 0 },
	    { 0, 50, 0 },
	};

	glm::vec4 vertexColors[] = {
	    { 1, 0, 0, 1.f },
	    { 0, 1, 0, 1.f },
	    { 0, 0, 1, 1.f },
	};

	// Note that instead of binding buffers for vertices, we use
	// setVertexData to provide vertex positiona and color data for
	// the draw command inline. This is generally a passable choice
	// for small, frequently changing geometry data.

	encoder
	    .bindGraphicsPipeline( pipelineHelloTriangle )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
	    .setVertexData( vertexPositions, sizeof( vertexPositions ), 0 )
	    .setVertexData( vertexColors, sizeof( vertexColors ), 1 )
	    .draw( 3 );
}

// ----------------------------------------------------------------------

static bool app_update( app_o* self ) {

	// Polls events for all windows
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// will resize the window incessantly....
	self->window.setWindowSize( 1024, 1024 + ( self->frame_counter % 10 ) * 4 );

	// Update interactive camera using mouse inputs
	app_process_ui_events( self );

	// We use a RenderModule to give the renderer a top-level overview
	// of how we wish to do rendering.
	//
	// Our key tool for structure is a RenderPass, which represents
	// a collection of resource inputs (images, buffers) and resource
	// outputs (color attachments, depth attachments).
	// By connecting their outputs to one or more subsequent RenderPass
	// inputs, RenderPasseses can form a graph, which the renderer must
	// respect.
	//
	// A key image resource is `LE_SWAPCHAIN_IMAGE_HANDLE` - whatever
	// you draw into this resource will end up on screen. Only
	// renderpasses which contribute to this resource will get
	// executed.
	static le_img_resource_handle LE_SWAPCHAIN_IMAGE_HANDLE = self->renderer.getSwapchainResource();

	le::RenderGraph renderGraph{};
	{

		auto renderPassFinal =
		    le::RenderPass( "root", le::QueueFlagBits::eGraphics )
		        .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE ) // Color attachment
		        .setExecuteCallback( self, pass_main_exec )      // This is where we record our draw commands
		        .setSampleCount( le::SampleCountFlagBits::e4 )   // Perform an additional image -re-allocation per frame
		    ;

		renderGraph.addRenderPass( renderPassFinal );
	}

	// This evaluate the rendergraph by first calling `setup()` on all
	// renderpasses, then checking which passes contribute to
	// `LE_SWAPCHAIN_IMAGE_HANDLE`, and then executing contributing
	// passes in order.
	self->renderer.update( renderGraph );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void app_destroy( app_o* self ) {
	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( hello_triangle_app, api ) {
	auto  hello_triangle_app_api_i = static_cast<hello_triangle_app_api*>( api );
	auto& hello_triangle_app_i     = hello_triangle_app_api_i->hello_triangle_app_i;

	hello_triangle_app_i.initialize = app_initialize;
	hello_triangle_app_i.terminate  = app_terminate;

	hello_triangle_app_i.create  = app_create;
	hello_triangle_app_i.destroy = app_destroy;
	hello_triangle_app_i.update  = app_update;
}
