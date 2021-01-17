#include "hello_video_app.h"

#include "le_window/le_window.h"
#include "le_renderer/le_renderer.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_camera/le_camera.h"
#include "le_ui_event/le_ui_event.h"
#include "le_video/le_video.h"
#include "le_resource_manager/le_resource_manager.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct hello_video_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCamera           camera;
	LeCameraController cameraController;
	LeResourceManager  resource_manager;

	le::Video video;
};

constexpr le_resource_handle_t VIDEO_HANDLE = LE_IMG_RESOURCE( "video" );

typedef hello_video_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
	le::Video::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void app_reset_camera( app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static app_o *app_create() {
	auto app = new ( app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1280 )
	    .setHeight( 720 )
	    .setTitle( "Island // HelloVideoApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	app->video.setup( app->resource_manager, VIDEO_HANDLE );
	app->video.load( "./local_resources/test.mp4" );

	// Set up the camera
	app_reset_camera( app );

	return app;
}

// ----------------------------------------------------------------------

static void app_reset_camera( app_o *self ) {
	//	le::Extent2D extents{};
	//	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	//	self->camera.setViewport( { 0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f } );
	//	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	//	glm::mat4 camMatrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	//	self->camera.setViewMatrixGlm( camMatrix );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{ pRp };
	auto app = static_cast<app_o *>( user_data );

	// Attachment may be further specialised using le::ImageAttachmentInfoBuilder().

	rp.addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE, le::ImageAttachmentInfoBuilder().build() );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto app = static_cast<app_o *>( user_data );

	le::Encoder encoder{ encoder_ };

	static auto shaderVert = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.vert", le::ShaderStage::eVertex );
	static auto shaderFrag = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.frag", le::ShaderStage::eFragment );

	static auto video_texture = le::Renderer::produceTextureHandle( "video" );

	static auto pipelineHelloVideoExample =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .build();

	encoder
	    .bindGraphicsPipeline( pipelineHelloVideoExample )
	    .setArgumentTexture( LE_ARGUMENT_NAME( "src_video" ), video_texture )
	    .draw( 4 );

	//	auto        app = static_cast<app_o *>( user_data );
	//	le::Encoder encoder{ encoder_ };
	//
	//	auto extents = encoder.getRenderpassExtent();
	//
	//	le::Viewport viewports[ 1 ] = {
	//	    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
	//	};
	//
	//	app->camera.setViewport( viewports[ 0 ] );
	//
	//	// Data as it is laid out in the shader ubo.
	//	// Be careful to respect std430 or std140 layout
	//	// depending on what you specify in the
	//	// shader.
	//	struct MvpUbo {
	//		glm::mat4 model;
	//		glm::mat4 view;
	//		glm::mat4 projection;
	//	};
	//
	//	// Draw main scene
	//
	//	static auto shaderVert = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.vert", le::ShaderStage::eVertex );
	//	static auto shaderFrag = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.frag", le::ShaderStage::eFragment );
	//
	//	static auto pipelineHelloVideo =
	//	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	//	        .addShaderStage( shaderVert )
	//	        .addShaderStage( shaderFrag )
	//	        .build();
	//
	//	MvpUbo mvp;
	//	mvp.model      = glm::mat4( 1.f ); // identity matrix
	//	mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
	//	mvp.view       = app->camera.getViewMatrixGlm();
	//	mvp.projection = app->camera.getProjectionMatrixGlm();
	//
	//	glm::vec3 vertexPositions[] = {
	//	    { -50, -50, 0 },
	//	    { 50, -50, 0 },
	//	    { 0, 50, 0 },
	//	};
	//
	//	glm::vec4 vertexColors[] = {
	//	    { 1, 0, 0, 1.f },
	//	    { 0, 1, 0, 1.f },
	//	    { 0, 0, 1, 1.f },
	//	};
	//
	//	encoder
	//	    .bindGraphicsPipeline( pipelineHelloVideo )
	//	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
	//	    .setVertexData( vertexPositions, sizeof( vertexPositions ), 0 )
	//	    .setVertexData( vertexColors, sizeof( vertexColors ), 1 )
	//	    .draw( 3 );
}

// ----------------------------------------------------------------------
static void app_process_ui_events( app_o *self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const *pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool wantsToggle = false;

	for ( auto &event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto &e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					app_reset_camera( self );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
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

static bool app_update( app_o *self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// update interactive camera using mouse data
	app_process_ui_events( self );

	static auto video_texture = le::Renderer::produceTextureHandle( "video" );

	le::RenderModule mainModule{};
	{

		auto video_tex_info =
		    le::ImageSamplerInfoBuilder()
		        .withImageViewInfo()
		        .setImage( VIDEO_HANDLE )
		        .end()
		        .build();

		auto renderPassFinal =
		    le::RenderPass( "root", LE_RENDER_PASS_TYPE_DRAW )
		        .sampleTexture( video_texture, video_tex_info ) // Declare texture name: color lut image
		        .setSetupCallback( self, pass_main_setup )
		        .setExecuteCallback( self, pass_main_exec ) //
		    ;

		mainModule.addRenderPass( renderPassFinal );
	}

	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void app_destroy( app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( hello_video_app, api ) {
	auto  hello_video_app_api_i = static_cast<hello_video_app_api *>( api );
	auto &hello_video_app_i     = hello_video_app_api_i->hello_video_app_i;

	hello_video_app_i.initialize = app_initialize;
	hello_video_app_i.terminate  = app_terminate;

	hello_video_app_i.create  = app_create;
	hello_video_app_i.destroy = app_destroy;
	hello_video_app_i.update  = app_update;
}
