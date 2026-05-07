#include "lut_grading_example_app.h"

#include "le_window.h"
#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_resource_manager.h"
#include "le_ui_event.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <math.h>

struct lut_grading_example_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	float    mouse_x_normalised = 0.5; // current mouse x control point, normalised over width of window
	uint32_t mouse_button_state = 0;   // state of all mouse buttons - this uint32 is used as an array of 32 bools, really.

	LeResourceManager        resource_manager{ renderer };

	le_image_resource_handle image_0   = renderer.createImageResourceHandle( "image_0" );
	le_image_resource_handle image_1   = renderer.createImageResourceHandle( "image_1" );
	le_image_resource_handle image_lut = renderer.createImageResourceHandle( "lut_image" );

	le::Extent2D               window_extents;

	le_bindless_texture_handle tex_0 = nullptr;
	le_bindless_texture_handle tex_1 = nullptr;
	le_bindless_texture_handle lut_0 = nullptr;

	le_image_resource_handle swapchain_handle = nullptr;
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

static lut_grading_example_app_o* lut_grading_example_app_create() {
	auto app = new ( lut_grading_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 640 )
	    .setHeight( 960 )
	    .setTitle( "Island // LutGradingExampleApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( app->window );
	app->window_extents = app->renderer.getSwapchainExtent();

	char const* hald_lut =
	    "./local_resources/images/night_from_day.png";
	//	    "./local_resources/images/hald_8_identity.png";  // pass-through

	char const* src_image_0_path =
	    "./local_resources/images/revolt-97ZPiaJbDuA-unsplash.jpg";

	char const* src_image_1_path =
	    "./local_resources/images/helena-lopes-7FC4WpyYcfQ-unsplash.jpg";

	// Provide additional information for 3D LUT Image:
	// ImageType, Dimensions need to be explicit.
	auto image_info_color_lut_image_info =
	    le::ImageInfoBuilder()
	        .setImageType( le::ImageType::e3D )
	        .setExtent( 64, 64, 64 )
	        .build();

	// Instruct resource manager to load data for images from given path
	app->resource_manager.add_item( app->image_lut, image_info_color_lut_image_info, &hald_lut, true );
	app->resource_manager.add_item( app->image_0, le::ImageInfoBuilder().build(), &src_image_0_path, true );
	app->resource_manager.add_item( app->image_1, le::ImageInfoBuilder().build(), &src_image_1_path, true );

	return app;
}

// ----------------------------------------------------------------------

static void app_process_ui_events( lut_grading_example_app_o* self ) {

	le::UiEvent const* events;
	uint32_t           num_events;

	bool         wants_toggle = false;
	bool         was_resized  = false;
	le::Extent2D window_extents;

	self->window.getUIEventQueue( &events, &num_events );

	auto events_begin = events;
	auto events_end   = events + num_events;

	for ( auto e = events_begin; e != events_end; e++ ) {
		if ( e->event == LeUiEvent::Type::eWindowExtent ) {
			auto& ev       = e->windowExtent;
			window_extents = {
			    .width  = ev.width,
			    .height = ev.height,
			};
			was_resized = true;
		}

		if ( e->event == LeUiEvent::Type::eCursorPosition ) {
			if ( self->mouse_button_state & 0x1 ) {
				// if first mouse button is pressed
				self->mouse_x_normalised = e->cursorPosition.x / float( self->window_extents.width );
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

	if ( was_resized ) {
		self->renderer.resizeSwapchain( window_extents.width, window_extents.height );
		self->window_extents = window_extents;
	}
}

// ----------------------------------------------------------------------
// This method gets updated once per frame
static bool lut_grading_example_app_update( lut_grading_example_app_o* self ) {

	// Polls events for all windows to see if we need to close window
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	app_process_ui_events( self );

	le::RenderGraph renderGraph{};

	// resource_manager uploads image data to gpu if image has not yet been uploaded.
	self->resource_manager.update( renderGraph );

	if ( nullptr == self->swapchain_handle ) {
		self->swapchain_handle = self->renderer.getSwapchainResource();
	}

	if ( self->tex_0 == nullptr ) {
		self->tex_0 = self->renderer.allocateBindlessTexture(
		    le::ImageSamplerInfoBuilder()
		        .withImageViewInfo()
		        .setImage( self->image_0 )
		        .end()
		        .build() );
	}


	if ( self->tex_1 == nullptr ) {
		self->tex_1 = self->renderer.allocateBindlessTexture(
		    le::ImageSamplerInfoBuilder()
		        .withImageViewInfo()
		        .setImage( self->image_1 )
		        .end()
		        .build() );
	}

	// Specialise Sampler and ImageView information for 3d lut texture
	if ( self->lut_0 == nullptr ) {
		self->lut_0 = self->renderer.allocateBindlessTexture(
		    le::ImageSamplerInfoBuilder()
		        .withImageViewInfo()
		        .setImage( self->image_lut )
		        .setImageViewType( le::ImageViewType::e3D )
		        .end()
		        .withSamplerInfo()
		        .setAddressModeU( le::SamplerAddressMode::eMirroredRepeat )
		        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
		        .setAddressModeW( le::SamplerAddressMode::eMirroredRepeat )
		        .end()
		        .build() );
	}

	// Note that callbacks for renderpasses are given inline here - but
	// you could just as well pass function pointers instead of lambdas.
	//
	// To see how, look at the other examples.
	auto renderPassMain =
	    le::RenderPass( "main" )
	        .addColorAttachment( self->swapchain_handle )
	        .useImageResource( self->lut_0 )
	        .useImageResource( self->tex_0 )
	        .useImageResource( self->tex_1 )

	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	            auto                app = static_cast<lut_grading_example_app_o*>( user_data );
		        le::GraphicsEncoder encoder{ encoder_ };

		        // Draw main scene

		        static auto pipelineLutGradingExample =
		            LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		                .addShaderStage(
		                    LeShaderModuleBuilder( encoder.getPipelineManager() )
		                        .setShaderStage( le::ShaderStage::eVertex )
		                        .setSourceFilePath( "./local_resources/shaders/fullscreen.vert" )
		                        .build() )
		                .addShaderStage(
		                    LeShaderModuleBuilder( encoder.getPipelineManager() )
		                        .setShaderStage( le::ShaderStage::eFragment )
		                        .setSourceFilePath( "./local_resources/shaders/fullscreen.frag" )
		                        .build() )
		                .build();

		        float progress = app->mouse_x_normalised;

		        // progress = 0.5 + 0.5 * sinf( std::numbers::pi * 2 * ( app->frame_counter % ( 240 * 4 ) ) / float( 240 * 4 ) );

		        struct PushConstants {
			        uint32_t tex_0;
			        uint32_t tex_1;
			        uint32_t tex_lut;
		        } push_constant_data;

		        push_constant_data.tex_0   = app->tex_0->as_uint32();
		        push_constant_data.tex_1   = app->tex_1->as_uint32();
		        push_constant_data.tex_lut = app->lut_0->as_uint32();

		        encoder
		            .bindGraphicsPipeline( pipelineLutGradingExample )
		            // .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), lut_image_texture )
		            .setPushConstantData( &push_constant_data, sizeof( PushConstants ) )
		            .setArgumentData( LE_ARGUMENT_NAME( "Params" ), &progress, sizeof( float ) )
		            .draw( 4 );
	        } ) //
	    ;

	renderGraph.addRenderPass( renderPassMain );

	self->renderer.update( renderGraph );

	self->frame_counter++;

	return true; // keep app alive, false will quit app.
}

// ----------------------------------------------------------------------

static void lut_grading_example_app_destroy( lut_grading_example_app_o* self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( lut_grading_example_app, api ) {

	auto  lut_grading_example_app_api_i = static_cast<lut_grading_example_app_api*>( api );
	auto& lut_grading_example_app_i     = lut_grading_example_app_api_i->lut_grading_example_app_i;

	lut_grading_example_app_i.initialize = app_initialize;
	lut_grading_example_app_i.terminate  = app_terminate;

	lut_grading_example_app_i.create  = lut_grading_example_app_create;
	lut_grading_example_app_i.destroy = lut_grading_example_app_destroy;
	lut_grading_example_app_i.update  = lut_grading_example_app_update;
}
