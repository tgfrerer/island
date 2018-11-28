#include "triangle_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_pixels/le_pixels.h"

#include "le_ui_event/le_ui_event.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "gtx/easing.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <bitset>
#include <vector>

#include <chrono> // for nanotime
using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct triangle_app_o {
	le::Backend  backend;
	pal::Window  window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;
	float        deltaTimeSec  = 0;
	float        animT         = 0;

	NanoTime update_start_time;

	// Note we use the c++ facade for resource handles as this guarantees that resource
	// handles are initialised to nullptr, otherwise this is too easy to forget...

	le_shader_module_o *shaderTriangle[ 2 ]{};
	le_shader_module_o *shaderPathTracer[ 2 ]{};

	// NOTE: RUNTIME-COMPILE : If you add any new things during run-time, make sure to only add at the end of the object,
	// otherwise all pointers above will be invalidated. this might also overwrite memory which
	// is stored after this object, which is very subtle in introducing errors. We need to think about a way of serializing
	// and de-serializing objects which are allocated on the heap. we don't have to worry about objects which are allocated
	// on the stack, as the stack acts like a pool allocator, and they are only alife while control visits the code section
	// in question.

	LeCamera           camera;
	LeCameraController cameraController;
};

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( triangle_app_o *self ); // ffdecl.
// ----------------------------------------------------------------------

