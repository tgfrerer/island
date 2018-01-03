#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>

// ----------------------------------------------------------------------

struct FrameData {
	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	std::vector<vk::CommandBuffer> commandBuffers;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	const le::Device leDevice;
	le::Swapchain    swapchain;
	vk::Device       vkDevice = nullptr;

	std::vector<FrameData> frames;

	le_renderer_o( const le::Device &device_, const le::Swapchain &swapchain_ )
	    : leDevice( device_ )
	    , swapchain( swapchain_ )
	    , vkDevice( leDevice.getVkDevice() ) {
	}
};

// ----------------------------------------------------------------------

static le_renderer_o *renderer_create( le_backend_vk_device_o *device, le_backend_swapchain_o *swapchain ) {
	auto obj = new le_renderer_o( device, swapchain );
	return obj;
}

// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o *self ) {

	for ( auto &frameData : self->frames ) {
		self->vkDevice.destroyFence( frameData.frameFence );
		self->vkDevice.destroySemaphore( frameData.semaphorePresentComplete );
		self->vkDevice.destroySemaphore( frameData.semaphoreRenderComplete );
		self->vkDevice.destroyCommandPool( frameData.commandPool );
	}

	self->frames.clear();

	delete self;
}

// ----------------------------------------------------------------------

static void renderer_setup( le_renderer_o *self ) {
	auto numImages = self->swapchain.getSwapchainImageCount();

	vk::CommandPoolCreateInfo commandPoolCreateInfo;

	for ( size_t i = 0; i != numImages; ++i ) {
		auto frameData = FrameData();

		frameData.frameFence               = self->vkDevice.createFence( {::vk::FenceCreateFlagBits::eSignaled} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = self->vkDevice.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = self->vkDevice.createSemaphore( {} );
		frameData.commandPool              = self->vkDevice.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->leDevice.getDefaultGraphicsQueueFamilyIndex()} );

		self->frames.emplace_back( std::move( frameData ) );
	}
}

// ----------------------------------------------------------------------

static void renderer_fetch_frame(FrameData* frame){
	// + ensure frame fence has been reached

	// + free transient gpu resources

	// + acquire image from swapchain
}

// ----------------------------------------------------------------------

static void renderer_update(le_renderer_o* self){

// trigger resolve on oldest prepared frame.

}

// ----------------------------------------------------------------------

void register_le_renderer_api( void *api_ ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create  = renderer_create;
	le_renderer_i.destroy = renderer_destroy;
	le_renderer_i.setup   = renderer_setup;
	le_renderer_i.update  = renderer_update;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
