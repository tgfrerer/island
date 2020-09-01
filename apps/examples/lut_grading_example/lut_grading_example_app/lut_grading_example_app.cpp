#include "lut_grading_example_app.h"

#include "le_window/le_window.h"
#include "le_renderer/le_renderer.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_pixels/le_pixels.h" // only needed for static build
#include "le_resource_manager/le_resource_manager.h"
#include "le_ui_event/le_ui_event.h"

#include <iostream>
#include <memory>
#include <sstream>

constexpr le_resource_handle_t SRC_IMG_HANDLE       = LE_IMG_RESOURCE( "src_image" );
constexpr le_resource_handle_t COLOR_LUT_IMG_HANDLE = LE_IMG_RESOURCE( "color_lut_image" );

struct lut_grading_example_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	float    mouse_x_normalised = 0.5; // current mouse x control point, normalised over width of window
	uint32_t mouse_button_state = 0;   // state of all mouse buttons - this uint32 is used as an array of 32 bools, really.

	LeResourceManager resource_manager;
};

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

// ----------------------------------------------------------------------

static lut_grading_example_app_o *lut_grading_example_app_create() {
	auto app = new ( lut_grading_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 640 )
	    .setHeight( 960 )
	    .setTitle( "Island // LutGradingExampleApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	char const *hald_lut =
	    "./local_resources/images/night_from_day.png";
	//	    "./local_resources/images/hald_8_identity.png";  // pass-through

	char const *src_image_path =
	    "./local_resources/images/revolt-97ZPiaJbDuA-unsplash.jpg";

	// Provide additional information for 3D LUT Image:
	// ImageType, Dimensions need to be explicit.
	auto image_info_color_lut_texture =
	    le::ImageInfoBuilder()
	        .setImageType( le::ImageType::e3D )
	        .setExtent( 64, 64, 64 )
	        .build();

	// Instruct resource manager to load data for images from given path
	app->resource_manager.add_item( COLOR_LUT_IMG_HANDLE, image_info_color_lut_texture, &hald_lut );
	app->resource_manager.add_item( SRC_IMG_HANDLE, le::ImageInfoBuilder().build(), &src_image_path );

	return app;
}

static void app_process_ui_events( lut_grading_example_app_o *self ) {

	le::UiEvent const *events;
	uint32_t           num_events;

	uint32_t swapchain_width;
	uint32_t swapchain_height;

	self->window.getUIEventQueue( &events, num_events );
	self->renderer.getSwapchainExtent( &swapchain_width, &swapchain_height );

	auto events_begin = events;
	auto events_end   = events + num_events;

	for ( auto e = events_begin; e != events_end; e++ ) {

		if ( e->event == LeUiEvent::Type::eCursorPosition ) {
			if ( self->mouse_button_state & 0x1 ) {
				// if first mouse button is pressed
				self->mouse_x_normalised = e->cursorPosition.x / float( swapchain_width );
			}
		}

		else if ( e->event == LeUiEvent::Type::eMouseButton ) {
			if ( e->mouseButton.action == LeUiEvent::ButtonAction::eRelease ) {
				self->mouse_button_state &= ( 0 << e->mouseButton.button );
			} else {
				// event is either press or repeat
				self->mouse_button_state |= ( 1 << e->mouseButton.button );
			}
		}
	}
}

// ----------------------------------------------------------------------
// This method gets updated once per frame
static bool lut_grading_example_app_update( lut_grading_example_app_o *self ) {

	// Polls events for all windows to see if we need to close window
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	app_process_ui_events( self );

	le::RenderModule mainModule{};

	// resource_manager uploads image data to gpu if image has not yet been uploaded.
	self->resource_manager.update( mainModule );

	static auto src_image_texture = le::Renderer::produceTextureHandle( "src_image" );
	static auto lut_image_texture = le::Renderer::produceTextureHandle( "color_lut_image" );

	// Specialise Sampler and ImageView information for 3d lut texture
	auto lut_tex_info =
	    le::ImageSamplerInfoBuilder()
	        .withImageViewInfo()
	        .setImage( COLOR_LUT_IMG_HANDLE )
	        .setImageViewType( le::ImageViewType::e3D )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eMirroredRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
	        .setAddressModeW( le::SamplerAddressMode::eMirroredRepeat )
	        .end()
	        .build();

	// Specialise Sampler and ImageView for 2d src image texture
	auto src_imag_tex_info =
	    le::ImageSamplerInfoBuilder()
	        .withImageViewInfo()
	        .setImage( SRC_IMG_HANDLE )
	        .end()
	        .build();

	// Note that callbacks for renderpasses are given inline here - but
	// you could just as well pass function pointers instead of lambdas
	// To see how, look at the other examples.
	auto renderPassMain =
	    le::RenderPass( "main" )
	        .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE )
	        .sampleTexture( lut_image_texture, lut_tex_info )      // Declare texture name: color lut image
	        .sampleTexture( src_image_texture, src_imag_tex_info ) // Declare texture name: src image
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
		        auto        app = static_cast<lut_grading_example_app_o *>( user_data );
		        le::Encoder encoder{ encoder_ };

		        // Draw main scene

		        static auto shaderVert = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.vert", le::ShaderStage::eVertex );
		        static auto shaderFrag = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.frag", le::ShaderStage::eFragment );

		        static auto pipelineLutGradingExample =
		            LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		                .addShaderStage( shaderVert )
		                .addShaderStage( shaderFrag )
		                .build();

		        static auto src_image_texture = le::Renderer::produceTextureHandle( "src_image" );
		        static auto lut_image_texture = le::Renderer::produceTextureHandle( "color_lut_image" );

		        encoder
		            .bindGraphicsPipeline( pipelineLutGradingExample )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), src_image_texture )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), lut_image_texture )
		            .setArgumentData( LE_ARGUMENT_NAME( "Params" ), &app->mouse_x_normalised, sizeof( float ) )
		            .draw( 4 );
	        } ) //
	    ;

	mainModule.addRenderPass( renderPassMain );

	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive, false will quit app.
}

// ----------------------------------------------------------------------

static void lut_grading_example_app_destroy( lut_grading_example_app_o *self ) {

	delete ( self );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( lut_grading_example_app, api ) {

	auto  lut_grading_example_app_api_i = static_cast<lut_grading_example_app_api *>( api );
	auto &lut_grading_example_app_i     = lut_grading_example_app_api_i->lut_grading_example_app_i;

	lut_grading_example_app_i.initialize = app_initialize;
	lut_grading_example_app_i.terminate  = app_terminate;

	lut_grading_example_app_i.create  = lut_grading_example_app_create;
	lut_grading_example_app_i.destroy = lut_grading_example_app_destroy;
	lut_grading_example_app_i.update  = lut_grading_example_app_update;
}
