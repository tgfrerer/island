#include "le_backend_vk/le_backend_vk.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include "include/internal/le_swapchain_vk_common.h"
#include "le_backend_vk/util/vk_mem_alloc/vk_mem_alloc.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <ctime>

struct TransferFrame {
	vk::Image         image            = nullptr; // Owned. Handle to image
	vk::Buffer        buffer           = nullptr; // Owned. Handle to buffer
	VmaAllocation     imageAllocation  = nullptr; // Owned. Handle to image allocation
	VmaAllocation     bufferAllocation = nullptr; // Owned. Handle to buffer allocation
	VmaAllocationInfo imageAllocationInfo{};
	VmaAllocationInfo bufferAllocationInfo{};
	vk::Fence         frameFence;
	vk::CommandBuffer cmdPresent; // copies from image to buffer
	vk::CommandBuffer cmdAcquire; // transfers image back to correct layout
};

struct img_data_o {
	le_swapchain_vk_settings_t mSettings;
	uint32_t                   mImagecount;                    // Number of images in swapchain
	uint32_t                   totalImages;                    // total number of produced images
	uint32_t                   mImageIndex;                    // current image index
	uint32_t                   vk_graphics_queue_family_index; //
	vk::Extent3D               mSwapchainExtent;               //
	vk::SurfaceFormatKHR       windowSurfaceFormat;            //
	uint32_t                   reserved__;                     // RESERVED for packing this struct
	vk::Device                 device;                         // Owned by backend
	vk::PhysicalDevice         physicalDevice;                 // Owned by backend
	vk::CommandPool            vkCommandPool;                  // Command pool from wich we allocate present and acquire command buffers
	le_backend_o *             backend = nullptr;              // Not owned. Backend owns swapchain.
	std::vector<TransferFrame> transferFrames;                 //
	FILE *                     ffmpeg_pipe = nullptr;          // Pipe to ffmpeg. Owned. must be closed if opened
};

// ----------------------------------------------------------------------

