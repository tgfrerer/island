#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>

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

template <typename T>
class AtomicQueue{
	std::queue<T> mQueue;
	std::mutex mMutex;
public:
	T& front(){
		std::lock_guard<std::mutex> lck(mMutex);
		return mQueue.front();
	}
	void pop(){
		std::lock_guard<std::mutex> lck(mMutex);
		mQueue.pop();
	}
	void push(const T& val){
		std::lock_guard<std::mutex> lck(mMutex);
		mQueue.push(val);
	}
	size_t size(){
		std::lock_guard<std::mutex> lck(mMutex);
		size_t currentSize = mQueue.size();
		return currentSize;
	}
	bool empty(){
		std::lock_guard<std::mutex> lck(mMutex);
		bool currentEmpty = mQueue.empty();
		return currentEmpty;
	}
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	const le::Device leDevice;
	le::Swapchain    swapchain;
	vk::Device       vkDevice = nullptr;

	vk::RenderPass   debugRenderPass = nullptr;

	AtomicQueue<size_t> queue_frames_acquire;
	AtomicQueue<size_t> queue_frames_process;
	AtomicQueue<size_t> queue_frames_record;
	AtomicQueue<size_t> queue_frames_dispatch;

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

static void renderer_setup( le_renderer_o *self ) {
	auto numImages = self->swapchain.getImagesCount();

	vk::CommandPoolCreateInfo commandPoolCreateInfo;

	for ( size_t i = 0; i != numImages; ++i ) {
		auto frameData = FrameData();

		frameData.frameFence               = self->vkDevice.createFence( {::vk::FenceCreateFlagBits::eSignaled} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = self->vkDevice.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = self->vkDevice.createSemaphore( {} );
		frameData.commandPool              = self->vkDevice.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->leDevice.getDefaultGraphicsQueueFamilyIndex()} );

		self->frames.emplace_back( std::move( frameData ) );

		// pre-populate aqcuire queue
		self->queue_frames_acquire.push(i);
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

static void renderer_clear_frame( le_renderer_o *self, FrameData &frame ) {

	auto &device = self->vkDevice;
	// + ensure frame fence has been reached
	device.waitForFences( {frame.frameFence}, true, 100'000'000 );
	device.resetFences( {frame.frameFence} );

	device.freeCommandBuffers( frame.commandPool, frame.commandBuffers );
	frame.commandBuffers.clear();

	device.resetCommandPool( frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources );

	for ( auto &fb : frame.debugFramebuffers ) {
		device.destroyFramebuffer( fb );
	}
	frame.debugFramebuffers.clear();

	// NOTE: free transient gpu resources
	//       + transient memory
}

// ----------------------------------------------------------------------

static void renderer_acquire_frame(le_renderer_o* self){

	if (self->queue_frames_acquire.empty()){
		return;
	}

	// ---------| invariant: There are frames to process.

	auto currentFrameIndex = self->queue_frames_acquire.front();

	auto &frame = self->frames[ currentFrameIndex ];
	frame.meta.time_clear_frame_start = std::chrono::high_resolution_clock::now();

	renderer_clear_frame(self, frame);

	// TODO: update descriptor pool for this frame

	auto acquireSuccess = self->swapchain.acquireNextImage(frame.semaphorePresentComplete,frame.swapchainImageIndex);

	frame.meta.time_clear_frame_end = std::chrono::high_resolution_clock::now();

	if ( acquireSuccess ) {
		self->queue_frames_acquire.pop();
		self->queue_frames_record.push( std::move( currentFrameIndex ) );
	} else {

		// TODO: deal with failed acquisition - frame needs to be placed back onto
		// acquire queue. Failure most likely means that the swapchain was reset,
		// perhaps because of window resize.
		std::cout << "could not acquire frame." << std::endl;
	}
}

// ----------------------------------------------------------------------

static void renderer_record_frame(le_renderer_o* self){

	if (self->queue_frames_record.empty()){
		return;
	}
	// ---------| invariant: There are frames to process.

	auto currentFrameIndex = self->queue_frames_record.front();

	auto &frame = self->frames[ currentFrameIndex ];

	// record api-agnostic intermediate draw lists
	frame.meta.time_record_frame_start = std::chrono::high_resolution_clock::now();
	frame.meta.time_record_frame_end = std::chrono::high_resolution_clock::now();

	self->queue_frames_record.pop();
	self->queue_frames_process.push(std::move(currentFrameIndex));
}

// ----------------------------------------------------------------------

static void renderer_process_frame( le_renderer_o *self ) {

	if ( self->queue_frames_process.empty() ) {
		return;
	}
	// ---------| invariant: There are frames to process.

	auto currentFrameIndex = self->queue_frames_process.front();


	auto &frame                         = self->frames[ currentFrameIndex ];
	frame.meta.time_process_frame_start = std::chrono::high_resolution_clock::now();

	// translate intermediate draw lists into vk command buffers, and sync primitives

	auto cmdBufs = self->vkDevice.allocateCommandBuffers( {frame.commandPool, vk::CommandBufferLevel::ePrimary, 1} );
	assert( cmdBufs.size() == 1 );

	// create frame buffer, based on swapchain and renderpass

	{
		std::array<vk::ImageView, 1> framebufferAttachments{
			{self->swapchain.getImageView( frame.swapchainImageIndex )}};

		vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo
		    .setFlags           ( {} )
		    .setRenderPass      ( self->debugRenderPass )
		    .setAttachmentCount ( uint32_t( framebufferAttachments.size() ) )
		    .setPAttachments    ( framebufferAttachments.data() )
		    .setWidth           ( self->swapchain.getImageWidth() )
		    .setHeight          ( self->swapchain.getImageHeight() )
		    .setLayers          ( 1 )
		    ;

		vk::Framebuffer fb = self->vkDevice.createFramebuffer( framebufferCreateInfo );
		frame.debugFramebuffers.emplace_back( std::move( fb ) );
	}

	std::array<vk::ClearValue, 1> clearValues{
		{vk::ClearColorValue( std::array<float, 4>{{0.3f, 1.f, 0.4f, 1.f}} )}};

	auto &cmd = cmdBufs.front();

	cmd.begin( {::vk::CommandBufferUsageFlagBits::eOneTimeSubmit} );
	{
		vk::RenderPassBeginInfo renderPassBeginInfo;

		renderPassBeginInfo
		    .setRenderPass      ( self->debugRenderPass )
		    .setFramebuffer     ( frame.debugFramebuffers.back() )
		    .setRenderArea      ( vk::Rect2D( {0, 0}, {self->swapchain.getImageWidth(), self->swapchain.getImageHeight()} ) )
		    .setClearValueCount ( uint32_t( clearValues.size() ) )
		    .setPClearValues    ( clearValues.data() )
		    ;

		cmd.beginRenderPass( renderPassBeginInfo, vk::SubpassContents::eInline );
		cmd.endRenderPass();
	}
	cmd.end();

	// place command buffer in frame store so that it can be submitted.
	for ( auto &&c : cmdBufs ) {
		frame.commandBuffers.emplace_back( c );
	}

	frame.meta.time_process_frame_end = std::chrono::high_resolution_clock::now();
	//std::cout << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_process_frame_end-frame.meta.time_process_frame_start).count() << std::endl;

	self->queue_frames_process.pop();
	self->queue_frames_dispatch.push( std::move( currentFrameIndex ) );
}

// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o *self) {

	if (self->queue_frames_dispatch.empty()){
		return;
	}
	// ---------| invariant: There are frames to process.

	auto currentFrameIndex = self->queue_frames_dispatch.front();

	auto &frame = self->frames[ currentFrameIndex ];
	frame.meta.time_dispatch_frame_start = std::chrono::high_resolution_clock::now();

	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = {::vk::PipelineStageFlagBits::eColorAttachmentOutput};

	vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount   ( 1 )
	    .setPWaitSemaphores      ( &frame.semaphorePresentComplete )
	    .setPWaitDstStageMask    ( wait_dst_stage_mask.data() )
	    .setCommandBufferCount   ( uint32_t( frame.commandBuffers.size() ) )
	    .setPCommandBuffers      ( frame.commandBuffers.data() )
	    .setSignalSemaphoreCount ( 1 )
	    .setPSignalSemaphores    ( &frame.semaphoreRenderComplete )
	    ;

	auto queue = vk::Queue{self->leDevice.getDefaultGraphicsQueue()};

	queue.submit( {submitInfo}, frame.frameFence );

	bool presentSuccessful = self->swapchain.present( self->leDevice.getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex );

	frame.meta.time_dispatch_frame_end = std::chrono::high_resolution_clock::now();

	if (presentSuccessful){
		self->queue_frames_dispatch.pop();
		self->queue_frames_acquire.push(std::move(currentFrameIndex));
	} else {

		std::cout << "WARNING: " << "could not present frame." << std::endl;

		// Present was not successful -
		//
		// This most likely happened because the window surface has been resized.
		// We therefore attempt to reset the swapchain.

		// first, remove this frame from present queue.
		renderer_clear_frame(self,frame);
		self->queue_frames_dispatch.pop();
		self->queue_frames_acquire.push(currentFrameIndex);

		self->swapchain.reset();
	}

}

// ----------------------------------------------------------------------

static void renderer_update(le_renderer_o* self){

	renderer_acquire_frame ( self );
	renderer_record_frame  ( self ); // this should run on the main thread
	renderer_process_frame ( self );
	renderer_dispatch_frame( self );

}

// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o *self ) {

	// todo: call renderer_clear_frame on all frames which are not yet cleared.

	while(!self->queue_frames_acquire.empty()){
		auto frameIndex = self->queue_frames_acquire.front();
		renderer_clear_frame(self,self->frames[frameIndex]);
		self->queue_frames_acquire.pop();
	}

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

void register_le_renderer_api( void *api_ ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create  = renderer_create;
	le_renderer_i.destroy = renderer_destroy;
	le_renderer_i.setup   = renderer_setup;
	le_renderer_i.update  = renderer_update;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
