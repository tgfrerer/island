#include "test_app/test_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"
#include "le_renderer/private/hash_util.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_LEFT_HANDED       // vulkan uses left handed coordinate system
#include "libs/glm/glm/glm.hpp"
#include "libs/glm/glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>

struct le_graphics_pipeline_state_o; // owned by renderer

struct test_app_o {
	std::unique_ptr<le::Backend>  backend;
	std::unique_ptr<pal::Window>  window;
	std::unique_ptr<le::Renderer> renderer;
	le_graphics_pipeline_state_o *psoMain;           // owned by the renderer
	le_graphics_pipeline_state_o *psoFullScreenQuad; // owned by the renderer

	// NOTE: RUNTIME-COMPILE : If you add any new things during run-time, make sure to only add at the end of the object,
	// otherwise all pointers above will be invalidated. this might also overwrite memory which
	// is stored after this object, which is very subtle in introducing errors. We need to think about a way of serializing
	// and de-serializing objects which are allocated on the heap. we don't have to worry about objects which are allocated
	// on the stack, as the stack acts like a pool allocator, and they are only alife while control visits the code section
	// in question.
	uint64_t frame_counter = 0;
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

static test_app_o *test_app_create() {
	auto app = new ( test_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 640 )
	    .setHeight( 480 )
	    .setTitle( "Hello world" );

	app->window = std::make_unique<pal::Window>( settings );

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );

	app->backend = std::make_unique<le::Backend>( &backendCreateInfo );

	// We need a valid instance at this point.
	app->backend->createWindowSurface( *app->window );
	app->backend->createSwapchain( nullptr ); // TODO (swapchain) - make it possible to set swapchain parameters

	app->backend->setup();

	app->renderer = std::make_unique<le::Renderer>( *app->backend );
	app->renderer->setup();

	{
		// -- Declare graphics pipeline state objects

		// Creating shader modules will eventually compile shader source code from glsl to spir-v
		auto defaultVertShader        = app->renderer->createShaderModule( "./shaders/default.vert", LeShaderType::eVert );
		auto defaultFragShader        = app->renderer->createShaderModule( "./shaders/default.frag", LeShaderType::eFrag );
		auto fullScreenQuadVertShader = app->renderer->createShaderModule( "./shaders/fullscreenQuad.vert", LeShaderType::eVert );
		auto fullScreenQuadFragShader = app->renderer->createShaderModule( "./shaders/fullscreenQuad.frag", LeShaderType::eFrag );

		{
			// create default pipeline
			le_graphics_pipeline_create_info_t pi;
			pi.shader_module_frag = defaultFragShader;
			pi.shader_module_vert = defaultVertShader;

			// The pipeline state object holds all state for the pipeline,
			// that's links to shader modules, blend states, input assembly, etc...
			// Everything, in short, but the renderpass, and subpass (which are added at the last minute)
			//
			// The backend pipeline object is compiled on-demand, when it is first used with a renderpass, and henceforth cached.
			auto psoHandle = app->renderer->createGraphicsPipelineStateObject( &pi );

			if ( psoHandle ) {
				app->psoMain = psoHandle;
			} else {
				std::cerr << "declaring main pipeline failed miserably.";
			}
		}
		{
			le_graphics_pipeline_create_info_t pi;
			// create full screen quad pipeline
			pi.shader_module_vert = fullScreenQuadVertShader;
			pi.shader_module_frag = fullScreenQuadFragShader;
			auto psoHandle        = app->renderer->createGraphicsPipelineStateObject( &pi );

			if ( psoHandle ) {
				app->psoFullScreenQuad = psoHandle;
			} else {
				std::cerr << "declaring test pipeline failed miserably.";
			}
		}
	}

	static_assert( const_char_hash64( "resource-image-testing" ) == RESOURCE_IMAGE_ID( "testing" ), "hashes must match" );
	static_assert( const_char_hash64( "resource-buffer-testing" ) == RESOURCE_BUFFER_ID( "testing" ), "hashes must match" );
	static_assert( RESOURCE_IMAGE_ID( "testing" ) != RESOURCE_BUFFER_ID( "testing" ), "buffer and image resources can't have same id based on same name" );

	/*

	  Create resources here -
	  resources can be:
		transient   - this means they can be written to and used in the same frame, their lifetime is limited to frame lifetime.
		persistent  - this means they must be staged, first their data must be written to (mapped) scratch, then copied using the queue.

	*/

	return app;
}

static float get_image_plane_distance( const le::Viewport &viewport, float fovRadians ) {
	return viewport.height / ( 2.0f * tanf( fovRadians * 0.5f ) );
}

// ----------------------------------------------------------------------

