#include "test_app/test_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "vulkan/vulkan.hpp"
#include <iostream>
#include <memory>

struct test_app_o {
	std::unique_ptr<le::Backend> backend;
	std::unique_ptr<pal::Window> window;
	std::unique_ptr<le::Renderer> renderer;
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
	    .setWidth ( 640 )
	    .setHeight( 480 )
	    .setTitle ( "Hello world" )
	    ;

	obj->window = std::make_unique<pal::Window>(settings);

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions =  pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );

	obj->backend = std::make_unique<le::Backend>(&backendCreateInfo);

	// we need a valid instance at this point.
	obj->backend->createWindowSurface(*obj->window);
	obj->backend->createSwapchain(nullptr); // todo - make it possible to set swapchain parameters

	obj->backend->setup();



	obj->renderer = std::make_unique<le::Renderer>(*obj->backend);
	obj->renderer->setup();

	return obj;
}

// ----------------------------------------------------------------------

static bool test_app_update(test_app_o* self){

	pal::Window::pollEvents();

	if (self->window->shouldClose()){
		return false;
	}


	le::RenderModule renderModule{};
	{
//		le::RenderPass renderPassEarlyZ( "earlyZ" );
//		renderPassEarlyZ.setSetupCallback( []( auto pRp) {
//			auto rp     = le::RenderPassRef{pRp};

//			le::ImageAttachmentInfo depthAttachmentInfo;
//			depthAttachmentInfo.access_flags = le::AccessFlagBits::eWrite;
//			depthAttachmentInfo.format       = vk::Format::eD32SfloatS8Uint; // TODO: signal correct depth stencil format

//			rp.addImageAttachment( "depth", &depthAttachmentInfo );
//			return true;
//		} );

//		le::RenderPass renderPassForward( "forward" );
//		renderPassForward.setSetupCallback( []( auto pRp) {
//			auto rp     = le::RenderPassRef{pRp};

//			le::ImageAttachmentInfo colorAttachmentInfo;
//			colorAttachmentInfo.format       = vk::Format::eR8G8B8A8Unorm;
//			colorAttachmentInfo.access_flags = le::AccessFlagBits::eWrite;

//			// le::ImageAttachmentInfo depthAttachmentInfo;
//			// depthAttachmentInfo.format = device.getDefaultDepthStencilFormat();
//			// rp.addInputAttachment( "depth", &depthAttachmentInfo );

//			rp.addImageAttachment( "backbuffer", &colorAttachmentInfo );
//			return true;
//		} );

		le::RenderPass renderPassFinal( "root" );

		renderPassFinal.setSetupCallback( []( auto pRp) {
			auto rp     = le::RenderPassRef{pRp};

			le::ImageAttachmentInfo colorAttachmentInfo;
			colorAttachmentInfo.format       = vk::Format::eR8G8B8A8Unorm;
			colorAttachmentInfo.access_flags = le::AccessFlagBits::eReadWrite;

			le::ImageAttachmentInfo depthAttachmentInfo;
			depthAttachmentInfo.format       = vk::Format::eD32SfloatS8Uint; // TODO: signal correct depth stencil format
			depthAttachmentInfo.access_flags = le::AccessFlagBits::eReadWrite;


			rp.addImageAttachment( "backbuffer", &colorAttachmentInfo );
			//rp.addImageAttachment( "depth", &depthAttachmentInfo );
			return true;
		} );

		renderPassFinal.setRenderCallback([](auto encoder_, auto user_data_){
			std::cout << "** rendercallback called" << std::endl;
			auto self = static_cast<test_app_o*>(user_data_);
			le::CommandBufferEncoder encoder{encoder_};
//			// encoder.setPipeline(pipelineId);
//			// encoder.setDescriptor(setIndex,bindingNumber,arrayIndex,descriptorValue);
//			for (int i = 0; i !=100; ++i ){
//				encoder.setLineWidth(1.2f);
//			}
//			encoder.setLineWidth(5.3f);
			le::Viewport viewports[ 2 ] = {
			    {{0.f, 0.f, 100.f, 100.f, 0.f, 1.f}},
			    {{120.f, 0.f, 100.f, 100.f, 0.f, 1.f}},
			};

			le::Rect2D scissors[ 2 ] = {
			    {{0, 0, 100, 100}},
			    {{120, 0, 100, 100}},
			};

			encoder.setScissor(0,1,scissors);
			encoder.setViewport( 0, 1, viewports );

			encoder.draw(3,1,0,0);

			encoder.setScissor(0,1,&scissors[1]);
			encoder.setViewport( 0, 1, &viewports[1] );

			// vertex data must be stored to GPU mapped memory using an allocator through encoder first,
			// will then be available to the gpu.

			// this will use the scratch buffer -- and the encoded command will store the
			// location of the data as it was laid down in the scratch buffer.
			//encoder.setVertexData(void* data, size_t numBytes, uint32_t bindingIndex);

			// the scratch buffer is uploaded/transferred before the renderpass begins
			// so that data from it is read-visible,

			//encoder.setVertexData(bufferO* buffer, uint64_t numBytes, uint32_t bindingIndex);

			encoder.draw(3,1,0,0);



			// encoder.setVertexBuffers({buffer1,buffer2},{offset1,offset2});


		}, self);

		// renderModule.addRenderPass( renderPassEarlyZ );
		// renderModule.addRenderPass( renderPassForward );
		renderModule.addRenderPass( renderPassFinal );

	}
	// update will call all rendercallbacks in this frame.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer->update( renderModule );

	return true;
}

// ----------------------------------------------------------------------

static void test_app_destroy(test_app_o* self){
	delete(self);
}

// ----------------------------------------------------------------------

void register_test_app_api( void *api_ ) {
	auto  test_app_api_i = static_cast<test_app_api *>( api_ );
	auto &test_app_i     = test_app_api_i->test_app_i;

	test_app_i.initialize = initialize;
	test_app_i.terminate  = terminate;

	test_app_i.create    = test_app_create;
	test_app_i.destroy   = test_app_destroy;
	test_app_i.update    = test_app_update;
}
