#include "test_mipmaps_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_pixels/le_pixels.h"
#include "le_ui_event/le_ui_event.h"

#include "le_pipeline_builder/le_pipeline_builder.h"

#include <iostream>
#include <memory>
#include <sstream>

#define LE_ARGUMENT_NAME( x ) hash_64_fnv1a_const( #x )

struct Image : NoCopy, NoMove {
	le_resource_handle_t imageHandle{};
	le_resource_info_t   imageInfo{};
	le_resource_handle_t textureHandle{};
	le_pixels_o *        pixels; // owned
	le_pixels_info       pixelsInfo;
	bool                 wasLoaded{};

	~Image() {

		if ( pixels ) {
			using namespace le_pixels;
			le_pixels_i.destroy( pixels );
		}
	}
};

// ----------------------------------------------------------------------

struct test_mipmaps_app_o {
	le::Backend  backend;
	pal::Window  window;
	le::Renderer renderer;

	float lodBias = 0; // will be set through cursor position
	Image testImage;
};

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

// ----------------------------------------------------------------------
// FIXME: miplevels parameter placement is weird.
static bool initialiseImage( Image &img, char const *path, uint32_t mipLevels = 1, le_pixels_info::TYPE const &pixelType = le_pixels_info::eUInt8, le::Format const &imgFormat = le::Format::eR8G8B8A8Unorm, int numChannels = 4 ) {
	using namespace le_pixels;
	img.pixels     = le_pixels_i.create( path, numChannels, pixelType );
	img.pixelsInfo = le_pixels_i.get_info( img.pixels );

	// store earth albedo image handle
	img.imageHandle = LE_IMG_RESOURCE( path );
	img.imageInfo   = le::ImageInfoBuilder()
	                    .setFormat( imgFormat )
	                    .setExtent( img.pixelsInfo.width, img.pixelsInfo.height )
	                    .addUsageFlags( LE_IMAGE_USAGE_TRANSFER_DST_BIT )
	                    .setMipLevels( mipLevels )
	                    .build();

	img.textureHandle = LE_TEX_RESOURCE( ( std::string( path ) + "_tex" ).c_str() );
	return true;
}

// ----------------------------------------------------------------------

static test_mipmaps_app_o *test_mipmaps_app_create() {
	auto app = new ( test_mipmaps_app_o );

	// Load an image
	initialiseImage( app->testImage, "./resources/images/horse-1330690_640.jpg", 10 );

	pal::Window::Settings settings;
	settings
	    .setWidth( app->testImage.pixelsInfo.width )   // scale window to image dimensions
	    .setHeight( app->testImage.pixelsInfo.height ) // scale window to image dimensions
	    .setTitle( "Hello Mipmap" );

	// Create a new window
	app->window.setup( settings );

	le_swapchain_vk_settings_t swapchainSettings;
	swapchainSettings.presentmode_hint = le::Swapchain::Presentmode::eImmediate;
	swapchainSettings.imagecount_hint  = 3;

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );
	backendCreateInfo.swapchain_settings  = &swapchainSettings;
	backendCreateInfo.pWindow             = app->window;

	app->backend.setup( &backendCreateInfo );
	app->renderer.setup( app->backend );

	return app;
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

// ----------------------------------------------------------------------

static bool pass_resource_setup( le_renderpass_o *pRp, void *user_data ) {

	auto rp  = le::RenderPassRef{pRp};
	auto app = static_cast<test_mipmaps_app_o *>( user_data );

	rp.useResource( app->testImage.imageHandle, app->testImage.imageInfo );
	return true;
}

// ----------------------------------------------------------------------