static triangle_app_o *triangle_app_create() {
	auto app = new ( triangle_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window.setup( settings );

	le_swapchain_vk_settings_t swapchainSettings;
	swapchainSettings.presentmode_hint = le::Swapchain::Presentmode::eImmediate;

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );
	backendCreateInfo.pWindow             = app->window;
	backendCreateInfo.swapchain_settings  = &swapchainSettings;

	// initialise the backend
	app->backend.setup( &backendCreateInfo );

	// initialise the renderer
	app->renderer.setup( app->backend );

	// -- Declare graphics pipeline state objects

	{
		// create shader objects

		app->shaderTriangle[ 0 ] = app->renderer.createShaderModule( "./resources/shaders/quad_bezier.vert", le::ShaderStage::eVertex );
		app->shaderTriangle[ 1 ] = app->renderer.createShaderModule( "./resources/shaders/quad_bezier.frag", le::ShaderStage::eFragment );

		app->shaderPathTracer[ 0 ] = app->renderer.createShaderModule( "./resources/shaders/path_tracer.vert", le::ShaderStage::eVertex );
		app->shaderPathTracer[ 1 ] = app->renderer.createShaderModule( "./resources/shaders/path_tracer.frag", le::ShaderStage::eFragment );
	}

	app->update_start_time = std::chrono::high_resolution_clock::now();

	{

		// set up the camera
		reset_camera( app );
	}
	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( triangle_app_o *self ) {
	self->camera.setViewport( {0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

static bool pass_resource_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp = le::RenderPassRef{pRp};

	rp.useResource( LE_BUF_RESOURCE( "TriangleBuffer" ),
	                le::BufferInfoBuilder()
	                    .setSize( sizeof( glm::vec3 ) * 6 )
	                    .build() // create resource for triangle vertex buffer
	);

	return true;
}

// ----------------------------------------------------------------------

static void pass_resource_exec( le_command_buffer_encoder_o *encoder, void *user_data ) {
	using namespace le_renderer;

	// upload triangle data
	glm::vec3 trianglePositions[ 6 ] = {
	    {0, -50, 0},
	    {0, 25, 0},
	    {100, 50, 0},
	    {00, -50, 0},
	    {00, 25, 0},
	    {-100, 50, 0},
	};

	encoder_i.write_to_buffer( encoder, LE_BUF_RESOURCE( "TriangleBuffer" ), 0, trianglePositions, sizeof( trianglePositions ) );
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPassRef{pRp};
	auto app = static_cast<triangle_app_o *>( user_data );

	rp
	    .addColorAttachment( app->renderer.getBackbufferResource() ) // color attachment
	    .addDepthStencilAttachment( LE_IMG_RESOURCE( "ImgDepth" ) )  // depth attachment
	    .useResource( LE_BUF_RESOURCE( "TriangleBuffer" ),
	                  le::BufferInfoBuilder()
	                      .setSize( sizeof( glm::vec3 ) * 6 )
	                      .setUsageFlags( LE_BUFFER_USAGE_VERTEX_BUFFER_BIT )
	                      .build() )
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<triangle_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto screenWidth  = app->window.getSurfaceWidth();
	auto screenHeight = app->window.getSurfaceHeight();

	le::Viewport viewports[ 3 ] = {
	    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
	    {10.f, 10.f, 160.f * 3.f + 10.f, 106.f * 3.f + 10.f, 0.f, 1.f},
	    {10.f, 10.f, 640 / 5, 425 / 5, 0.f, 1.f},
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 3 ] = {
	    {0, 0, screenWidth, screenHeight},
	    {10, 10, 160 * 3 + 10, 106 * 3 + 10},
	    {10, 10, 640 / 5, 425 / 5},
	};

	// data as it is laid out in the ubo for the shader
	struct ColorUbo_t {
		glm::vec4 color;
	};

	struct MatrixStackUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// NOTE: We must align to multiples of 16, as per std140 layout,
	// which *must* be observed for uniform blocks.
	// See: <https://www.khronos.org/registry/vulkan/specs/1.0-wsi_extensions/html/vkspec.html#interfaces-resources-layout>
	//
	struct RayInfo_t {
		__attribute__( ( aligned( 16 ) ) ) glm::vec3 rayTL;
		__attribute__( ( aligned( 16 ) ) ) glm::vec3 rayTR;
		__attribute__( ( aligned( 16 ) ) ) glm::vec3 rayBL;
		__attribute__( ( aligned( 16 ) ) ) glm::vec3 rayBR;
		__attribute__( ( aligned( 16 ) ) ) glm::vec3 eye;
		__attribute__( ( aligned( 16 ) ) ) glm::vec2 clipNearFar;
	};

	app->animT       = fmodf( app->animT + app->deltaTimeSec, 10.f );
	float r_val      = app->animT / 10.f;
	float r_anim_val = glm::elasticEaseOut( r_val );

	// Draw main scene
	if ( true ) {

		static auto pipelineTriangle =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( app->shaderTriangle[ 0 ] )
		        .addShaderStage( app->shaderTriangle[ 1 ] )
		        .build();

		static auto pipelinePathTracer =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( app->shaderPathTracer[ 0 ] )
		        .addShaderStage( app->shaderPathTracer[ 1 ] )
		        .withRasterizationState()
		        .end()
		        .build();

		MatrixStackUbo_t mvp;
		mvp.model = glm::mat4( 1.f ); // identity matrix

		mvp.model      = glm::rotate( mvp.model, glm::radians( r_anim_val * 360 ), glm::vec3( 0, 1, 0 ) );
		mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
		mvp.view       = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );
		mvp.projection = *reinterpret_cast<glm::mat4 const *>( app->camera.getProjectionMatrix() );

		RayInfo_t rayInfo{};
		{

			glm::mat4 viewMatrixInverse       = glm::inverse( mvp.view );
			glm::mat4 projectionMatrixInverse = glm::inverse( mvp.projection );

			glm::vec4 cameraOrigin = viewMatrixInverse * glm::vec4( 0, 0, 0, 1 );

			// We need to transform the near plane of the unit cube from clip space to
			// world space.

			std::array<glm::vec4, 4> nearPlane;

			nearPlane[ 0 ] = glm::vec4( -1, +1, 0, 1 ); // TL
			nearPlane[ 1 ] = glm::vec4( +1, +1, 0, 1 ); // TR
			nearPlane[ 2 ] = glm::vec4( -1, -1, 0, 1 ); // BL
			nearPlane[ 3 ] = glm::vec4( +1, -1, 0, 1 ); // BR

			for ( auto &p : nearPlane ) {
				// transform nearPlane edge ray into view space
				glm::vec4 a;
				a = p = projectionMatrixInverse * p;
				// undo perspective division (unproject)
				a = p = p / p.w;
				// transform nearPlane into world space
				a = p = viewMatrixInverse * p;
				// create ray by subtracting: endpoint - origin
				a = p = p - cameraOrigin;
				// now let's normalise that.
				a = p = glm::normalize( p );
			}

			app->camera.getClipDistances( &rayInfo.clipNearFar.x, &rayInfo.clipNearFar.y );
			rayInfo.eye   = cameraOrigin;
			rayInfo.rayTL = nearPlane[ 0 ];
			rayInfo.rayTR = nearPlane[ 1 ];
			rayInfo.rayBL = nearPlane[ 2 ];
			rayInfo.rayBR = nearPlane[ 3 ];
		}

		le_resource_handle_t buffers[] = {LE_BUF_RESOURCE( "TriangleBuffer" )};
		uint64_t             offsets[] = {0};

		ColorUbo_t color{{1, 1, 1, 1}};

		glm::vec4 triangleColors[] = {
		    {1, 0, 0, 1.f},
		    {0, 1, 0, 1.f},
		    {0, 0, 1, 1.f},
		    {1, 0, 0, 1.f},
		    {0, 1, 0, 1.f},
		    {0, 0, 1, 1.f},
		};

		uint16_t indexData[] = {0, 1, 2, 3, 4, 5};

		if ( true ) {
			encoder
			    .bindGraphicsPipeline( pipelineTriangle )
			    .setScissors( 0, 1, scissors )
			    .setViewports( 0, 1, viewports )
			    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
			    .setArgumentData( LE_ARGUMENT_NAME( "Color" ), &color, sizeof( ColorUbo_t ) )
			    .bindVertexBuffers( 0, 1, buffers, offsets )
			    .setVertexData( triangleColors, sizeof( triangleColors ), 1 )
			    .setIndexData( indexData, sizeof( indexData ) )
			    .drawIndexed( 6, 100 );
		}

		if ( false ) {
			// note that this draws a full screen quad.
			encoder
			    .bindGraphicsPipeline( pipelinePathTracer )
			    .setScissors( 0, 1, scissors )
			    .setViewports( 0, 1, viewports )
			    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
			    .setArgumentData( LE_ARGUMENT_NAME( "RayInfo" ), &rayInfo, sizeof( RayInfo_t ) )
			    .draw( 3 );
		}
	}
}

static void process_ui_events( triangle_app_o *self ) {

	le::UiEvent const *pEvents;
	uint32_t           eventCount = 0;
	self->window.getUIEventQueue( &pEvents, eventCount );

	self->cameraController.processEvents( self->camera, pEvents, eventCount );
}

// ----------------------------------------------------------------------

static bool triangle_app_update( triangle_app_o *self ) {

	static bool resetCameraOnReload = false;

	{
		// update frame delta time
		auto   current_time     = std::chrono::high_resolution_clock::now();
		double millis           = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>( current_time - self->update_start_time ).count();
		self->deltaTimeSec      = float( millis / 1000.0 );
		self->update_start_time = current_time;
	}

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	self->cameraController.setControlRect( 0, 0, float( self->window.getSurfaceWidth() ), float( self->window.getSurfaceHeight() ) );
	process_ui_events( self );

	if ( resetCameraOnReload ) {
		// Reset camera
		reset_camera( self );
		resetCameraOnReload = false;
	}

	using namespace le_renderer;

	le::RenderModule mainModule{};
	{
		le::RenderPass resourcePass( "resource copy", LE_RENDER_PASS_TYPE_TRANSFER );
		resourcePass.setSetupCallback( self, pass_resource_setup );
		resourcePass.setExecuteCallback( self, pass_resource_exec );

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal.setSetupCallback( self, pass_main_setup );
		renderPassFinal.setExecuteCallback( self, pass_main_exec );

		mainModule.addRenderPass( resourcePass );
		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void triangle_app_destroy( triangle_app_o *self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_triangle_app_api( void *api ) {
	auto  triangle_app_api_i = static_cast<triangle_app_api *>( api );
	auto &triangle_app_i     = triangle_app_api_i->triangle_app_i;

	triangle_app_i.initialize = initialize;
	triangle_app_i.terminate  = terminate;

	triangle_app_i.create  = triangle_app_create;
	triangle_app_i.destroy = triangle_app_destroy;
	triangle_app_i.update  = triangle_app_update;
}
