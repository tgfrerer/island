#include "triangle_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_pixels/le_pixels.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h> // for key codes

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

#define LE_ARGUMENT_NAME( x ) hash_64_fnv1a_const( #x )

#include <chrono> // for nanotime
using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct le_mouse_event_data_o {
	uint32_t  buttonState{};
	glm::vec2 cursor_pos;
};

struct triangle_app_o {
	std::unique_ptr<le::Backend>  backend;
	std::unique_ptr<pal::Window>  window;
	std::unique_ptr<le::Renderer> renderer;
	uint64_t                      frame_counter = 0;
	float                         deltaTimeSec  = 0;
	float                         animT         = 0;

	std::array<bool, 5>   mouseButtonStatus{}; // status for each mouse button
	glm::vec2             mousePos{};          // current mouse position
	le_mouse_event_data_o mouseData;

	NanoTime update_start_time;

	// Note we use the c++ facade for resource handles as this guarantees that resource
	// handles are initialised to nullptr, otherwise this is too easy to forget...
	le::ResourceHandle resImgDepth       = nullptr;
	le::ResourceHandle resBufTrianglePos = nullptr;

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
	app->window = std::make_unique<pal::Window>( settings );

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );

	app->backend = std::make_unique<le::Backend>( &backendCreateInfo );

	// We need a valid instance at this point.
	app->backend->createWindowSurface( *app->window );

	le_swapchain_vk_settings_o swapchainSettings;
	swapchainSettings.presentmode_hint = le::Swapchain::Presentmode::eImmediate;
	swapchainSettings.imagecount_hint  = 2;

	app->backend->createSwapchain( &swapchainSettings ); // TODO (swapchain) - make it possible to set swapchain parameters

	app->backend->setup();

	app->renderer = std::make_unique<le::Renderer>( *app->backend );
	app->renderer->setup();

	// -- Declare graphics pipeline state objects

	{
		// create shader objects

		app->shaderTriangle[ 0 ] = app->renderer->createShaderModule( "./resources/shaders/quad_bezier.vert", LeShaderType::eVert );
		app->shaderTriangle[ 1 ] = app->renderer->createShaderModule( "./resources/shaders/quad_bezier.frag", LeShaderType::eFrag );

		app->shaderPathTracer[ 0 ] = app->renderer->createShaderModule( "./resources/shaders/path_tracer.vert", LeShaderType::eVert );
		app->shaderPathTracer[ 1 ] = app->renderer->createShaderModule( "./resources/shaders/path_tracer.frag", LeShaderType::eFrag );
	}

	{
		// -- Set window event callbacks

		using namespace pal_window;
		// set the callback user data for all callbacks from window *app->window
		// to be our app pointer.
		window_i.set_callback_user_data( *app->window, app );

		using triangle_app::triangle_app_i;

		window_i.set_key_callback( *app->window, &triangle_app_i.key_callback );
		window_i.set_character_callback( *app->window, &triangle_app_i.character_callback );

		window_i.set_cursor_position_callback( *app->window, &triangle_app_i.cursor_position_callback );
		window_i.set_cursor_enter_callback( *app->window, &triangle_app_i.cursor_enter_callback );
		window_i.set_mouse_button_callback( *app->window, &triangle_app_i.mouse_button_callback );
		window_i.set_scroll_callback( *app->window, &triangle_app_i.scroll_callback );
	}

	app->update_start_time = std::chrono::high_resolution_clock::now();

	{
		// Declare resources which we will need for our scene

		app->resImgDepth       = app->renderer->declareResource( LeResourceType::eImage );
		app->resBufTrianglePos = app->renderer->declareResource( LeResourceType::eBuffer );
	}
	{
		// set up the camera
		reset_camera( app );
	}
	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( triangle_app_o *self ) {
	self->camera.setViewport( {0, 0, float( self->window->getSurfaceWidth() ), float( self->window->getSurfaceHeight() ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

static bool pass_resource_setup( le_renderpass_o *pRp, void *user_data ) {
	auto app = static_cast<triangle_app_o *>( user_data );
	auto rp  = le::RenderPassRef{pRp};

	{
		// create z-buffer image for main renderpass
		le_resource_info_t imgInfo{};
		imgInfo.type = LeResourceType::eImage;
		{
			auto &img         = imgInfo.image;
			img.format        = VK_FORMAT_D32_SFLOAT_S8_UINT;
			img.flags         = 0;
			img.arrayLayers   = 1;
			img.extent.width  = 0; // zero means size of backbuffer.
			img.extent.height = 0; // zero means size of backbuffer.
			img.extent.depth  = 1;
			img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			img.mipLevels     = 1;
			img.samples       = VK_SAMPLE_COUNT_1_BIT;
			img.imageType     = VK_IMAGE_TYPE_2D;
			img.tiling        = VK_IMAGE_TILING_OPTIMAL;
		}
		rp.createResource( app->resImgDepth, imgInfo );
	}

	{
		// create resource for triangle vertex buffer
		le_resource_info_t bufInfo{};
		bufInfo.type         = LeResourceType::eBuffer;
		bufInfo.buffer.size  = sizeof( glm::vec3 ) * 6;
		bufInfo.buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		rp.createResource( app->resBufTrianglePos, bufInfo );
		rp.useResource( app->resBufTrianglePos, LeAccessFlagBits::eLeAccessFlagBitWrite );
	}

	rp.setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_resource_exec( le_command_buffer_encoder_o *encoder, void *user_data ) {
	using namespace le_renderer;
	auto app = static_cast<triangle_app_o *>( user_data );

	// Writing is always to encoder scratch buffer memory because that's the only memory that
	// is HOST visible.
	//
	// Type of resource ownership decides whether
	// a copy is added to the queue that transfers from scratch memory
	// to GPU local memory.

	// upload triangle data
	glm::vec3 trianglePositions[ 6 ] = {
	    //	    	    {-50, -50, 0},
	    //	    	    {50, -50, 0},
	    //	    	    {0, 50, 0},
	    {0, -50, 0},
	    {0, 25, 0},
	    {100, 50, 0},
	    {00, -50, 0},
	    {00, 25, 0},
	    {-100, 50, 0},
	};

	encoder_i.write_to_buffer( encoder, app->resBufTrianglePos, 0, trianglePositions, sizeof( trianglePositions ) );
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPassRef{pRp};
	auto app = static_cast<triangle_app_o *>( user_data );

	rp
	    .addImageAttachment( app->renderer->getBackbufferResource() ) // color attachment
	    .addDepthImageAttachment( app->resImgDepth )                  // depth attachment
	    .useResource( app->resBufTrianglePos, LeAccessFlagBits::eLeAccessFlagBitRead )
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<triangle_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto screenWidth  = app->window->getSurfaceWidth();
	auto screenHeight = app->window->getSurfaceHeight();

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

		vk::PipelineRasterizationStateCreateInfo rasterizationState{};
		rasterizationState
		    .setDepthClampEnable( VK_FALSE )
		    .setRasterizerDiscardEnable( VK_FALSE )
		    .setPolygonMode( vk::PolygonMode::eFill )
		    .setCullMode( vk::CullModeFlagBits::eNone )
		    .setFrontFace( vk::FrontFace::eCounterClockwise )
		    .setDepthBiasEnable( VK_FALSE )
		    .setDepthBiasConstantFactor( 0.f )
		    .setDepthBiasClamp( 0.f )
		    .setDepthBiasSlopeFactor( 1.f )
		    .setLineWidth( 1.f );

		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
		inputAssemblyInfo.setTopology( vk::PrimitiveTopology::eTriangleList );

		static auto pipelineTriangle =
		    LeGraphicsPipelineBuilder( encoder.getPipelineCache() )
		        .setVertexShader( app->shaderTriangle[ 0 ] )
		        .setFragmentShader( app->shaderTriangle[ 1 ] )
		        .setRasterizationInfo( rasterizationState )
		        //		        .setInputAssemblyInfo( inputAssemblyInfo )
		        .build();

		static auto pipelinePathTracer =
		    LeGraphicsPipelineBuilder( encoder.getPipelineCache() )
		        .setVertexShader( app->shaderPathTracer[ 0 ] )
		        .setFragmentShader( app->shaderPathTracer[ 1 ] )
		        .setRasterizationInfo( rasterizationState )
		        .build();

		MatrixStackUbo_t mvp;
		mvp.model = glm::mat4( 1.f ); // identity matrix
		                              //mvp.model = glm::translate( mvp.model, glm::vec3( 0, 0, -100 ) );
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

		LeResourceHandle buffers[] = {app->resBufTrianglePos};
		uint64_t         offsets[] = {0};

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
			    .setArgumentData( LE_ARGUMENT_NAME( MatrixStack ), &mvp, sizeof( MatrixStackUbo_t ) )
			    .setArgumentData( LE_ARGUMENT_NAME( Color ), &color, sizeof( ColorUbo_t ) )
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
			    .setArgumentData( LE_ARGUMENT_NAME( MatrixStack ), &mvp, sizeof( MatrixStackUbo_t ) )
			    .setArgumentData( LE_ARGUMENT_NAME( RayInfo ), &rayInfo, sizeof( RayInfo_t ) )
			    .draw( 3 );
		}
	}
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

	if ( self->window->shouldClose() ) {
		return false;
	}

	{
		// update interactive camera using mouse data
		self->cameraController.setControlRect( 0, 0, float( self->window->getSurfaceWidth() ), float( self->window->getSurfaceHeight() ) );
		self->cameraController.updateCamera( self->camera, &self->mouseData );
	}

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
	self->renderer->update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void triangle_app_destroy( triangle_app_o *self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

static void triangle_app_key_callback( void *user_data, int key, int scancode, int action, int mods ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<triangle_app_o *>( user_data );

	using namespace pal_window;
	if ( key == GLFW_KEY_F11 && action == GLFW_RELEASE ) {
		window_i.toggle_fullscreen( *app->window );
	}
}
static void triangle_app_character_callback( void *user_data, unsigned int codepoint ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null
}
static void triangle_app_cursor_position_callback( void *user_data, double xpos, double ypos ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<triangle_app_o *>( user_data );

	app->mouseData.cursor_pos = {float( xpos ), float( ypos )};
	app->mousePos             = {float( xpos ), float( ypos )};
}
static void triangle_app_cursor_enter_callback( void *user_data, int entered ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null
}
static void triangle_app_mouse_button_callback( void *user_data, int button, int action, int mods ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<triangle_app_o *>( user_data );

	if ( button >= 0 && button < int( app->mouseButtonStatus.size() ) ) {
		app->mouseButtonStatus[ size_t( button ) ] = ( action == GLFW_PRESS );

		if ( action == GLFW_PRESS ) {
			app->mouseData.buttonState |= uint8_t( 1 << size_t( button ) );
		} else if ( action == GLFW_RELEASE ) {
			app->mouseData.buttonState &= uint8_t( 0 << size_t( button ) );
		}
	}
}
static void triangle_app_scroll_callback( void *user_data, double xoffset, double yoffset ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null
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

	triangle_app_i.key_callback             = triangle_app_key_callback;
	triangle_app_i.character_callback       = triangle_app_character_callback;
	triangle_app_i.cursor_position_callback = triangle_app_cursor_position_callback;
	triangle_app_i.cursor_enter_callback    = triangle_app_cursor_enter_callback;
	triangle_app_i.mouse_button_callback    = triangle_app_mouse_button_callback;
	triangle_app_i.scroll_callback          = triangle_app_scroll_callback;
}