static void pass_resource_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_mipmaps_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	if ( false == app->testImage.wasLoaded ) {

		using namespace le_pixels;
		auto pixelsData = le_pixels_i.get_data( app->testImage.pixels );

		encoder.writeToImage( app->testImage.imageHandle,
		                      app->testImage.imageInfo,
		                      pixelsData,
		                      app->testImage.pixelsInfo.byte_count );

		le_pixels_i.destroy( app->testImage.pixels ); // Free pixels memory
		app->testImage.pixels = nullptr;              // Mark pixels memory as freed, otherwise Image.destroy() will double-free!

		app->testImage.wasLoaded = true;
	}
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPassRef{pRp};
	auto app = static_cast<test_mipmaps_app_o *>( user_data );

	LeTextureInfo texTest;
	texTest.imageView.imageId    = app->testImage.imageHandle;
	texTest.sampler.magFilter    = le::Filter::eNearest;
	texTest.sampler.minFilter    = le::Filter::eNearest;
	texTest.sampler.addressModeU = le::SamplerAddressMode::eMirroredRepeat;
	texTest.sampler.addressModeV = le::SamplerAddressMode::eMirroredRepeat;
	texTest.sampler.maxLod       = app->testImage.imageInfo.image.mipLevels;
	texTest.sampler.minLod       = 0;
	texTest.sampler.mipLodBias   = app->lodBias;

	rp
	    .addColorAttachment( app->renderer.getBackbufferResource() ) // color attachment
	    .sampleTexture( app->testImage.textureHandle, texTest )
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_mipmaps_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto screenWidth  = app->window.getSurfaceWidth();
	auto screenHeight = app->window.getSurfaceHeight();

	le::Viewport viewports[ 1 ] = {
	    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
	};

	le::Rect2D scissors[ 1 ] = {
	    {0, 0, screenWidth, screenHeight},
	};

	// Draw main scene
	if ( true ) {

		static auto shaderVert = app->renderer.createShaderModule( "./resources/shaders/fullscreenQuad.vert", le::ShaderStage::eVertex );
		static auto shaderFrag = app->renderer.createShaderModule( "./resources/shaders/fullscreenQuad.frag", le::ShaderStage::eFragment );

		static auto pipelineTriangle =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( shaderVert )
		        .addShaderStage( shaderFrag )
		        .build();

		encoder
		    .bindGraphicsPipeline( pipelineTriangle )
		    .setScissors( 0, 1, scissors )
		    .setViewports( 0, 1, viewports )
		    .setArgumentTexture( LE_ARGUMENT_NAME( src_tex_unit_0 ), app->testImage.textureHandle )
		    .draw( 4 ) //
		    ;
	}
}

// ----------------------------------------------------------------------
// Query UI events from window, and process them in sequence.
//
// Currently only sets the lodBias based on the mouse cursor's y position.
static void process_events( test_mipmaps_app_o *self ) {

	{
		le::UiEvent const *events;
		uint32_t           numEvents;
		self->window.getUIEventQueue( &events, numEvents );
		auto const events_end = events + numEvents;

		float maxY = float( self->window.getSurfaceHeight() );

		for ( auto e = events; e != events_end; e++ ) {

			switch ( e->event ) {
			case le::UiEvent::Type::eCursorPosition: {
				auto &event = e->cursorPosition;

				// Map cursor y position to lod
				float normalisedCursorPosition = float( event.y ) / maxY;
				self->lodBias                  = normalisedCursorPosition * self->testImage.imageInfo.image.mipLevels;

			} break;
			default:
			    break;
			}
		}
	}
}

// ----------------------------------------------------------------------

static bool test_mipmaps_app_update( test_mipmaps_app_o *self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	process_events( self );

	le::RenderModule mainModule{};
	{

		le::RenderPass renderPassTransfer( "transfer", LE_RENDER_PASS_TYPE_TRANSFER );
		renderPassTransfer.setSetupCallback( self, pass_resource_setup );
		renderPassTransfer.setExecuteCallback( self, pass_resource_exec );

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal.setSetupCallback( self, pass_main_setup );
		renderPassFinal.setExecuteCallback( self, pass_main_exec );

		mainModule.addRenderPass( renderPassTransfer );
		mainModule.addRenderPass( renderPassFinal );
	}

	self->renderer.update( mainModule );

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_mipmaps_app_destroy( test_mipmaps_app_o *self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_mipmaps_app_api( void *api ) {
	auto  test_mipmaps_app_api_i = static_cast<test_mipmaps_app_api *>( api );
	auto &test_mipmaps_app_i     = test_mipmaps_app_api_i->test_mipmaps_app_i;

	test_mipmaps_app_i.initialize = initialize;
	test_mipmaps_app_i.terminate  = terminate;

	test_mipmaps_app_i.create  = test_mipmaps_app_create;
	test_mipmaps_app_i.destroy = test_mipmaps_app_destroy;
	test_mipmaps_app_i.update  = test_mipmaps_app_update;
}
