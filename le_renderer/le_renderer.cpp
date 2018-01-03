#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>

struct FrameData {
	vk::Semaphore semaphoreRenderComplete  = nullptr;
	vk::Semaphore semaphorePresentComplete = nullptr;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	le::Device    mDevice;
	le::Swapchain mSwapchain;

	std::vector<FrameData> mFrames;

	le_renderer_o( const le::Device &device_, const le::Swapchain &swapchain_ )
	    : mDevice( device_ )
	    , mSwapchain( swapchain_ ) {
	}
};

// ----------------------------------------------------------------------

static le_renderer_o *renderer_create( le_backend_vk_device_o *device, le_backend_swapchain_o *swapchain ) {
	auto obj = new le_renderer_o( device, swapchain );
	return obj;
}

// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o *self ) {

	vk::Device vkDevice{self->mDevice.getVkDevice()};

	for ( auto &frameData : self->mFrames ) {
		vkDevice.destroySemaphore( frameData.semaphorePresentComplete );
		vkDevice.destroySemaphore( frameData.semaphoreRenderComplete );
	}

	self->mFrames.clear();

	delete self;
}

// ----------------------------------------------------------------------

static void renderer_setup( le_renderer_o *self ) {
	auto numImages = self->mSwapchain.getSwapchainImageCount();

	vk::Device vkDevice{self->mDevice.getVkDevice()};

	vk::SemaphoreCreateInfo createInfo;

	for ( size_t i = 0; i != numImages; ++i ) {
		auto frameData = FrameData();

		frameData.semaphorePresentComplete = vkDevice.createSemaphore( {}, nullptr );
		frameData.semaphoreRenderComplete  = vkDevice.createSemaphore( {}, nullptr );

		self->mFrames.emplace_back( std::move( frameData ) );
	}
}

// ----------------------------------------------------------------------

void register_le_renderer_api( void *api_ ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create  = renderer_create;
	le_renderer_i.destroy = renderer_destroy;
	le_renderer_i.setup   = renderer_setup;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
