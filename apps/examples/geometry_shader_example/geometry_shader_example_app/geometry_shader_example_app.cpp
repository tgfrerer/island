#include "geometry_shader_example_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_ui_event/le_ui_event.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct le_mouse_event_data_o {
	uint32_t  buttonState{};
	glm::vec2 cursor_pos;
};

struct geometry_shader_example_app_o {
	le::Backend  backend;
	pal::Window  window;
	le::Renderer renderer;

	LeCameraController cameraController;
	LeCamera           camera;
};

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( geometry_shader_example_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static geometry_shader_example_app_o *geometry_shader_example_app_create() {
	auto app = new ( geometry_shader_example_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1920 / 2 )
	    .setHeight( 1080 / 2 )
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

	reset_camera( app ); // set up the camera

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( geometry_shader_example_app_o *self ) {
	self->camera.setViewport( {0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPassRef{pRp};
	auto app = static_cast<geometry_shader_example_app_o *>( user_data );

	rp
	    .addColorAttachment( app->renderer.getBackbufferResource() ) // color attachment
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<geometry_shader_example_app_o *>( user_data );
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

	struct MatrixStackUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene
	if ( true ) {

		static auto pipelineGeometryShaderExamples =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/geometry_shader_example.vert", le::ShaderStage::eVertex ) )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/geometry_shader_example.frag", le::ShaderStage::eFragment ) )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/geometry_shader_example.geom", le::ShaderStage::eGeometry ) )
		        .withRasterizationState()
		        .setPolygonMode( le::PolygonMode::eFill )
		        .setCullMode( le::CullModeFlagBits::eNone )
		        .end()
		        .withInputAssemblyState()
		        .setToplogy( le::PrimitiveTopology::ePointList )
		        .end()
		        .withAttachmentBlendState( 0 )
		        .usePreset( le::AttachmentBlendPreset::eAdd )
		        .end()
		        .build();

		static auto pipelineDefault =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/default.vert", le::ShaderStage::eVertex ) )
		        .addShaderStage( app->renderer.createShaderModule( "./local_resources/shaders/default.frag", le::ShaderStage::eFragment ) )
		        .withRasterizationState()
		        .setPolygonMode( le::PolygonMode::eLine )
		        .end()
		        .withInputAssemblyState()
		        .setToplogy( le::PrimitiveTopology::eTriangleStrip )
		        .end()
		        .build();

		MatrixStackUbo_t mvp;

		mvp.model = glm::mat4( 1.f ); // identity matrix
		mvp.model = glm::scale( mvp.model, glm::vec3( 1 ) );
		//		mvp.model      = glm::translate( mvp.model, glm::vec3( 100.f, -100.f, -50.f ) );
		mvp.view       = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );
		mvp.projection = reinterpret_cast<glm::mat4 const &>( *app->camera.getProjectionMatrix() );

		glm::vec3 trianglePositions[] = {
		    {-50, 50, 0},
		    {-50, -50, 0},
		    {50, 50, 0},
		    {50, -50, 0},
		};

		constexpr float size_scale      = 0.25;
		glm::vec4       geometry_shader_exampleData[] = {
		    {3, 0.0, 0.0, 400 * size_scale}, //< flare point
		    {0, 0.1, 0.1, 200 * size_scale},
		    {0, 0.9, 0.9, 120 * size_scale},
		    {0, 1.0, 1.0, 300 * size_scale},
		    {0, 1.2, 1.2, 120 * size_scale},
		    {0, 1.5, 1.5, 30 * size_scale},
		    {1, 0.3, 0.3, 650 * size_scale},
		    {1, 0.5, 0.5, 300 * size_scale}, ///< screen centre
		    {1, 1.1, 1.1, 1300 * size_scale},
		    {1, 2.5, 2.5, 2300 * size_scale},
		    {2, 1.0, 1.0, 500 * size_scale},
		    {2, 1.0, 1.1, 400 * size_scale},
		    {2, 1.0, 1.2, 400 * size_scale},
		    {2, 1.0, 1.5, 500 * size_scale},
		    {2, 1.0, 2.5, 400 * size_scale},
		};

		struct GeometryShaderExampleParams {
			// uCanvas:
			// .x -> global canvas height (in pixels)
			// .y -> global canvas width (in pixels)
			// .z -> identity distance, that is the distance at which canvas is rendered 1:1
			__attribute__( ( aligned( 16 ) ) ) glm::vec3 uCanvas;
			__attribute__( ( aligned( 16 ) ) ) glm::vec3 uGeometryShaderExampleSource; ///< source of flare in screen space
			float                                        uHowClose;
		};

		glm::vec4 sourceInCameraSpace = mvp.view * glm::vec4{0, 0, -1000, 1.f};
		glm::vec4 sourceInClipSpace   = mvp.projection * sourceInCameraSpace;
		sourceInClipSpace             = sourceInClipSpace / sourceInClipSpace.w; // Normalise

		bool inFrustum = app->camera.getSphereCentreInFrustum( &sourceInCameraSpace.x, 500 );

		//		std::cout << "Clip space: " << glm::to_string( sourceInClipSpace ) << ", Camera Space: " << glm::to_string( sourceInCameraSpace ) << ", " << ( inFrustum ? "PASS" : "FAIL" )
		//		          << std::endl
		//		          << std::flush;

		GeometryShaderExampleParams params{};
		params.uCanvas.x        = screenWidth;
		params.uCanvas.y        = screenHeight;
		params.uCanvas.z        = app->camera.getUnitDistance();
		params.uGeometryShaderExampleSource = sourceInClipSpace;
		params.uHowClose        = 500;

		encoder
		    .setScissors( 0, 1, scissors )
		    .setViewports( 0, 1, viewports )
		    .bindGraphicsPipeline( pipelineDefault )
		    .setVertexData( trianglePositions, sizeof( trianglePositions ), 0 )
		    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
		    .draw( 4 ) //
		    ;

		// let' check if source is in clip space

		if ( inFrustum )
			encoder
			    .bindGraphicsPipeline( pipelineGeometryShaderExamples )
			    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
			    .setArgumentData( LE_ARGUMENT_NAME( "GeometryShaderExampleParams" ), &params, sizeof( GeometryShaderExampleParams ) )
			    .setVertexData( geometry_shader_exampleData, sizeof( geometry_shader_exampleData ), 0 )
			    .draw( sizeof( geometry_shader_exampleData ) / sizeof( glm::vec4 ) ) //
			    ;
	}
}

// ----------------------------------------------------------------------

static void geometry_shader_example_app_process_ui_events( geometry_shader_example_app_o *self ) {
	using namespace pal_window;
	uint32_t           numEvents;
	le::UiEvent const *pEvents;

	self->cameraController.setControlRect( 0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ) );
	self->window.getUIEventQueue( &pEvents, numEvents );
	self->cameraController.processEvents( self->camera, pEvents, numEvents );
}

// ----------------------------------------------------------------------

static bool geometry_shader_example_app_update( geometry_shader_example_app_o *self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	geometry_shader_example_app_process_ui_events( self );

	static bool resetCameraOnReload = false; // reload meand module reload
	if ( resetCameraOnReload ) {
		// Reset camera
		reset_camera( self );
		resetCameraOnReload = false;
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

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void geometry_shader_example_app_destroy( geometry_shader_example_app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_geometry_shader_example_app_api( void *api ) {
	auto  geometry_shader_example_app_api_i = static_cast<geometry_shader_example_app_api *>( api );
	auto &geometry_shader_example_app_i     = geometry_shader_example_app_api_i->geometry_shader_example_app_i;

	geometry_shader_example_app_i.initialize = initialize;
	geometry_shader_example_app_i.terminate  = terminate;

	geometry_shader_example_app_i.create  = geometry_shader_example_app_create;
	geometry_shader_example_app_i.destroy = geometry_shader_example_app_destroy;
	geometry_shader_example_app_i.update  = geometry_shader_example_app_update;
}
