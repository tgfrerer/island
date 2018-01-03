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
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	std::vector<vk::CommandBuffer> commandBuffers;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	const le::Device leDevice;
	le::Swapchain    swapchain;
	vk::Device       vkDevice = nullptr;

	std::vector<FrameData> frames;
	size_t currentFrame = size_t(~0);

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

	if (numImages !=0) {
		self->currentFrame = 0;
	}
}

// ----------------------------------------------------------------------

static void renderer_clear_frame(le_renderer_o* self, FrameData& frame){
	// + ensure frame fence has been reached

	auto &device = self->vkDevice;

	device.waitForFences({frame.frameFence},true,100'000'000);
	device.resetFences({frame.frameFence});

	device.freeCommandBuffers(frame.commandPool,frame.commandBuffers);
	frame.commandBuffers.clear();

	device.resetCommandPool(frame.commandPool,vk::CommandPoolResetFlagBits::eReleaseResources);

	// TODO: free transient gpu resources
	//       + transient memory
	//       + framebuffers
	// TODO: update descriptor pool for this frame

	auto acquireSuccess = self->swapchain.acquireNextImage(frame.semaphorePresentComplete,frame.swapchainImageIndex);

	if (!acquireSuccess){
		// TODO: deal with failed acquisition - frame needs to be placed back onto
		// stack. Failure most likely means that the swapchain was reset,
		// perhaps because of window resize.
		return;
	}

	// + acquire image from swapchain
}

static void renderer_record_frame(le_renderer_o* self, FrameData& frame){

}

static void renderer_process_frame(le_renderer_o*self, FrameData& frame){

}

static void renderer_dispatch_frame(le_renderer_o*self, FrameData& frame){

	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = { ::vk::PipelineStageFlagBits::eColorAttachmentOutput };

	vk::SubmitInfo submitInfo;
	submitInfo
	        .setWaitSemaphoreCount( 1 )
	        .setPWaitSemaphores( &frame.semaphorePresentComplete )
	        .setPWaitDstStageMask( wait_dst_stage_mask.data() )
	        .setCommandBufferCount( uint32_t(frame.commandBuffers.size()) )
	        .setPCommandBuffers( frame.commandBuffers.data() )
	        .setSignalSemaphoreCount( 1 )
	        .setPSignalSemaphores( &frame.semaphorePresentComplete)
	        ;

	auto queue = vk::Queue{self->leDevice.getDefaultGraphicsQueue()};

	queue.submit({submitInfo}, frame.frameFence);

	self->swapchain.present(self->leDevice.getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex);
}

// ----------------------------------------------------------------------

static void renderer_update(le_renderer_o* self){

	// trigger resolve on oldest prepared frame.
	std::cout << self->currentFrame << std::endl;

	auto &frame = self->frames[ self->currentFrame ];

	renderer_clear_frame   ( self, frame );
	renderer_process_frame ( self, frame );
	renderer_record_frame  ( self, frame );
	renderer_dispatch_frame( self, frame );

	// swap frame
	self->currentFrame = ( ++self->currentFrame ) % self->frames.size();
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
