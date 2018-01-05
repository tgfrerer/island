#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

// ----------------------------------------------------------------------

struct FrameData {

	struct Meta {
		 NanoTime time_clear_frame_start;
		 NanoTime time_clear_frame_end;

		 NanoTime time_process_frame_start;
		 NanoTime time_process_frame_end;

		 NanoTime time_record_frame_start;
		 NanoTime time_record_frame_end;

		 NanoTime time_dispatch_frame_start;
		 NanoTime time_dispatch_frame_end;
	};

	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	std::vector<vk::CommandBuffer> commandBuffers;

	std::vector<vk::Framebuffer>   debugFramebuffers;

	Meta meta;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	const le::Device leDevice;
	le::Swapchain    swapchain;
	vk::Device       vkDevice = nullptr;

	vk::RenderPass   debugRenderPass;

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

	// todo: call renderer_clear_frame on all frames which are not yet cleared.

	self->vkDevice.destroyRenderPass(self->debugRenderPass);

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

	// stand-in: create default renderpass.

	{
		std::array<vk::AttachmentDescription, 1> attachments;

		attachments[0]		// color attachment
		    .setFormat          ( vk::Format(self->swapchain.getSurfaceFormat()->format) )
		    .setSamples         ( vk::SampleCountFlagBits::e1 )
		    .setLoadOp          ( vk::AttachmentLoadOp::eClear )
		    .setStoreOp         ( vk::AttachmentStoreOp::eStore )
		    .setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
		    .setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
		    .setInitialLayout   ( vk::ImageLayout::eUndefined )
		    .setFinalLayout     ( vk::ImageLayout::ePresentSrcKHR )
		    ;
//		attachments[1]		//depth stencil attachment
//			.setFormat          ( depthFormat_ )
//			.setSamples         ( vk::SampleCountFlagBits::e1 )
//			.setLoadOp          ( vk::AttachmentLoadOp::eClear )
//			.setStoreOp         ( vk::AttachmentStoreOp::eStore)
//			.setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
//			.setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
//			.setInitialLayout   ( vk::ImageLayout::eUndefined )
//			.setFinalLayout     ( vk::ImageLayout::eDepthStencilAttachmentOptimal )
//			;

		// Define 2 attachments, and tell us what layout to expect these to be in.
		// Index references attachments from above.

		vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
		//vk::AttachmentReference depthReference{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

		vk::SubpassDescription subpassDescription;
		subpassDescription
		    .setPipelineBindPoint       ( vk::PipelineBindPoint::eGraphics )
		    .setInputAttachmentCount    ( 0 )
		    .setPInputAttachments       ( nullptr )
		    .setColorAttachmentCount    ( 1 )
		    .setPColorAttachments       ( &colorReference )
		    .setPResolveAttachments     ( nullptr )
		    .setPDepthStencilAttachment ( nullptr ) /* &depthReference */
		    .setPPreserveAttachments    ( nullptr )
		    .setPreserveAttachmentCount (0)
		    ;

		// Define 2 self-dependencies for subpass 0

		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0]
		    .setSrcSubpass      ( VK_SUBPASS_EXTERNAL ) // producer
		    .setDstSubpass      ( 0 )                   // consumer
		    .setSrcStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		    .setDstStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		    .setSrcAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		    .setDstAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		    .setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		    ;
		dependencies[1]
		    .setSrcSubpass      ( 0 )                                     // producer (last possible subpass)
		    .setDstSubpass      ( VK_SUBPASS_EXTERNAL )                   // consumer
		    .setSrcStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		    .setDstStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		    .setSrcAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		    .setDstAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		    .setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		    ;

		// Define 1 renderpass with 1 subpass

		vk::RenderPassCreateInfo renderPassCreateInfo;
		renderPassCreateInfo
		    .setAttachmentCount ( attachments.size() )
		    .setPAttachments    ( attachments.data() )
		    .setSubpassCount    ( 1 )
		    .setPSubpasses      ( &subpassDescription )
		    .setDependencyCount ( dependencies.size() )
		    .setPDependencies   ( dependencies.data() );

		self->debugRenderPass = self->vkDevice.createRenderPass(renderPassCreateInfo);
	}

}

// ----------------------------------------------------------------------