static void swapchain_img_reset( le_swapchain_o *base, const le_swapchain_vk_settings_t *settings_ ) {

	auto self = static_cast<img_data_o *const>( base->data );

	assert( settings_ );

	if ( settings_ ) {
		self->mSettings = *settings_;
		self->mSwapchainExtent
		    .setWidth( self->mSettings.width_hint )
		    .setHeight( self->mSettings.height_hint )
		    .setDepth( 1 );
		self->mImagecount = self->mSettings.imagecount_hint;
	}

	// TODO: create image allocations.

	/// - delete any semaphores
	/// - free any allocated images
	/// - clear vector with references to allocated images
	/// - allocate images
	/// - create vector with references to allocated images
	/// - create semaphores

	int imgAllocationResult = -1;
	int bufAllocationResult = -1;

	uint32_t const numFrames = self->mImagecount;

	self->transferFrames.reserve( numFrames );

	for ( size_t i = 0; i != numFrames; ++i ) {
		TransferFrame frame{};

		uint64_t imgSize = 0;
		{
			// Allocate space for an image which can hold a render surface

			VkImageCreateInfo imageCreateInfo =
			    vk::ImageCreateInfo()
			        .setImageType( ::vk::ImageType::e2D )
			        .setFormat( self->windowSurfaceFormat.format )
			        .setExtent( self->mSwapchainExtent )
			        .setMipLevels( 1 )
			        .setArrayLayers( 1 )
			        .setSamples( vk::SampleCountFlagBits::e1 )
			        .setTiling( vk::ImageTiling::eOptimal )
			        .setUsage( vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc )
			        .setSharingMode( ::vk::SharingMode::eExclusive )
			        .setQueueFamilyIndexCount( 1 )
			        .setPQueueFamilyIndices( &self->vk_graphics_queue_family_index )
			        .setInitialLayout( ::vk::ImageLayout::eUndefined ) //
			    ;

			VmaAllocationCreateInfo allocationCreateInfo{};
			allocationCreateInfo.flags          = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
			allocationCreateInfo.usage          = VMA_MEMORY_USAGE_GPU_ONLY;
			allocationCreateInfo.preferredFlags = 0;

			using namespace le_backend_vk;
			imgAllocationResult = private_backend_vk_i.allocate_image( self->backend, &imageCreateInfo,
			                                                           &allocationCreateInfo,
			                                                           reinterpret_cast<VkImage *>( &frame.image ),
			                                                           &frame.imageAllocation,
			                                                           &frame.imageAllocationInfo );
			assert( imgAllocationResult == VK_SUCCESS );
			imgSize = frame.imageAllocationInfo.size;
		}

		{
			// Allocate space for a buffer in which to read back the image data.
			//
			// now we need a buffer which is host visible and coherent, which we can use to read out our data.
			// there needs to be one buffer per image;
			using namespace le_backend_vk;

			VkBufferCreateInfo bufferCreateInfo =
			    vk::BufferCreateInfo()
			        .setPQueueFamilyIndices( &self->vk_graphics_queue_family_index )
			        .setQueueFamilyIndexCount( 1 )
			        .setUsage( vk::BufferUsageFlagBits::eTransferDst )
			        .setSize( imgSize ) //
			    ;

			VmaAllocationCreateInfo allocationCreateInfo{};
			allocationCreateInfo.flags          = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			allocationCreateInfo.usage          = VMA_MEMORY_USAGE_CPU_ONLY;
			allocationCreateInfo.preferredFlags = 0;

			bufAllocationResult = private_backend_vk_i.allocate_buffer( self->backend, &bufferCreateInfo, &allocationCreateInfo, reinterpret_cast<VkBuffer *>( &frame.buffer ), &frame.bufferAllocation, &frame.bufferAllocationInfo );
			assert( imgAllocationResult == VK_SUCCESS );
		}

		frame.frameFence = self->device.createFence( {::vk::FenceCreateFlagBits::eSignaled} );

		self->transferFrames.emplace_back( frame );
	}

	// Allocate command buffers for each frame.
	// Each frame needs one command buffer

	::vk::CommandBufferAllocateInfo allocateInfo;
	allocateInfo
	    .setCommandPool( self->vkCommandPool )
	    .setLevel( ::vk::CommandBufferLevel::ePrimary )
	    .setCommandBufferCount( numFrames * 2 );

	auto cmdBuffers = self->device.allocateCommandBuffers( allocateInfo );

	// set up commands in frames.

	for ( size_t i = 0; i != numFrames; ++i ) {
		self->transferFrames[ i ].cmdAcquire = cmdBuffers[ i * 2 ];
		self->transferFrames[ i ].cmdPresent = cmdBuffers[ i * 2 + 1 ];
	}

	// Add commands to command buffers for all frames.

	for ( auto &frame : self->transferFrames ) {
		{
			// copy == transfer image to buffer memory
			vk::CommandBuffer &cmdPresent = frame.cmdPresent;

			cmdPresent.begin( {::vk::CommandBufferUsageFlags()} );

			auto imgMemBarrier =
			    vk::ImageMemoryBarrier()
			        .setSrcAccessMask( ::vk::AccessFlagBits::eMemoryRead )
			        .setDstAccessMask( ::vk::AccessFlagBits::eTransferRead )
			        .setOldLayout( ::vk::ImageLayout::ePresentSrcKHR )
			        .setNewLayout( ::vk::ImageLayout::eTransferSrcOptimal )
			        .setSrcQueueFamilyIndex( self->vk_graphics_queue_family_index ) // < TODO: queue ownership: graphics -> transfer
			        .setDstQueueFamilyIndex( self->vk_graphics_queue_family_index ) // < TODO: queue ownership: graphics -> transfer
			        .setImage( frame.image )
			        .setSubresourceRange( {::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} );

			cmdPresent.pipelineBarrier( ::vk::PipelineStageFlagBits::eAllCommands, ::vk::PipelineStageFlagBits::eTransfer, ::vk::DependencyFlags(), {}, {}, {imgMemBarrier} );

			::vk::ImageSubresourceLayers imgSubResource;
			imgSubResource
			    .setAspectMask( ::vk::ImageAspectFlagBits::eColor )
			    .setMipLevel( 0 )
			    .setBaseArrayLayer( 0 )
			    .setLayerCount( 1 );

			vk::BufferImageCopy imgCopy;
			imgCopy
			    .setBufferOffset( 0 ) // offset is always 0, since allocator created individual buffer objects
			    .setBufferRowLength( self->mSwapchainExtent.width )
			    .setBufferImageHeight( self->mSwapchainExtent.height )
			    .setImageSubresource( imgSubResource )
			    .setImageOffset( {0} )
			    .setImageExtent( self->mSwapchainExtent );

			// TODO: here we must wait for the buffer read event to have been signalled - because that means that the buffer
			// is available for writing again. Perhaps we should use a buffer which is not persistently mapped, if that's faster.

			// image must be transferred to a buffer - we can then read from this buffer.
			cmdPresent.copyImageToBuffer( frame.image, ::vk::ImageLayout::eTransferSrcOptimal, frame.buffer, {imgCopy} );
			cmdPresent.end();
		}
		{
			// Move ownership of image back from transfer -> graphics
			// Change image layout back to colorattachment

			::vk::CommandBuffer &cmdAcquire = frame.cmdAcquire;

			cmdAcquire.begin( {::vk::CommandBufferUsageFlags()} );

			auto barrierReadToAcquire =
			    vk::ImageMemoryBarrier()
			        .setSrcAccessMask( ::vk::AccessFlagBits::eTransferRead )
			        .setDstAccessMask( ::vk::AccessFlagBits::eColorAttachmentWrite )
			        .setOldLayout( ::vk::ImageLayout::eUndefined )
			        .setNewLayout( ::vk::ImageLayout::eColorAttachmentOptimal )
			        .setSrcQueueFamilyIndex( self->vk_graphics_queue_family_index ) // < TODO: queue ownership: transfer -> graphics
			        .setDstQueueFamilyIndex( self->vk_graphics_queue_family_index ) // < TODO: queue ownership: transfer -> graphics
			        .setImage( frame.image )
			        .setSubresourceRange( {::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} );

			cmdAcquire.pipelineBarrier( ::vk::PipelineStageFlagBits::eAllCommands, ::vk::PipelineStageFlagBits::eColorAttachmentOutput, ::vk::DependencyFlags(), {}, {}, {barrierReadToAcquire} );

			cmdAcquire.end();
		}
	}
}