static bool test_app_update( test_app_o *self ) {

	pal::Window::pollEvents();

	if ( self->window->shouldClose() ) {
		return false;
	}

	// grab interface for encoder so that it can be used in callbacks -
	// making it static allows it to be visible inside the callback context,
	// and it also ensures that the registry call only happens upon first retrieval.
	static auto const &le_encoder = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;

	le::RenderModule mainModule{};
	{

		le::RenderPass resourcePass( "resource copy", LE_RENDER_PASS_TYPE_TRANSFER );

		resourcePass.setSetupCallback( []( auto pRp ) -> bool {
			auto rp = le::RenderPassRef{pRp};

			le_renderer_api::ResourceInfo resourceInfo;
			resourceInfo.ownership = le_renderer_api::ResourceInfo::eFrameLocal;

			rp.createResource( RESOURCE_BUFFER_ID( "debug-buffer" ), resourceInfo );

			return true;
		} );

		resourcePass.setExecuteCallback( self, []( auto encoder_, auto user_data_ ) {
			auto self = static_cast<test_app_o *>( user_data_ );

			le::CommandBufferEncoder encoder{encoder_};

			//encoder.updateResource( RESOURCE_BUFFER_ID( "debug-buffer" ), ptr );
		} );

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );

		renderPassFinal.setSetupCallback( []( auto pRp ) -> bool {
			auto rp = le::RenderPassRef{pRp};

			le::ImageAttachmentInfo colorAttachmentInfo{};
			colorAttachmentInfo.format       = vk::Format::eB8G8R8A8Unorm; // TODO (swapchain): use swapchain image format programmatically
			colorAttachmentInfo.access_flags = le::AccessFlagBits::eWrite;
			colorAttachmentInfo.loadOp       = LE_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachmentInfo.storeOp      = LE_ATTACHMENT_STORE_OP_STORE;
			rp.addImageAttachment( RESOURCE_IMAGE_ID( "backbuffer" ), &colorAttachmentInfo );

			//rp.useResource( RESOURCE_BUFFER_ID( "debug-buffer" ), le::AccessFlagBits::eRead );
			rp.setIsRoot( true );

			return true;
		} );

		renderPassFinal.setExecuteCallback( self, []( auto encoder, auto user_data_ ) {
			auto self = static_cast<test_app_o *>( user_data_ );

			auto screenWidth  = self->window->getSurfaceWidth();
			auto screenHeight = self->window->getSurfaceHeight();

			le::Viewport viewports[ 2 ] = {
			    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
			    {10.f, 10.f, 200.f, 200.f, 0.f, 1.f},
			};

			le::Rect2D scissors[ 2 ] = {
			    {0, 0, screenWidth, screenHeight},
			    {10, 10, 200, 200},
			};

			glm::vec3 fullScreenQuadData[ 3 ] = {
			    {0, 0, 0},
			    {2, 0, 0},
			    {0, 2, 0},
			};

			glm::vec3 triangleData[ 3 ] = {
			    {-50, -50, 0},
			    {50, -50, 0},
			    {0, 50, 0},
			};

			uint16_t indexData[ 3 ] = {0, 1, 2};

			// data as it is laid out in the ubo for the shader
			struct ColorUbo_t {
				glm::vec4 color;
			};

			struct MatrixStackUbo_t {
				glm::mat4 modelMatrix;
				glm::mat4 viewMatrix;
				glm::mat4 projectionMatrix;
			};

			float r_val = ( self->frame_counter % 120 ) / 120.f;

			ColorUbo_t ubo1{{1, 0, 0, 1}};

			// Bind full main graphics pipeline
			le_encoder.bind_graphics_pipeline( encoder, self->psoMain );

			le_encoder.set_scissor( encoder, 0, 1, scissors );
			le_encoder.set_viewport( encoder, 0, 1, viewports );

			MatrixStackUbo_t matrixStack;
			matrixStack.projectionMatrix = glm::perspective( glm::radians( 60.f ), float( screenWidth ) / float( screenHeight ), 0.01f, 1000.f );
			matrixStack.modelMatrix      = glm::mat4( 1.f ); // identity matrix
			matrixStack.modelMatrix      = glm::scale( matrixStack.modelMatrix, glm::vec3( 2 ) );

			//			matrixStack.modelMatrix      = glm::rotate( matrixStack.modelMatrix, glm::radians( r_val * 360 ), glm::vec3( 0, 0, 1 ) );

			float normDistance     = get_image_plane_distance( viewports[ 0 ], glm::radians( 60.f ) ); // calculate unit distance
			matrixStack.viewMatrix = glm::lookAt( glm::vec3( 0, 0, normDistance ), glm::vec3( 0 ), glm::vec3( 0, -1, 0 ) );

			le_encoder.set_argument_ubo_data( encoder, const_char_hash64( "MatrixStack" ), &matrixStack, sizeof( MatrixStackUbo_t ) ); // set a descriptor to set, binding, array_index
			le_encoder.set_argument_ubo_data( encoder, const_char_hash64( "Color" ), &ubo1, sizeof( ColorUbo_t ) );                    // set a descriptor to set, binding, array_index

			le_encoder.set_vertex_data( encoder, triangleData, sizeof( glm::vec3 ) * 3, 0 );
			le_encoder.set_index_data( encoder, indexData, sizeof( indexData ), 0 ); // 0 for indexType means uint16_t
			le_encoder.draw_indexed( encoder, 3, 1, 0, 0, 0 );

			// Bind full screen quad pipeline
			le_encoder.bind_graphics_pipeline( encoder, self->psoFullScreenQuad );
			le_encoder.set_scissor( encoder, 0, 1, &scissors[ 1 ] );
			le_encoder.set_viewport( encoder, 0, 1, &viewports[ 1 ] );
			le_encoder.draw( encoder, 3, 1, 0, 0 );
		} );

		mainModule.addRenderPass( resourcePass );
		mainModule.addRenderPass( renderPassFinal );
	}

	// update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer->update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_app_destroy( test_app_o *self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

void register_test_app_api( void *api_ ) {
	auto  test_app_api_i = static_cast<test_app_api *>( api_ );
	auto &test_app_i     = test_app_api_i->test_app_i;

	test_app_i.initialize = initialize;
	test_app_i.terminate  = terminate;

	test_app_i.create  = test_app_create;
	test_app_i.destroy = test_app_destroy;
	test_app_i.update  = test_app_update;
}
