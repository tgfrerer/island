#include "test_img_swapchain_app.h"

#include "pal_window/pal_window.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>

struct test_img_swapchain_app_o {
	pal::Window  window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCamera camera;
};

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( test_img_swapchain_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static test_img_swapchain_app_o *test_img_swapchain_app_create() {
	auto app = new ( test_img_swapchain_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // TestImgSwapchainApp" );

	// create a new window
	app->window.setup( settings );

	auto rendererInfo = le::RendererInfoBuilder()

	                        .withSwapchain()
	                        .setHeightHint( 480 )
	                        .setWidthHint( 640 )
	                        .setFormatHint( le::Format::eR8G8B8A8Unorm )
	                        .setImagecountHint( 2 )
	                        .withImgSwapchain()
	                        .end()
	                        .end()
	                        .build();

	app->renderer.setup( rendererInfo );

	// set up the camera
	reset_camera( app );

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( test_img_swapchain_app_o *self ) {
	le::Extent2D surfaceExtent = self->renderer.getSwapchainExtent();
	self->camera.setViewport( {0, 0, float( surfaceExtent.width ), float( surfaceExtent.height ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<test_img_swapchain_app_o *>( user_data );

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_img_swapchain_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto &passExtent = encoder.getRenderpassExtent();

	//	std::cout << "screen dimensions: " << std::dec << passExtent.width << " x " << passExtent.height << std::endl
	//	          << std::flush;

	le::Viewport viewports[ 1 ] = {
	    {0.f, 0.f, float( passExtent.width ), float( passExtent.height ), 0.f, 1.f},
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 1 ] = {
	    {0, 0, passExtent.width, passExtent.height},
	};

	// data as it is laid out in the ubo for the shader
	struct MatrixStackUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene

	static auto shaderVert = app->renderer.createShaderModule( "./resources/shaders/default.vert", le::ShaderStage::eVertex );
	static auto shaderFrag = app->renderer.createShaderModule( "./resources/shaders/default.frag", le::ShaderStage::eFragment );

	static auto pipelineTestImgSwapchain =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .build();

	MatrixStackUbo_t mvp;
	mvp.model      = glm::mat4( 1.f ); // identity matrix
	mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
	mvp.model      = glm::rotate( mvp.model, glm::radians( float( app->frame_counter % 360 ) ), glm::vec3{0, 0, 1} );
	mvp.view       = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );
	mvp.projection = reinterpret_cast<glm::mat4 const &>( *app->camera.getProjectionMatrix() );

	glm::vec3 test_img_swapchainPositions[] = {
	    {-50, -50, 0},
	    {50, -50, 0},
	    {0, 50, 0},
	};

	glm::vec4 test_img_swapchainColors[] = {
	    {1, 0, 0, 1.f},
	    {0, 1, 0, 1.f},
	    {0, 0, 1, 1.f},
	};

	encoder
	    .bindGraphicsPipeline( pipelineTestImgSwapchain )
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports )
	    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
	    .setVertexData( test_img_swapchainPositions, sizeof( test_img_swapchainPositions ), 0 )
	    .setVertexData( test_img_swapchainColors, sizeof( test_img_swapchainColors ), 1 )
	    .draw( 3 );
}

// ----------------------------------------------------------------------

static bool test_img_swapchain_app_update( test_img_swapchain_app_o *self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.

	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	le::RenderModule mainModule{};
	{

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal.setSetupCallback( self, pass_main_setup );
		renderPassFinal.setExecuteCallback( self, pass_main_exec );

		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_img_swapchain_app_destroy( test_img_swapchain_app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_img_swapchain_app_api( void *api ) {
	auto  test_img_swapchain_app_api_i = static_cast<test_img_swapchain_app_api *>( api );
	auto &test_img_swapchain_app_i     = test_img_swapchain_app_api_i->test_img_swapchain_app_i;

	test_img_swapchain_app_i.initialize = initialize;
	test_img_swapchain_app_i.terminate  = terminate;

	test_img_swapchain_app_i.create  = test_img_swapchain_app_create;
	test_img_swapchain_app_i.destroy = test_img_swapchain_app_destroy;
	test_img_swapchain_app_i.update  = test_img_swapchain_app_update;
}