// ----------------------------------------------------------------------

static le_swapchain_o *swapchain_img_create( const le_swapchain_vk_api::swapchain_interface_t &interface, le_backend_o *backend, const le_swapchain_vk_settings_t *settings ) {
	auto base  = new le_swapchain_o( interface );
	base->data = new img_data_o{};
	auto self  = static_cast<img_data_o *>( base->data );

	self->backend             = backend;
	self->windowSurfaceFormat = {vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear};
	self->mImageIndex         = uint32_t( ~0 );
	{

		using namespace le_backend_vk;
		self->device                         = private_backend_vk_i.get_vk_device( backend );
		self->physicalDevice                 = private_backend_vk_i.get_vk_physical_device( backend );
		auto le_device                       = private_backend_vk_i.get_le_device( backend );
		self->vk_graphics_queue_family_index = vk_device_i.get_default_graphics_queue_family_index( le_device );
	}

	{
		// Create a command pool, so that we may create command buffers from it.

		::vk::CommandPoolCreateInfo createInfo;
		createInfo
		    .setFlags( ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer )
		    .setQueueFamilyIndex( self->vk_graphics_queue_family_index ) //< Todo: make sure this has been set properly when renderer/queues were set up.
		    ;
		self->vkCommandPool = self->device.createCommandPool( createInfo );
	}

	swapchain_img_reset( base, settings );

	{
		// First generate a timestamp tag so that we can make
		// sure that successive screen captures don't overwrite.

		std::ostringstream timestamp_tag;
		std::time_t        time_now = std::time( nullptr );

		timestamp_tag << std::put_time( std::localtime( &time_now ), "_%y-%m-%d_%OH-%OM-%OS" );

		// -- Initialise ffmpeg as a receiver for our frames by selecting one of
		// these possible command line options. Eventually we want to expose the
		// command line, so that we may pipe to any program.

		const char *commandLines[] = {
		    "ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s %dx%d -i - -threads 0 -vcodec h264_nvenc -preset llhq -rc:v vbr_minqp -qmin:v 19 -qmax:v 21 -b:v 2500k -maxrate:v 5000k -profile:v high isl%s.mp4",
		    "ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s %dx%d -i - -threads 0  -preset fast -y -pix_fmt yuv420p -crf 21 isl%s.mp4",
		    "ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s %dx%d -i - -threads 0  isl%s_%%03d.png",
		};

		char cmd[ 1024 ]{};
		sprintf( cmd, commandLines[ 0 ], self->mSwapchainExtent.width, self->mSwapchainExtent.height, timestamp_tag.str().c_str() );

		std::cout << "Pipe command line string: '" << cmd << "'" << std::endl
		          << std::flush;

		// Open pipe to ffmpeg's stdin in binary write mode
		self->ffmpeg_pipe = popen( cmd, "w" );

		if ( self->ffmpeg_pipe == nullptr ) {

			std::cout << " ***** ERROR: Could not open pipe. Additionally, strerror reports:" << strerror( errno ) << std::endl
			          << std::flush;
		}

		assert( self->ffmpeg_pipe != nullptr );
	}
	return base;
}