static void renderer_clear_frame(le_renderer_o* self, FrameData& frame){
	// + ensure frame fence has been reached

	frame.meta.time_clear_frame_start = std::chrono::high_resolution_clock::now();
	auto &device = self->vkDevice;

	device.waitForFences({frame.frameFence},true,100'000'000);
	device.resetFences({frame.frameFence});

	device.freeCommandBuffers(frame.commandPool,frame.commandBuffers);
	frame.commandBuffers.clear();

	device.resetCommandPool(frame.commandPool,vk::CommandPoolResetFlagBits::eReleaseResources);

	for (auto & fb:frame.debugFramebuffers){
		device.destroyFramebuffer(fb);
	}
	frame.debugFramebuffers.clear();

	// NOTE: free transient gpu resources
	//       + transient memory
	//       + framebuffers
	// NOTE: update descriptor pool for this frame

	// + acquire image from swapchain
	auto acquireSuccess = self->swapchain.acquireNextImage(frame.semaphorePresentComplete,frame.swapchainImageIndex);


	if (acquireSuccess == false){
		// TODO: deal with failed acquisition - frame needs to be placed back onto
		// stack. Failure most likely means that the swapchain was reset,
		// perhaps because of window resize.
		std::cout << "could not acquire frame." << std::endl;
	} else {

		// NOTE: frame was acquired successfully.
	}

	frame.meta.time_clear_frame_end = std::chrono::high_resolution_clock::now();

	// std::cout << std::dec << std::chrono::duration_cast<std::chrono::nanoseconds>(frame.meta.time_clear_frame_end-frame.meta.time_clear_frame_start).count() << std::endl;
}

// ----------------------------------------------------------------------

static void renderer_record_frame(le_renderer_o* self, FrameData& frame){
 // record api-agnostic intermediate draw lists
	frame.meta.time_record_frame_start = std::chrono::high_resolution_clock::now();
	frame.meta.time_record_frame_end = std::chrono::high_resolution_clock::now();
}

// ----------------------------------------------------------------------

static void renderer_process_frame(le_renderer_o*self, FrameData& frame){

	frame.meta.time_process_frame_start = std::chrono::high_resolution_clock::now();

	// translate intermediate draw lists into vk command buffers, and sync primitives

	auto cmdBufs = self->vkDevice.allocateCommandBuffers({frame.commandPool,vk::CommandBufferLevel::ePrimary,1});
	assert(cmdBufs.size() == 1 );


	// create frame buffer, based on swapchain and renderpass

	{
		std::array<vk::ImageView,1> framebufferAttachments {
			{self->swapchain.getImageView(frame.swapchainImageIndex)}
		};

		vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo
		        .setFlags( {} )
		        .setRenderPass( self->debugRenderPass )
		        .setAttachmentCount( uint32_t(framebufferAttachments.size()) )
		        .setPAttachments( framebufferAttachments.data() )
		        .setWidth( self->swapchain.getImageWidth() )
		        .setHeight( self->swapchain.getImageHeight())
		        .setLayers( 1 )
		        ;

		vk::Framebuffer fb = self->vkDevice.createFramebuffer(framebufferCreateInfo);
		frame.debugFramebuffers.emplace_back(std::move(fb));
	}

	std::array<vk::ClearValue, 1> clearValues{
		{vk::ClearColorValue( std::array<float, 4>{{0.3f, 1.f, 0.4f, 1.f}} )}};

	auto & cmd = cmdBufs.front();

	cmd.begin({ ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	{
		vk::RenderPassBeginInfo renderPassBeginInfo;

		renderPassBeginInfo
		    .setRenderPass( self->debugRenderPass )
		    .setFramebuffer( frame.debugFramebuffers.front() )
		    .setRenderArea( vk::Rect2D( {0, 0}, {self->swapchain.getImageWidth(), self->swapchain.getImageHeight()} ) )
		    .setClearValueCount( uint32_t( clearValues.size() ) )
		    .setPClearValues( clearValues.data() );

		cmd.beginRenderPass(renderPassBeginInfo,vk::SubpassContents::eInline);
		cmd.endRenderPass();
	}
	cmd.end();

	// place command buffer in frame store so that it can be submitted.
	for (auto &&c:cmdBufs){
		frame.commandBuffers.emplace_back(c);
	}

	frame.meta.time_process_frame_end = std::chrono::high_resolution_clock::now();

	//std::cout << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_process_frame_end-frame.meta.time_process_frame_start).count() << std::endl;
}

// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o *self, FrameData &frame ) {
	frame.meta.time_dispatch_frame_start = std::chrono::high_resolution_clock::now();

	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = {::vk::PipelineStageFlagBits::eColorAttachmentOutput};

	vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount( 1 )
	    .setPWaitSemaphores( &frame.semaphorePresentComplete )
	    .setPWaitDstStageMask( wait_dst_stage_mask.data() )
	    .setCommandBufferCount( uint32_t( frame.commandBuffers.size() ) )
	    .setPCommandBuffers( frame.commandBuffers.data() )
	    .setSignalSemaphoreCount( 1 )
	    .setPSignalSemaphores( &frame.semaphoreRenderComplete )
	    ;

	auto queue = vk::Queue{self->leDevice.getDefaultGraphicsQueue()};

	queue.submit( {submitInfo}, frame.frameFence );

	bool presentSuccessful = self->swapchain.present( self->leDevice.getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex );

	frame.meta.time_dispatch_frame_end = std::chrono::high_resolution_clock::now();
}

// ----------------------------------------------------------------------

static void renderer_update(le_renderer_o* self){

	// trigger resolve on oldest prepared frame.
	// std::cout << self->currentFrame << std::endl;

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
