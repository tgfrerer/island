#include "test_app/test_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"

#include "vulkan/vulkan.hpp"
#include <iostream>
#include <memory>

struct test_app_o {
	std::unique_ptr<le::Instance> instance;
	std::unique_ptr<pal::Window> window;
	std::unique_ptr<le::Device> device;
	std::unique_ptr<le::Swapchain> swapchain;
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


	uint32_t numRequestedExtensions = 0;
	auto requestedExtensions = pal::Window::getRequiredVkExtensions( &numRequestedExtensions );

	obj->instance = std::make_unique<le::Instance>(requestedExtensions, numRequestedExtensions);

	obj->window = std::make_unique<pal::Window>(settings);
	obj->window->createSurface(obj->instance->getVkInstance());

	obj->device = std::make_unique<le::Device>(*obj->instance);

	le::Swapchain::Settings swapchainSettings;
	swapchainSettings
	    .setImageCountHint          ( 3 )
	    .setPresentModeHint         ( le::Swapchain::Presentmode::eFifo )
	    .setWidthHint               ( obj->window->getSurfaceWidth() )
	    .setHeightHint              ( obj->window->getSurfaceHeight() )
	    .setVkDevice                ( obj->device->getVkDevice() )
	    .setVkPhysicalDevice        ( obj->device->getVkPhysicalDevice() )
	    .setGraphicsQueueFamilyIndex( obj->device->getDefaultGraphicsQueueFamilyIndex() )
	    .setVkSurfaceKHR            ( obj->window->getVkSurfaceKHR() )
	    ;

	obj->swapchain = std::make_unique<le::Swapchain>(swapchainSettings);

	obj->renderer = std::make_unique<le::Renderer>(*obj->device,*obj->swapchain);
	obj->renderer->setup();

	return obj;
}

// ----------------------------------------------------------------------

static bool test_app_update(test_app_o* self){

	pal::Window::pollEvents();

	if (self->window->shouldClose()){
		return false;
	}

	// app.update
	// app.draw

	le::RenderModule renderModule{*self->device};
	{
		le::RenderPass renderPassEarlyZ( "earlyZ" );
		renderPassEarlyZ.setSetupCallback( []( auto pRp, auto pDevice ) {
			auto rp     = le::RenderPassRef{pRp};
			auto device = le::Device{pDevice};

			le::ImageAttachmentInfo depthAttachmentInfo;
			depthAttachmentInfo.access_flags = le::AccessFlagBits::eWrite;
			depthAttachmentInfo.format       = device.getDefaultDepthStencilFormat();

			rp.addImageAttachment( "depth", &depthAttachmentInfo );
			return true;
		} );

		le::RenderPass renderPassForward( "forward" );
		renderPassForward.setSetupCallback( []( auto pRp, auto pDevice ) {
			auto rp     = le::RenderPassRef{pRp};
			auto device = le::Device{pDevice};

			le::ImageAttachmentInfo colorAttachmentInfo;
			colorAttachmentInfo.format       = vk::Format::eR8G8B8A8Unorm;
			colorAttachmentInfo.access_flags = le::AccessFlagBits::eWrite;

			// le::ImageAttachmentInfo depthAttachmentInfo;
			// depthAttachmentInfo.format = device.getDefaultDepthStencilFormat();
			// rp.addInputAttachment( "depth", &depthAttachmentInfo );

			rp.addImageAttachment( "backbuffer", &colorAttachmentInfo );
			return true;
		} );

		le::RenderPass renderPassFinal( "root" );
		renderPassFinal.setSetupCallback( []( auto pRp, auto pDevice ) {
			auto rp     = le::RenderPassRef{pRp};
			auto device = le::Device{pDevice};

			le::ImageAttachmentInfo colorAttachmentInfo;
			colorAttachmentInfo.format       = vk::Format::eR8G8B8A8Unorm;
			colorAttachmentInfo.access_flags = le::AccessFlagBits::eReadWrite;

			le::ImageAttachmentInfo depthAttachmentInfo;
			depthAttachmentInfo.format       = device.getDefaultDepthStencilFormat();
			depthAttachmentInfo.access_flags = le::AccessFlagBits::eReadWrite;

			rp.addImageAttachment( "backbuffer", &colorAttachmentInfo );
			//rp.addImageAttachment( "depth", &depthAttachmentInfo );
			return true;
		} );


		renderPassFinal.setRenderCallback([](auto encoder_, auto user_data_){
			auto self = static_cast<test_app_o*>(user_data_);

			std::cout << "** rendercallback called" << std::endl;

		}, self);

		// TODO: add setExecuteFun to renderpass - this is the method which actually
		// does specify the draw calls, and which pipelines to use.

		//						renderModule.addRenderPass( renderPassEarlyZ );
		renderModule.addRenderPass( renderPassForward );
		renderModule.addRenderPass( renderPassFinal );



	}
	// update will call all rendercallbacks in this frame.
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