// ----------------------------------------------------------------------

static void swapchain_img_destroy( le_swapchain_o *base ) {

	auto self = static_cast<img_data_o *const>( base->data );

	// close ffmpeg pipe handle

	if ( self->ffmpeg_pipe ) {
		pclose( self->ffmpeg_pipe );
		self->ffmpeg_pipe = nullptr; // mark as closed
	}

	using namespace le_backend_vk;

	{
		// -- Wait for current in-flight frame to be completed on device.

		// We must do this since we're not allowed to delete any vulkan resources
		// which are currently used by the device. Awaiting the fence guarantees
		// that no resources are in-flight at this point.

		auto imageIndex = ( self->mImageIndex ) % self->mImagecount;

		auto fenceWaitResult = self->device.waitForFences( {self->transferFrames[ imageIndex ].frameFence}, VK_TRUE, 100'000'000 );

		if ( fenceWaitResult != ::vk::Result::eSuccess ) {
			assert( false ); // waiting for fence took too long.
		}
	}

	for ( auto &f : self->transferFrames ) {

		// Destroy image allocation for this frame.
		private_backend_vk_i.destroy_image( self->backend, f.image, f.imageAllocation );
		// Destroy buffer allocation for this frame.
		private_backend_vk_i.destroy_buffer( self->backend, f.buffer, f.bufferAllocation );

		if ( f.frameFence ) {
			self->device.destroyFence( f.frameFence );
			f.frameFence = nullptr;
		}
	}

	// Clear TransferFrame

	self->transferFrames.clear();

	if ( self->vkCommandPool ) {

		// Destroying the command pool implicitly frees all command buffers
		// which were allocated from it, so we don't have to free command
		// buffers explicitly.

		self->device.destroyCommandPool( self->vkCommandPool );
		self->vkCommandPool = nullptr;
	}

	delete self; // delete object's data
	delete base; // delete object
}

// ----------------------------------------------------------------------

static bool swapchain_img_acquire_next_image( le_swapchain_o *base, VkSemaphore semaphorePresentComplete, uint32_t &imageIndex ) {

	auto self = static_cast<img_data_o *const>( base->data );
	// This method will return the next avaliable vk image index for this swapchain, possibly
	// before this image is available for writing. Image will be ready for writing when
	// semaphorePresentComplete is signalled.

	// acquire next image, signal semaphore
	imageIndex = ( self->mImageIndex + 1 ) % self->mImagecount;

	auto fenceWaitResult = self->device.waitForFences( {self->transferFrames[ imageIndex ].frameFence}, VK_TRUE, 100'000'000 );

	if ( fenceWaitResult != ::vk::Result::eSuccess ) {
		assert( false ); // waiting for fence took too long.
	}

	self->device.resetFences( {self->transferFrames[ imageIndex ].frameFence} );

	self->mImageIndex = imageIndex;

	if ( self->ffmpeg_pipe ) {
		// TODO: we should be able to do the write on the back thread.
		// the back thread must signal that it is complete with writing
		// before the next present command is executed.

		// Write out frame contents to ffmpeg via pipe.
		auto const &frame = self->transferFrames[ imageIndex ];
		fwrite( frame.bufferAllocationInfo.pMappedData, self->mSwapchainExtent.width * self->mSwapchainExtent.height * 4, 1, self->ffmpeg_pipe );

	} else {
		char file_name[ 1024 ];
		sprintf( file_name, "isl_%08d.rgba", self->totalImages );
		auto const &  frame = self->transferFrames[ imageIndex ];
		std::ofstream myfile( file_name, std::ios::out | std::ios::binary );
		myfile.write( ( char * )frame.bufferAllocationInfo.pMappedData, self->mSwapchainExtent.width * self->mSwapchainExtent.height * 4 );
		myfile.close();
		std::cout << "Wrote Image: " << file_name << std::endl
		          << std::flush;
	}

	++self->totalImages;

	// The number of array elements must correspond to the number of wait semaphores, as each
	// mask specifies what the semaphore is waiting for.
	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = {::vk::PipelineStageFlagBits::eTransfer};

	auto presentCompleteSemaphore = vk::Semaphore{semaphorePresentComplete};

	::vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount( 0 )
	    .setPWaitSemaphores( nullptr )
	    .setPWaitDstStageMask( nullptr )
	    .setCommandBufferCount( 1 )
	    .setPCommandBuffers( &self->transferFrames[ imageIndex ].cmdAcquire ) // transfers image back to correct layout
	    .setSignalSemaphoreCount( 1 )
	    .setPSignalSemaphores( &presentCompleteSemaphore );

	// !TODO: instead of submitting to the default graphics queue, this needs to go to the transfer queue.

	{
		// We must fetch the default queue via the backend.
		//
		// Submitting directly via the queue is not very elegant, as the queue must be exernally synchronised, and
		// by submitting to the queue here we are living on the edge if we ever wanted
		// to have more than one thread producing frames.

		// we can use an event (signal here, command buffer waits for it)

		using namespace le_backend_vk;
		auto le_device_o = private_backend_vk_i.get_le_device( self->backend );
		auto queue       = vk::Queue{vk_device_i.get_default_graphics_queue( le_device_o )};

		queue.submit( 1, &submitInfo, nullptr );
	}

	return true;
}

