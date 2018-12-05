#include "test_mesh_generator_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_ui_event/le_ui_event.h"

#include "le_mesh_generator/le_mesh_generator.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "glm/gtx/string_cast.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct le_mouse_event_data_o {
	uint32_t  buttonState{};
	glm::vec2 cursor_pos;
};

struct test_mesh_generator_app_o {
	le::Backend  backend;
	pal::Window  window;
	le::Renderer renderer;

	LeCameraController cameraController;
	LeCamera           camera;
	LeMeshGenerator    sphereGenerator;
};

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( test_mesh_generator_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static test_mesh_generator_app_o *test_mesh_generator_app_create() {
	auto app = new ( test_mesh_generator_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1920 / 2 )
	    .setHeight( 1080 / 2 )
	    .setTitle( "Island // TestMeshGeneratorApp" );

	// Create a new window
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

	reset_camera( app ); // set up the camera

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( test_mesh_generator_app_o *self ) {
	self->camera.setViewport( {0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<test_mesh_generator_app_o *>( user_data );

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_BUFFER" ) )
	    .setIsRoot( true ) //
	    ;

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_mesh_generator_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto screenWidth  = app->window.getSurfaceWidth();
	auto screenHeight = app->window.getSurfaceHeight();

	le::Viewport viewports[ 1 ] = {
	    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 1 ] = {
	    {0, 0, screenWidth, screenHeight},
	};

	// Draw main scene

	struct MVP_DefaultUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};
	MVP_DefaultUbo_t mvp;

	mvp.model      = glm::mat4( 1.f ); // identity matrix
	mvp.model      = glm::scale( mvp.model, glm::vec3( 1 ) );
	mvp.view       = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );
	mvp.projection = reinterpret_cast<glm::mat4 const &>( *app->camera.getProjectionMatrix() );

	// draw mesh

	static auto pipelineDefault =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/default.vert", le::ShaderStage::eVertex ) )
	        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/default.frag", le::ShaderStage::eFragment ) )
	        .withRasterizationState()
	        .setPolygonMode( le::PolygonMode::eLine )
	        //	        .setPolygonMode( le::PolygonMode::eFill )
	        .setCullMode( le::CullModeFlagBits::eBack )
	        .setFrontFace( le::FrontFace::eCounterClockwise )
	        .end()
	        .withInputAssemblyState()
	        .setToplogy( le::PrimitiveTopology::eTriangleList )
	        .end()
	        .withDepthStencilState()
	        .setDepthTestEnable( true )
	        .end()
	        .build();

	app->sphereGenerator.generateSphere( 100, 6, 4 );
	uint16_t *sphereIndices{};
	float *   sphereVertices{};
	float *   sphereNormals{};
	float *   sphereUvs{};
	size_t    numVertices{};
	size_t    numIndices{};
	app->sphereGenerator.getData( numVertices, numIndices, &sphereVertices, &sphereNormals, &sphereUvs, &sphereIndices );

	encoder
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports )
	    .bindGraphicsPipeline( pipelineDefault );

	encoder
	    .setVertexData( sphereVertices, numVertices * 3 * sizeof( float ), 0 )
	    .setVertexData( sphereNormals, numVertices * 3 * sizeof( float ), 1 )
	    .setVertexData( sphereUvs, numVertices * 2 * sizeof( float ), 2 )
	    .setIndexData( sphereIndices, numIndices * sizeof( uint16_t ) );

	encoder.setArgumentData( LE_ARGUMENT_NAME( "MVP_Default" ), &mvp, sizeof( MVP_DefaultUbo_t ) )
	    .drawIndexed( uint32_t( numIndices ) ) //
	    ;
}

// ----------------------------------------------------------------------

static void test_mesh_generator_app_process_ui_events( test_mesh_generator_app_o *self ) {
	using namespace pal_window;
	uint32_t           numEvents;
	le::UiEvent const *pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, numEvents );

	self->cameraController.setControlRect( 0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ) );
	self->cameraController.processEvents( self->camera, pEvents, numEvents );
}

// ----------------------------------------------------------------------

static bool test_mesh_generator_app_update( test_mesh_generator_app_o *self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// update interactive camera using mouse data
	test_mesh_generator_app_process_ui_events( self );

	static bool resetCameraOnReload = false; // reload meand module reload
	if ( resetCameraOnReload ) {
		// Reset camera
		reset_camera( self );
		resetCameraOnReload = false;
	}

	using namespace le_renderer;

	le::RenderModule mainModule{};
	{

		le::RenderPass renderPassFinal( "final-pass", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal.setSetupCallback( self, pass_main_setup );
		renderPassFinal.setExecuteCallback( self, pass_main_exec );

		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer.update( mainModule );

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_mesh_generator_app_destroy( test_mesh_generator_app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_mesh_generator_app_api( void *api ) {
	auto  test_mesh_generator_app_api_i = static_cast<test_mesh_generator_app_api *>( api );
	auto &test_mesh_generator_app_i     = test_mesh_generator_app_api_i->test_mesh_generator_app_i;

	test_mesh_generator_app_i.initialize = initialize;
	test_mesh_generator_app_i.terminate  = terminate;

	test_mesh_generator_app_i.create  = test_mesh_generator_app_create;
	test_mesh_generator_app_i.destroy = test_mesh_generator_app_destroy;
	test_mesh_generator_app_i.update  = test_mesh_generator_app_update;
}
