#include "test_dependency.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include <vulkan/vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>

struct test_dependency_o {
	le::Backend  backend;
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

static void reset_camera( test_dependency_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static test_dependency_o *test_dependency_create() {
	auto app = new ( test_dependency_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window.setup( settings );

	le_swapchain_vk_settings_t swapchainSettings;
	swapchainSettings.presentmode_hint = le::Swapchain::Presentmode::eFifo;
	swapchainSettings.imagecount_hint  = 3;

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );
	backendCreateInfo.swapchain_settings  = &swapchainSettings;
	backendCreateInfo.pWindow             = app->window;

	app->backend.setup( &backendCreateInfo );

	app->renderer.setup( app->backend );

	// -- Declare graphics pipeline state objects

	{
		// set up the camera
		reset_camera( app );
	}

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( test_dependency_o *self ) {
	auto swapchainExtent = self->renderer.getSwapchainExtent();
	self->camera.setViewport( {0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

static bool pass_two_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp = le::RenderPass{pRp};

	LeTextureInfo texInfo{};
	texInfo.imageView.imageId = LE_IMG_RESOURCE( "dummy_image" );

	LeImageAttachmentInfo attachmentInfo{};
	attachmentInfo.clearValue.color = {{{0.f, 0.f, 1.f, 1.f}}};

	rp
	    .addColorAttachment( LE_IMG_RESOURCE( "two_output" ), attachmentInfo ) // color attachment 0
	    .sampleTexture( LE_TEX_RESOURCE( "dummy_texture" ), texInfo )          //
	    .addDepthStencilAttachment( LE_IMG_RESOURCE( "depthStencil" ) );

	return true;
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<test_dependency_o *>( user_data );

	LeTextureInfo texInfoTwo{};
	texInfoTwo.imageView.imageId = LE_IMG_RESOURCE( "two_output" );

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .sampleTexture( LE_IMG_RESOURCE( "dummy_texture_two" ), texInfoTwo )
	    .setIsRoot( true ) //
	    ;

	return true;
}

// ----------------------------------------------------------------------

static void pass_one_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
}

static void pass_two_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
}

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_dependency_o *>( user_data );
	le::Encoder encoder{encoder_};

	// data as it is laid out in the ubo for the shader
	struct ColorUbo_t {
		glm::vec4 color;
	};

	struct MatrixStackUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene
	if ( true ) {

		static auto shaderVert = app->renderer.createShaderModule( "./resources/shaders/default.vert", le::ShaderStage::eVertex );
		static auto shaderFrag = app->renderer.createShaderModule( "./resources/shaders/default.frag", le::ShaderStage::eFragment );

		static auto pipelineTriangle =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( shaderVert )
		        .addShaderStage( shaderFrag )
		        .build();

		MatrixStackUbo_t mvp;
		mvp.model      = glm::mat4( 1.f ); // identity matrix
		mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
		mvp.view       = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );
		mvp.projection = reinterpret_cast<glm::mat4 const &>( *app->camera.getProjectionMatrix() );

		glm::vec3 trianglePositions[] = {
		    {-50, -50, 0},
		    {50, -50, 0},
		    {0, 50, 0},
		};

		glm::vec4 triangleColors[] = {
		    {1, 0, 0, 1.f},
		    {0, 1, 0, 1.f},
		    {0, 0, 1, 1.f},
		};

		encoder
		    .bindGraphicsPipeline( pipelineTriangle )
		    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
		    .setVertexData( trianglePositions, sizeof( trianglePositions ), 0 )
		    .setVertexData( triangleColors, sizeof( triangleColors ), 1 )
		    .draw( 3 );
	}
}

// ----------------------------------------------------------------------

static bool test_dependency_update( test_dependency_o *self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	le::RenderModule mainModule{};
	{

		LeTextureInfo texInfo{};
		texInfo.imageView.imageId = LE_IMG_RESOURCE( "dummy_image" );

		LeImageAttachmentInfo attachmentInfo{};
		attachmentInfo.clearValue.color = {{{1.f, 0.f, 0.f, 1.f}}};

		auto renderpassOne = le::RenderPass( "one", LE_RENDER_PASS_TYPE_DRAW );
		renderpassOne
		    .addDepthStencilAttachment( LE_IMG_RESOURCE( "one_depth" ) )                                                                                        // color attachment 0
		    .addColorAttachment( LE_IMG_RESOURCE( "one_output" ), attachmentInfo, le::ImageInfoBuilder().setFormat( le::Format::eR32G32B32A32Sfloat ).build() ) // color attachment 1
		    .sampleTexture( LE_TEX_RESOURCE( "dummy_texture" ), texInfo )
		    .setIsRoot( true )
		    .setExecuteCallback( self, pass_one_exec );

		mainModule.addRenderPass( renderpassOne );
		mainModule.addRenderPass( le::RenderPass( "two", LE_RENDER_PASS_TYPE_DRAW, pass_two_setup, pass_two_exec, self ) );
		mainModule.addRenderPass( le::RenderPass( "main", LE_RENDER_PASS_TYPE_DRAW, pass_main_setup, pass_main_exec, self ) );
	}

	// Update will first call setup callbacks, then render callbacks in this module.
	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_dependency_destroy( test_dependency_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_dependency_api( void *api ) {
	auto  test_dependency_api_i = static_cast<test_dependency_api *>( api );
	auto &test_dependency_i     = test_dependency_api_i->test_dependency_i;

	test_dependency_i.initialize = initialize;
	test_dependency_i.terminate  = terminate;

	test_dependency_i.create  = test_dependency_create;
	test_dependency_i.destroy = test_dependency_destroy;
	test_dependency_i.update  = test_dependency_update;
}