// ----------------------------------------------------------------------

static bool swapchain_img_present( le_swapchain_o *base, VkQueue queue_, VkSemaphore renderCompleteSemaphore_, uint32_t *pImageIndex ) {

	auto self = static_cast<img_data_o *const>( base->data );

	vk::PipelineStageFlags wait_dst_stage_mask = ::vk::PipelineStageFlagBits::eColorAttachmentOutput;

	auto renderCompleteSemaphore = vk::Semaphore{renderCompleteSemaphore_};

	vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount( 1 )
	    .setPWaitSemaphores( &renderCompleteSemaphore ) // these are the renderComplete semaphores
	    .setPWaitDstStageMask( &wait_dst_stage_mask )
	    .setCommandBufferCount( 1 )
	    .setPCommandBuffers( &self->transferFrames[ *pImageIndex ].cmdPresent ) // copies image to buffer
	    .setSignalSemaphoreCount( 0 )
	    .setPSignalSemaphores( nullptr );

	// Todo: submit to transfer queue, not main queue, if possible

	{
		vk::Queue queue{queue_};
		queue.submit( {submitInfo}, self->transferFrames[ *pImageIndex ].frameFence );
	}

	return true;
};

// ----------------------------------------------------------------------

static VkImage swapchain_img_get_image( le_swapchain_o *base, uint32_t index ) {

	auto self = static_cast<img_data_o *const>( base->data );

#ifndef NDEBUG
	assert( index < self->transferFrames.size() );
#endif
	return self->transferFrames[ index ].image;
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR *swapchain_img_get_surface_format( le_swapchain_o *base ) {
	auto self = static_cast<img_data_o *const>( base->data );
	return &reinterpret_cast<VkSurfaceFormatKHR &>( self->windowSurfaceFormat );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_img_get_image_width( le_swapchain_o *base ) {
	auto self = static_cast<img_data_o *const>( base->data );
	return self->mSwapchainExtent.width;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_img_get_image_height( le_swapchain_o *base ) {

	auto self = static_cast<img_data_o *const>( base->data );
	return self->mSwapchainExtent.height;
}

// ----------------------------------------------------------------------

static size_t swapchain_img_get_swapchain_images_count( le_swapchain_o *base ) {
	auto self = static_cast<img_data_o *const>( base->data );
	return self->mImagecount;
}

// ----------------------------------------------------------------------

void register_le_swapchain_img_api( void *api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i = api->swapchain_img_i;

	swapchain_i.create             = swapchain_img_create;
	swapchain_i.destroy            = swapchain_img_destroy;
	swapchain_i.reset              = swapchain_img_reset;
	swapchain_i.acquire_next_image = swapchain_img_acquire_next_image;
	swapchain_i.get_image          = swapchain_img_get_image;
	swapchain_i.get_image_width    = swapchain_img_get_image_width;
	swapchain_i.get_image_height   = swapchain_img_get_image_height;
	swapchain_i.get_surface_format = swapchain_img_get_surface_format;
	swapchain_i.get_images_count   = swapchain_img_get_swapchain_images_count;
	swapchain_i.present            = swapchain_img_present;
}
