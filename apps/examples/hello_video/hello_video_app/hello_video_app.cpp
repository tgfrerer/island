#include "hello_video_app.h"

#include "le_window/le_window.h"
#include "le_renderer/le_renderer.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_ui_event/le_ui_event.h"
#include "le_video/le_video.h"
#include "le_resource_manager/le_resource_manager.h"
#include "le_log/le_log.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.

#include <vector>

struct hello_video_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeResourceManager resource_manager;

	le::Video video;

	le_log_module_o *log = le_log::get_module( "hello_video" );
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
	le_log::info( app->log, "Loaded Video" );

	return app;
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
				}

			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
			break;
		}
	}

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
		self->resource_manager.update( mainModule );

		auto video_tex_info =
		    le::ImageSamplerInfoBuilder()
		        .withImageViewInfo()
		        .setImage( VIDEO_HANDLE )
		        .end()
		        .build();

		auto renderPassFinal =
		    le::RenderPass( "root", LE_RENDER_PASS_TYPE_DRAW )
		        .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE )
		        .sampleTexture( video_texture, video_tex_info ) // Declare texture video
		        .setExecuteCallback( self, pass_main_exec )     //
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
