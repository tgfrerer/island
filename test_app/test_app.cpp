#include "test_app/test_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"
#include "le_renderer/private/hash_util.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <memory>

struct test_app_o {
	std::unique_ptr<le::Backend>         backend;
	std::unique_ptr<pal::Window>         window;
	std::unique_ptr<le::Renderer>        renderer;
	struct le_graphics_pipeline_state_o *testPSOHandle;
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
	auto obj = new ( test_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 640 )
	    .setHeight( 480 )
	    .setTitle( "Hello world" );

	obj->window = std::make_unique<pal::Window>( settings );

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );

	obj->backend = std::make_unique<le::Backend>( &backendCreateInfo );

	// We need a valid instance at this point.
	obj->backend->createWindowSurface( *obj->window );
	obj->backend->createSwapchain( nullptr ); // TODO (swapchain) - make it possible to set swapchain parameters

	obj->backend->setup();

	obj->renderer = std::make_unique<le::Renderer>( *obj->backend );
	obj->renderer->setup();

	{
		// -- Declare graphics pipeline state object

		// Creating shader modules will eventually compile shader source code from glsl to spir-v
		auto defaultVertShader = obj->renderer->createShaderModule( "default.frag.spv" );
		auto defaultFragShader = obj->renderer->createShaderModule( "default.vert.spv" );

		le_graphics_pipeline_create_info_t pi;
		pi.shader_module_frag = defaultFragShader;
		pi.shader_module_vert = defaultVertShader;

		// The pipeline state object holds all state for the pipeline,
		// that's links to shader modules, blend states, input assembly, etc...
		// Everything, in short, but the renderpass, and subpass (which are added in at the last minute)
		//
		// The backend pipeline object is compiled on-demand, when it is first used with a renderpass, and henceforth cached.
		auto pipelineStateHandle = obj->renderer->createGraphicsPipelineStateObject( &pi );

		if ( pipelineStateHandle ) {
			obj->testPSOHandle = pipelineStateHandle;
		} else {
			std::cerr << "declaring a pipeline failed miserably.";
		}
	}

	/*

	  Create resources here -
	  resources can be:
		transient   - this means they can be written to and used in the same frame, their lifetime is limited to frame lifetime.
		persistent  - this means they must be staged, first their data must be written to (mapped) scratch, then copied using the queue.

	*/

	return obj;
}

// ----------------------------------------------------------------------

static bool test_app_update( test_app_o *self ) {

	pal::Window::pollEvents();

	if ( self->window->shouldClose() ) {
		return false;
	}

	static_assert( const_char_hash64( "resource-image-testing" ) == RESOURCE_IMAGE_ID( "testing" ), "hashes must match" );
	static_assert( const_char_hash64( "resource-buffer-testing" ) == RESOURCE_BUFFER_ID( "testing" ), "hashes must match" );
	static_assert( RESOURCE_IMAGE_ID( "testing" ) != RESOURCE_BUFFER_ID( "testing" ), "buffer and image resources can't have same id based on same name" );

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

			//encoder.updateResource(RESOURCE_BUFFER_ID("debug-buffer"), ptr);
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

		renderPassFinal.setExecuteCallback( self, []( auto encoder_, auto user_data_ ) {
			auto                     self = static_cast<test_app_o *>( user_data_ );
			le::CommandBufferEncoder encoder{encoder_};
			le::Viewport             viewports[ 2 ] = {
			    {{50.f, 50.f, 100.f, 100.f, 0.f, 1.f}},
			    {{200.f, 50.f, 200.f, 200.f, 0.f, 1.f}},
			};

			le::Rect2D scissors[ 2 ] = {
			    {{50, 50, 100, 100}},
			    {{200, 50, 200, 200}},
			};

			struct vec4 {
				float x = 0;
				float y = 0;
				float z = 0;
				float w = 0;
			};

			vec4 vertData[ 3 ] = {{0, 0, 0, 0}, {2, 0, 0, 0}, {0, 2, 0, 0}};

			static_assert( sizeof( vertData ) == sizeof( float ) * 4 * 3, "vertData must be tightly packed" );

			// TODO (pipeline): implement binding graphics pipeline
			// encoder.bind_graphics_pipeline( self->default_pipeline );

			// This will use the scratch buffer -- and the encoded command will store the
			// location of the data as it was laid down in the scratch buffer.

			// vertex data must be stored to GPU mapped memory using an allocator through encoder first,
			// will then be available to the gpu.

			// The scratch buffer is uploaded/transferred before the renderpass begins
			// so that data from it is read-visible
			encoder.setVertexData( vertData, sizeof( vertData ), 0 );

			encoder.setScissor( 0, 1, scissors );
			encoder.setViewport( 0, 1, viewports );

			encoder.draw( 3, 1, 0, 0 );

			encoder.setScissor( 0, 1, &scissors[ 1 ] );
			encoder.setViewport( 0, 1, &viewports[ 1 ] );

			encoder.draw( 3, 1, 0, 0 );
		} );

		mainModule.addRenderPass( resourcePass );
		mainModule.addRenderPass( renderPassFinal );
	}

	// update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer->update( mainModule );

	return true;
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
