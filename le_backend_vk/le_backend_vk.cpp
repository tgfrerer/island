#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h"

#define VMA_USE_STL_CONTAINERS 1
#include "util/vk_mem_alloc/vk_mem_alloc.h" // for allocation

#include "le_backend_vk/le_backend_types_internal.h"

#include "le_swapchain_vk/le_swapchain_vk.h"
#include "pal_window/pal_window.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_backend_vk/util/spooky/SpookyV2.h" // for hashing renderpass gestalt

#include <vector>
#include <unordered_map>
#include <forward_list>
#include <iostream>
#include <iomanip>
#include <list>
#include <set>
#include <atomic>
#include <mutex>

#include <memory>

#ifndef PRINT_DEBUG_MESSAGES
#	define PRINT_DEBUG_MESSAGES false
#endif

struct ResourceInfo {
	// since this is a union, the first field will for both be VK_STRUCTURE_TYPE
	// and its value will tell us what type the descriptor represents.
	union {
		VkBufferCreateInfo bufferInfo; // | only one of either ever in use
		VkImageCreateInfo  imageInfo;  // | only one of either ever in use
	};

	bool operator==( const ResourceInfo &rhs ) const {
		if ( bufferInfo.sType == rhs.bufferInfo.sType ) {

			if ( bufferInfo.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO ) {

				return ( bufferInfo.flags == rhs.bufferInfo.flags &&
				         bufferInfo.size == rhs.bufferInfo.size &&
				         bufferInfo.usage == rhs.bufferInfo.usage &&
				         bufferInfo.sharingMode == rhs.bufferInfo.sharingMode &&
				         bufferInfo.queueFamilyIndexCount == rhs.bufferInfo.queueFamilyIndexCount &&
				         bufferInfo.pQueueFamilyIndices == rhs.bufferInfo.pQueueFamilyIndices );

			} else {

				return ( imageInfo.flags == rhs.imageInfo.flags &&
				         imageInfo.imageType == rhs.imageInfo.imageType &&
				         imageInfo.format == rhs.imageInfo.format &&
				         imageInfo.extent.width == rhs.imageInfo.extent.width &&
				         imageInfo.extent.height == rhs.imageInfo.extent.height &&
				         imageInfo.extent.depth == rhs.imageInfo.extent.depth &&
				         imageInfo.mipLevels == rhs.imageInfo.mipLevels &&
				         imageInfo.arrayLayers == rhs.imageInfo.arrayLayers &&
				         imageInfo.samples == rhs.imageInfo.samples &&
				         imageInfo.tiling == rhs.imageInfo.tiling &&
				         imageInfo.usage == rhs.imageInfo.usage &&
				         imageInfo.sharingMode == rhs.imageInfo.sharingMode &&
				         imageInfo.queueFamilyIndexCount == rhs.imageInfo.queueFamilyIndexCount &&
				         imageInfo.pQueueFamilyIndices == rhs.imageInfo.pQueueFamilyIndices &&
				         imageInfo.initialLayout == rhs.imageInfo.initialLayout );
			}

		} else {
			// not the same type of descriptor
			return false;
		}
	}

	bool operator!=( const ResourceInfo &rhs ) const {
		return !operator==( rhs );
	}

	bool isBuffer() const {
		return bufferInfo.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	}

	enum class ConsolidationResult : uint8_t {
		eOk = 0,
		eIncompatibleTypes, // can't consolidate two resources of non-matching resource type
	};

	// combines usage flags for image and buffer resources, returns false if
	// there was an error, otherwise, modifies first resource in-place
	static ResourceInfo consolidate( const ResourceInfo &lhs, const ResourceInfo &rhs, ResourceInfo::ConsolidationResult *errNo ) {
		auto result = lhs;

		if ( lhs.bufferInfo.sType != rhs.bufferInfo.sType ) {
			*errNo = ConsolidationResult::eIncompatibleTypes;
			return result;
		}

		// --------| invariant: types are identical

		if ( lhs.isBuffer() ) {
			// buffer resources
			result.bufferInfo.usage |= rhs.bufferInfo.usage;
		} else {
			// image resources
			result.imageInfo.usage |= rhs.imageInfo.usage;
		}

		return result;
	};
};

// ------------------------------------------------------------

struct AllocatedResource {
	VmaAllocation     allocation;
	VmaAllocationInfo allocationInfo;
	union {
		VkBuffer asBuffer;
		VkImage  asImage;
	};
	ResourceInfo info; // Details on resource
};

// herein goes all data which is associated with the current frame
// backend keeps track of multiple frames, exactly one per renderer::FrameData frame.
struct BackendFrameData {
	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	uint32_t                       swapchainWidth           = 0; // swapchain may be resized, therefore it needs to be stored with frame
	uint32_t                       swapchainHeight          = 0; // swapchain may be resized, therefore it needs to be stored with frame
	uint32_t                       padding                  = 0; // NOTICE: remove if needed.
	std::vector<vk::CommandBuffer> commandBuffers;

	struct Texture {
		vk::Sampler   sampler;
		vk::ImageView imageView;
	};

	std::unordered_map<le_resource_handle_t, Texture, LeResourceHandleIdentity> textures; // non-owning, references to frame-local textures, cleared on frame fence.

	// ResourceState keeps track of the resource stage *before* a barrier
	struct ResourceState {
		vk::AccessFlags        visible_access; // which memory access must be be visible - if any of these are WRITE accesses, these must be made available(flushed) before next access
		vk::PipelineStageFlags write_stage;    // current or last stage at which write occurs
		vk::ImageLayout        layout;         // current layout (for images)
	};

	// With `syncChainTable` and image_attachment_info_o.syncState, we should
	// be able to create renderpasses. Each resource has a sync chain, and each attachment_info
	// has a struct which holds indices into the sync chain telling us where to look
	// up the sync state for a resource at different stages of renderpass construction.
	std::unordered_map<le_resource_handle_t, std::vector<ResourceState>, LeResourceHandleIdentity> syncChainTable;

	static_assert( sizeof( VkBuffer ) == sizeof( VkImageView ) && sizeof( VkBuffer ) == sizeof( VkImage ), "size of AbstractPhysicalResource components must be identical" );

	// Todo: clarify ownership of physical resources inside FrameData
	// Q: Does this table actually own the resources?
	// A: It must not: as it is used to map external resources as well.
	std::unordered_map<le_resource_handle_t, AbstractPhysicalResource, LeResourceHandleIdentity> physicalResources; // map from renderer resource id to physical resources - only contains resources this frame uses.

	/// \brief vk resources retained and destroyed with BackendFrameData
	std::forward_list<AbstractPhysicalResource> ownedResources;

	std::vector<LeRenderPass>       passes;
	std::vector<vk::DescriptorPool> descriptorPools; // one descriptor pool per pass

	/*

	  Each Frame has one allocation Pool from which all allocations for scratch buffers are drawn.

	  When creating encoders, each encoder has their own sub-allocator, each sub-allocator owns an
	  independent block of memory allcated from the frame pool. This way, encoders can work in their
	  own thread.

	 */

	std::unordered_map<le_resource_handle_t, AllocatedResource, LeResourceHandleIdentity> availableResources; // resources this frame may use
	std::unordered_map<le_resource_handle_t, AllocatedResource, LeResourceHandleIdentity> binnedResources;    // resources to delete when this frame comes round to clear()

	VmaPool                        allocationPool;   // pool from which allocations for this frame come from
	std::vector<le_allocator_o *>  allocators;       // one linear sub-allocator per command buffer
	std::vector<vk::Buffer>        allocatorBuffers; // one vkBuffer per command buffer
	std::vector<VmaAllocation>     allocations;      // one allocation per command buffer
	std::vector<VmaAllocationInfo> allocationInfos;  // one allocation info per command buffer
};

// ----------------------------------------------------------------------

/// \brief backend data object
struct le_backend_o {

	le_backend_vk_settings_t settings;

	std::unique_ptr<le::Instance> instance;
	std::unique_ptr<le::Device>   device;

	std::unique_ptr<pal::Window>   window; // non-owning
	std::unique_ptr<le::Swapchain> swapchain;
	vk::Format                     swapchainImageFormat = {}; ///< default image format used for swapchain (backbuffer image must be in this format)

	std::vector<BackendFrameData> mFrames;

	le_pipeline_manager_o *pipelineCache = nullptr;

	VmaAllocator mAllocator = nullptr;

	uint32_t queueFamilyIndexGraphics = 0; // set during setup
	uint32_t queueFamilyIndexCompute  = 0; // set during setup

	const le_resource_handle_t backBufferImageHandle = LE_IMG_RESOURCE( "Backbuffer-Image" ); // opaque handle identifying the backbuffer image, initialised in setup()

	struct {
		std::unordered_map<le_resource_handle_t, AllocatedResource, LeResourceHandleIdentity> allocatedResources; // allocated resources, indexed by resource name hash
	} only_backend_allocate_resources_may_access;                                                                 // only acquire_physical_resources may read/write

	const vk::BufferUsageFlags LE_BUFFER_USAGE_FLAGS_SCRATCH =
	    vk::BufferUsageFlagBits::eIndexBuffer |
	    vk::BufferUsageFlagBits::eVertexBuffer |
	    vk::BufferUsageFlagBits::eUniformBuffer |
	    vk::BufferUsageFlagBits::eTransferSrc;
};

// ----------------------------------------------------------------------
static inline VkAttachmentStoreOp le_to_vk( const LeAttachmentStoreOp &lhs ) {
	switch ( lhs ) {
	case ( LE_ATTACHMENT_STORE_OP_STORE ):
	    return VK_ATTACHMENT_STORE_OP_STORE;
	case ( LE_ATTACHMENT_STORE_OP_DONTCARE ):
	    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	assert( false ); // if control ends here, something is wrong, as switch must cover all cases
	return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

// ----------------------------------------------------------------------
static inline VkAttachmentLoadOp le_to_vk( const LeAttachmentLoadOp &lhs ) {
	switch ( lhs ) {
	case ( LE_ATTACHMENT_LOAD_OP_LOAD ):
	    return VK_ATTACHMENT_LOAD_OP_LOAD;
	case ( LE_ATTACHMENT_LOAD_OP_CLEAR ):
	    return VK_ATTACHMENT_LOAD_OP_CLEAR;
	case ( LE_ATTACHMENT_LOAD_OP_DONTCARE ):
	    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}
	assert( false ); // if control ends here, something is wrong, as switch must cover all cases
	return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

// ----------------------------------------------------------------------

static inline const vk::ClearValue &le_to_vk( const LeClearValue &lhs ) {
	static_assert( sizeof( vk::ClearValue ) == sizeof( LeClearValue ), "clear value must be of equal size" );
	return reinterpret_cast<const vk::ClearValue &>( lhs );
}

// ----------------------------------------------------------------------

static inline bool is_depth_stencil_format( vk::Format format_ ) {
	return ( format_ >= vk::Format::eD16Unorm && format_ <= vk::Format::eD32SfloatS8Uint );
}

// ----------------------------------------------------------------------

static le_backend_o *backend_create() {
	auto self = new le_backend_o;
	return self;
}

// ----------------------------------------------------------------------

static void backend_destroy( le_backend_o *self ) {

	if ( self->pipelineCache ) {
		using namespace le_backend_vk;
		le_pipeline_manager_i.destroy( self->pipelineCache );
		self->pipelineCache = nullptr;
	}

	vk::Device device = self->device->getVkDevice(); // may be nullptr if device was not created

	for ( auto &frameData : self->mFrames ) {

		using namespace le_backend_vk;

		// -- destroy per-frame data

		device.destroyFence( frameData.frameFence );
		device.destroySemaphore( frameData.semaphorePresentComplete );
		device.destroySemaphore( frameData.semaphoreRenderComplete );
		device.destroyCommandPool( frameData.commandPool );

		for ( auto &d : frameData.descriptorPools ) {
			device.destroyDescriptorPool( d );
		}

		// destroy per-allocator buffers
		for ( auto &b : frameData.allocatorBuffers ) {
			device.destroyBuffer( b );
		}

		for ( auto &a : frameData.allocators ) {
			le_allocator_linear_i.destroy( a );
		}
		frameData.allocators.clear();
		frameData.allocationInfos.clear();

		vmaMakePoolAllocationsLost( self->mAllocator, frameData.allocationPool, nullptr );
		vmaDestroyPool( self->mAllocator, frameData.allocationPool );

		// remove any binned resources
		for ( auto &a : frameData.binnedResources ) {

			if ( a.second.info.isBuffer() ) {
				device.destroyBuffer( a.second.asBuffer );
			} else {
				device.destroyImage( a.second.asImage );
			}

			vmaFreeMemory( self->mAllocator, a.second.allocation );
		}
		frameData.binnedResources.clear();
	}

	self->mFrames.clear();

	// Remove any resources still alive in the backend.
	// At this point we're running single-threaded, so we can ignore the
	// ownership claim on allocatedResources.
	for ( auto &a : self->only_backend_allocate_resources_may_access.allocatedResources ) {

		if ( a.second.info.isBuffer() ) {
			device.destroyBuffer( a.second.asBuffer );
		} else {
			device.destroyImage( a.second.asImage );
		}

		vmaFreeMemory( self->mAllocator, a.second.allocation );
	}

	self->only_backend_allocate_resources_may_access.allocatedResources.clear();

	if ( self->mAllocator ) {
		vmaDestroyAllocator( self->mAllocator );
		self->mAllocator = nullptr;
	}

	delete self;
}

// ----------------------------------------------------------------------

static bool backend_create_window_surface( le_backend_o *self, pal_window_o *window_ ) {
	self->window = std::make_unique<pal::Window>( window_ );
	assert( self->instance );
	assert( self->instance->getVkInstance() );
	bool success = self->window->createSurface( self->instance->getVkInstance() );
	return success;
}

// ----------------------------------------------------------------------

static void backend_create_swapchain( le_backend_o *self, le_swapchain_vk_settings_t *swapchainSettings_ ) {

	assert( self->window );

	le_swapchain_vk_settings_t settings{};

	if ( swapchainSettings_ ) {
		settings = *swapchainSettings_;
	}

	// Set default settings if not user specified for certain swapchain settings

	if ( settings.imagecount_hint == 0 ) {
		settings.imagecount_hint = 3;
	}

	if ( settings.presentmode_hint == le::Swapchain::Presentmode::eDefault ) {
		settings.presentmode_hint = le::Swapchain::Presentmode::eFifo;
	}

	// The following settings are not user-hintable, and will get overridden by default

	settings.width_hint                     = self->window->getSurfaceWidth();
	settings.height_hint                    = self->window->getSurfaceHeight();
	settings.vk_device                      = self->device->getVkDevice();
	settings.vk_physical_device             = self->device->getVkPhysicalDevice();
	settings.vk_surface                     = self->window->getVkSurfaceKHR();
	settings.vk_graphics_queue_family_index = self->device->getDefaultGraphicsQueueFamilyIndex();

	self->swapchain            = std::make_unique<le::Swapchain>( &settings );
	self->swapchainImageFormat = vk::Format( self->swapchain->getSurfaceFormat()->format );
}

// ----------------------------------------------------------------------

static size_t backend_get_num_swapchain_images( le_backend_o *self ) {
	assert( self->swapchain );
	return self->swapchain->getImagesCount();
}

// ----------------------------------------------------------------------

static void backend_reset_swapchain( le_backend_o *self ) {
	self->swapchain->reset();
}

// ----------------------------------------------------------------------

/// \brief Declare a resource as a virtual buffer
/// \details This is an internal method. Virtual buffers are buffers which don't have individual
/// Vulkan buffer backing. Instead, they use their Frame's buffer for storage. Virtual buffers
/// are used to store Frame-local transient data such as values for shader parameters.
/// Each Encoder uses its own virtual buffer for such purposes.
static le_resource_handle_t declare_resource_virtual_buffer( uint8_t index ) {
	auto resource = LE_RESOURCE( "Encoder-Virtual", LeResourceType::eBuffer ); // virtual resources all have the same id, 0, which means they are not part of the regular roster of resources...

	{
		resource.meta.index = index; // encoder index
		resource.meta.flags = le_resource_handle_t::FlagBits::eIsVirtual;
	}

	return resource;
}

// ----------------------------------------------------------------------

static le_resource_handle_t backend_get_backbuffer_resource( le_backend_o *self ) {
	return self->backBufferImageHandle;
}

// ----------------------------------------------------------------------

static void backend_setup( le_backend_o *self, le_backend_vk_settings_t *settings ) {

	self->settings = *settings;

	// -- if window surface, query required vk extensions from glfw

	std::vector<char const *> requestedInstanceExtensions;
	{
		if ( self->settings.pWindow ) {

			// -- insert extensions necessary for glfw window

			uint32_t extensionCount         = 0;
			auto     glfwRequiredExtensions = pal::Window( self->settings.pWindow ).getRequiredVkExtensions( &extensionCount );

			requestedInstanceExtensions.insert( requestedInstanceExtensions.end(),
			                                    glfwRequiredExtensions,
			                                    glfwRequiredExtensions + extensionCount );
		}

		// -- insert any additionally requested extensions
		requestedInstanceExtensions.insert( requestedInstanceExtensions.end(),
		                                    settings->requestedExtensions,
		                                    settings->requestedExtensions + settings->numRequestedExtensions );
	}
	// -- initialise backend

	self->instance = std::make_unique<le::Instance>( requestedInstanceExtensions.data(), requestedInstanceExtensions.size() );
	self->device   = std::make_unique<le::Device>( *self->instance );

	{
		using namespace le_backend_vk;
		self->pipelineCache = le_pipeline_manager_i.create( self->device->getVkDevice() );
	}

	// -- create window surface if requested

	if ( self->settings.pWindow ) {
		backend_create_window_surface( self, self->settings.pWindow );
	}

	// -- create swapchain if requested

	backend_create_swapchain( self, self->settings.swapchain_settings );

	// -- setup backend memory objects

	auto frameCount = backend_get_num_swapchain_images( self );

	self->mFrames.reserve( frameCount );

	vk::Device         vkDevice         = self->device->getVkDevice();
	vk::PhysicalDevice vkPhysicalDevice = self->device->getVkPhysicalDevice();

	self->queueFamilyIndexGraphics = self->device->getDefaultGraphicsQueueFamilyIndex();
	self->queueFamilyIndexCompute  = self->device->getDefaultComputeQueueFamilyIndex();

	uint32_t memIndexScratchBufferGraphics = 0;
	{
		// -- Create allocator for backend vulkan memory
		{
			VmaAllocatorCreateInfo createInfo{};
			createInfo.flags                       = 0;
			createInfo.device                      = vkDevice;
			createInfo.frameInUseCount             = 0;
			createInfo.physicalDevice              = vkPhysicalDevice;
			createInfo.preferredLargeHeapBlockSize = 0; // set to default, currently 256 MB

			vmaCreateAllocator( &createInfo, &self->mAllocator );
		}

		{
			// Find memory index for scratch buffer - we do this by pretending to create
			// an allocation.

			vk::BufferCreateInfo bufferInfo{};
			bufferInfo
			    .setFlags( {} )
			    .setSize( 1 )
			    .setUsage( self->LE_BUFFER_USAGE_FLAGS_SCRATCH )
			    .setSharingMode( vk::SharingMode::eExclusive )
			    .setQueueFamilyIndexCount( 1 )
			    .setPQueueFamilyIndices( &self->queueFamilyIndexGraphics );

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

			vmaFindMemoryTypeIndexForBufferInfo( self->mAllocator, reinterpret_cast<VkBufferCreateInfo *>( &bufferInfo ), &allocInfo, &memIndexScratchBufferGraphics );
		}
	}

	assert( vkDevice ); // device must come from somewhere! It must have been introduced to backend before, or backend must create device used by everyone else...

	for ( size_t i = 0; i != frameCount; ++i ) {

		// -- Set up per-frame resources

		BackendFrameData frameData{};

		frameData.frameFence               = vkDevice.createFence( {} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = vkDevice.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = vkDevice.createSemaphore( {} );
		frameData.commandPool              = vkDevice.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->device->getDefaultGraphicsQueueFamilyIndex()} );

		{
			// -- set up an allocation pool for each frame
			// so that each frame can create sub-allocators
			// when it creates command buffers for each frame.

			VmaPoolCreateInfo aInfo{};
			aInfo.blockSize       = 1u << 24; // 16.77MB
			aInfo.flags           = VmaPoolCreateFlagBits::VMA_POOL_CREATE_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT;
			aInfo.memoryTypeIndex = memIndexScratchBufferGraphics;
			aInfo.frameInUseCount = 0;
			aInfo.minBlockCount   = 1;
			vmaCreatePool( self->mAllocator, &aInfo, &frameData.allocationPool );
		}

		self->mFrames.emplace_back( std::move( frameData ) );
	}

	// CHECK: this is where we used to create the vulkan pipeline cache object
}

// ----------------------------------------------------------------------

static void frame_track_resource_state( BackendFrameData &frame, le_renderpass_o **ppPasses, size_t numRenderPasses, const vk::Format &swapchainImageFormat, const le_resource_handle_t &backbufferImageHandle ) {

	// Track resource state

	// we should mark persistent resources which are not frame-local with special flags, so that they
	// come with an initial element in their sync chain, this element signals their last (frame-crossing) state
	// this naturally applies to "backbuffer", for example.

	// A pipeline barrier is defined as a combination of EXECUTION dependency and MEMORY dependency:
	//
	// * An EXECUTION DEPENDENCY tells us which stage needs to be complete (srcStage) before another named stage (dstStage) may execute.
	// * A  MEMORY DEPENDECY     tells us which memory needs to be made available/flushed (srcAccess) after srcStage,
	//   before another memory can be made visible/invalidated (dstAccess) before dstStage

	auto &syncChainTable = frame.syncChainTable;

	{
		// TODO: frame-external ("persistent") resources such as backbuffer
		// need to be correctly initialised:
		//

		auto backbufferIt = syncChainTable.find( backbufferImageHandle );
		if ( backbufferIt != syncChainTable.end() ) {
			auto &backbufferState          = backbufferIt->second.front();
			backbufferState.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput; // we need this, since semaphore waits on this stage
			backbufferState.visible_access = vk::AccessFlagBits( 0 );                           // semaphore took care of availability - we can assume memory is already available
		} else {
			std::cout << "WARNING: no reference to backbuffer found in renderpasses" << std::endl
			          << std::flush;
		}
	}

	// Renderpass implicit sync (per image resource):
	//
	// + Enter renderpass : INITIAL LAYOUT (layout must match)
	// + Layout transition if initial layout and attachment reference layout differ for subpass
	//   [ attachment memory is automatically made AVAILABLE | see Spec 6.1.1]
	//   [layout transition happens-before any LOAD OPs: (Source: amd open source driver <https://github.com/GPUOpen-Drivers/xgl/blob/aa330d8e9acffb578c88193e4abe017c8fe15426/icd/api/renderpass/renderpass_builder.cpp#L819>)]
	// + Load/clear op (executed using INITIAL LAYOUT once before first use per-resource)
	//   [ attachment memory must be AVAILABLE ]
	// + Enter subpass
	// + Command execution [attachment memory must be VISIBLE ]
	// + Store op
	// + Exit subpass : final layout
	// + Exit renderpass
	// + Layout transform (if final layout differs)

	static auto const &renderpass_i = Registry::getApi<le_renderer_api>()->le_renderpass_i;

	frame.passes.reserve( numRenderPasses );

	// TODO: move pass creation to its own method.

	for ( auto pass = ppPasses; pass != ppPasses + numRenderPasses; pass++ ) {

		LeRenderPass currentPass{};
		currentPass.type = renderpass_i.get_type( *pass );

		currentPass.width  = renderpass_i.get_width( *pass );
		currentPass.height = renderpass_i.get_height( *pass );

		// iterate over all image attachments

		LeImageAttachmentInfo const *pImageAttachments   = nullptr;
		size_t                       numImageAttachments = 0;
		renderpass_i.get_image_attachments( *pass, &pImageAttachments, &numImageAttachments );
		for ( auto const *imageAttachment = pImageAttachments; imageAttachment != pImageAttachments + numImageAttachments; imageAttachment++ ) {

			auto &syncChain = syncChainTable[ imageAttachment->resource_id ];

			vk::Format attachmentFormat = vk::Format( imageAttachment->format );

			if ( attachmentFormat == vk::Format::eUndefined ) {
				// If an attachment does not explicitly specify a format, use the format of the associated image resource instead.
				attachmentFormat = vk::Format( frame.availableResources[ imageAttachment->resource_id ].info.imageInfo.format );
			}

			bool isDepthStencil = is_depth_stencil_format( attachmentFormat );

			AttachmentInfo *currentAttachment = ( currentPass.attachments + ( currentPass.numColorAttachments + currentPass.numDepthStencilAttachments ) );

			if ( isDepthStencil ) {
				currentPass.numDepthStencilAttachments++;
			} else {
				currentPass.numColorAttachments++;
			}

			currentAttachment->resource_id = imageAttachment->resource_id;
			currentAttachment->format      = attachmentFormat;
			currentAttachment->loadOp      = vk::AttachmentLoadOp( le_to_vk( imageAttachment->loadOp ) );
			currentAttachment->storeOp     = vk::AttachmentStoreOp( le_to_vk( imageAttachment->storeOp ) );
			currentAttachment->clearValue  = le_to_vk( imageAttachment->clearValue );

			{
				// track resource state before entering a subpass

				auto &previousSyncState = syncChain.back();
				auto  beforeFirstUse{previousSyncState};

				if ( currentAttachment->loadOp == vk::AttachmentLoadOp::eLoad ) {
					// we must now specify which stages need to be visible for which coming memory access
					if ( isDepthStencil ) {
						beforeFirstUse.visible_access = vk::AccessFlagBits::eDepthStencilAttachmentRead;
						beforeFirstUse.write_stage    = vk::PipelineStageFlagBits::eEarlyFragmentTests;

					} else {
						// we need to make visible the information from color attachment output stage
						// to anyone using read or write on the color attachment.
						beforeFirstUse.visible_access = vk::AccessFlagBits::eColorAttachmentRead;
						beforeFirstUse.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
					}
				} else if ( currentAttachment->loadOp == vk::AttachmentLoadOp::eClear ) {
					// resource.loadOp must be either CLEAR / or DONT_CARE
					beforeFirstUse.write_stage    = isDepthStencil ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eColorAttachmentOutput;
					beforeFirstUse.visible_access = vk::AccessFlagBits( 0 );
				}

				currentAttachment->initialStateOffset = uint16_t( syncChain.size() );
				syncChain.emplace_back( std::move( beforeFirstUse ) ); // attachment initial state for a renderpass - may be loaded/cleared on first use
				                                                       // * sync state: ready for load/store *
			}

			{
				// track resource state before subpass

				auto &previousSyncState = syncChain.back();
				auto  beforeSubpass{previousSyncState};

				if ( imageAttachment->loadOp == LE_ATTACHMENT_LOAD_OP_LOAD ) {
					// resource.loadOp most be LOAD

					// we must now specify which stages need to be visible for which coming memory access
					if ( isDepthStencil ) {
						beforeSubpass.visible_access = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eEarlyFragmentTests;
						beforeSubpass.layout         = vk::ImageLayout::eDepthStencilAttachmentOptimal;
					} else {
						// we need to make visible the information from color attachment output stage
						// to anyone using read or write on the color attachment.
						beforeSubpass.visible_access = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						beforeSubpass.layout         = vk::ImageLayout::eColorAttachmentOptimal;
					}

				} else {

					// load op is either CLEAR, or DONT_CARE

					if ( isDepthStencil ) {
						beforeSubpass.visible_access = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eEarlyFragmentTests;
						beforeSubpass.layout         = vk::ImageLayout::eDepthStencilAttachmentOptimal;
					} else {
						beforeSubpass.visible_access = vk::AccessFlagBits::eColorAttachmentWrite;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						beforeSubpass.layout         = vk::ImageLayout::eColorAttachmentOptimal;
					}
				}

				syncChain.emplace_back( std::move( beforeSubpass ) );
			}

			// TODO: here, go through command instructions for renderpass and update resource chain

			// ... NOTE: if resource is modified by commands inside the renderpass, this needs to be added to the sync chain here.

			{
				// Whichever next resource state will be in the sync chain will be the resource state we should transition to
				// when defining the last_subpass_to_external dependency
				// which is why, optimistically, we designate the index of the next, not yet written state here -
				currentAttachment->finalStateOffset = uint16_t( syncChain.size() );
			}

			// print out info for this resource at this pass.
		}

		// Note that we "steal" the encoder from the renderer pass -
		// it becomes now our (the backend's) job to destroy it.
		currentPass.encoder = renderpass_i.steal_encoder( *pass );

		frame.passes.emplace_back( std::move( currentPass ) );
	}

	// TODO: add final states for resources which are permanent - or are used on another queue
	// this includes backbuffer, and makes sure the backbuffer transitions to the correct state in its last
	// subpass dependency.

	for ( auto &syncChainPair : syncChainTable ) {
		const auto &id        = syncChainPair.first;
		auto &      syncChain = syncChainPair.second;

		auto finalState{syncChain.back()};

		if ( id == backbufferImageHandle ) {
			finalState.write_stage    = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.visible_access = vk::AccessFlagBits::eMemoryRead;
			finalState.layout         = vk::ImageLayout::ePresentSrcKHR;
		} else {
			// we mimick implicit dependency here, which exists for a final subpass
			// see p.210 vk spec (chapter 7, render pass)
			finalState.write_stage    = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.visible_access = vk::AccessFlagBits( 0 );
		}

		syncChain.emplace_back( std::move( finalState ) );
	}
}

// ----------------------------------------------------------------------

/// \brief polls frame fence, returns true if fence has been crossed, false otherwise.
static bool backend_poll_frame_fence( le_backend_o *self, size_t frameIndex ) {
	auto &     frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	auto result = device.waitForFences( {frame.frameFence}, true, 1000'000'000 );
	// auto result = device.getFenceStatus( {frame.frameFence} );

	if ( result != vk::Result::eSuccess ) {
		return false;
	} else {
		return true;
	}
}

// ----------------------------------------------------------------------
/// \brief: Frees all frame local resources
/// \preliminary: frame fence must have been crossed.
static bool backend_clear_frame( le_backend_o *self, size_t frameIndex ) {

	static auto &backendI   = *Registry::getApi<le_backend_vk_api>();
	static auto &allocatorI = backendI.le_allocator_linear_i;

	auto &     frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	//	auto result = device.waitForFences( {frame.frameFence}, true, 100'000'000 );

	//	if ( result != vk::Result::eSuccess ) {
	//		return false;
	//	}

	// -------- Invariant: fence has been crossed, all resources protected by fence
	//          can now be claimed back.

	device.resetFences( {frame.frameFence} );

	// -- reset all frame-local sub-allocators
	for ( auto &alloc : frame.allocators ) {
		allocatorI.reset( alloc );
	}

	// -- remove any texture references

	frame.textures.clear();

	// -- remove any frame-local copy of allocated resources
	frame.availableResources.clear();

	for ( auto &d : frame.descriptorPools ) {
		device.resetDescriptorPool( d );
	}

	{ // clear resources owned exclusively by this frame

		for ( auto &r : frame.ownedResources ) {
			switch ( r.type ) {
			case AbstractPhysicalResource::eBuffer:
				device.destroyBuffer( r.asBuffer );
			    break;
			case AbstractPhysicalResource::eFramebuffer:
				device.destroyFramebuffer( r.asFramebuffer );
			    break;
			case AbstractPhysicalResource::eImage:
				device.destroyImage( r.asImage );
			    break;
			case AbstractPhysicalResource::eImageView:
				device.destroyImageView( r.asImageView );
			    break;
			case AbstractPhysicalResource::eRenderPass:
				device.destroyRenderPass( r.asRenderPass );
			    break;
			case AbstractPhysicalResource::eSampler:
				device.destroySampler( r.asSampler );
			    break;

			case AbstractPhysicalResource::eUndefined:
				std::cout << __PRETTY_FUNCTION__ << ": abstract physical resource has unknown type (" << std::hex << r.type << ") and cannot be deleted. leaking..." << std::flush;
			    break;
			}
		}
		frame.ownedResources.clear();
	}

	device.freeCommandBuffers( frame.commandPool, frame.commandBuffers );
	frame.commandBuffers.clear();

	// todo: we should probably notify anyone who wanted to recycle these
	// physical resources that they are not in use anymore.
	frame.physicalResources.clear();
	frame.syncChainTable.clear();

	for ( auto &f : frame.passes ) {
		if ( f.encoder ) {
			using namespace le_renderer;
			encoder_i.destroy( f.encoder );
			f.encoder = nullptr;
		}
	}
	frame.passes.clear();

	device.resetCommandPool( frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources );

	return true;
};

// ----------------------------------------------------------------------

static void backend_create_renderpasses( BackendFrameData &frame, vk::Device &device ) {

	// NOTE: we might be able to simplify this along the lines of
	// <https://github.com/Tobski/simple_vulkan_synchronization>
	// <https://github.com/gwihlidal/vk-sync-rs>

	// create renderpasses
	const auto &syncChainTable = frame.syncChainTable;

	// we use this to mask out any reads in srcAccess, as it never makes sense to flush reads
	const auto ANY_WRITE_ACCESS_FLAGS = ( vk::AccessFlagBits::eColorAttachmentWrite |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eDepthStencilAttachmentWrite |
	                                      vk::AccessFlagBits::eHostWrite |
	                                      vk::AccessFlagBits::eMemoryWrite |
	                                      vk::AccessFlagBits::eShaderWrite |
	                                      vk::AccessFlagBits::eTransferWrite );

	for ( size_t i = 0; i != frame.passes.size(); ++i ) {

		auto &pass = frame.passes[ i ];

		if ( pass.type != LE_RENDER_PASS_TYPE_DRAW ) {
			continue;
		}

		std::vector<vk::AttachmentDescription> attachments;
		attachments.reserve( pass.numColorAttachments + pass.numDepthStencilAttachments );

		std::vector<vk::AttachmentReference>     colorAttachmentReferences;
		std::unique_ptr<vk::AttachmentReference> depthAttachmentReference;

		// We must accumulate these flags over all attachments - they are the
		// union of all flags required by all attachments in a pass.
		vk::PipelineStageFlags srcStageFromExternalFlags;
		vk::PipelineStageFlags dstStageFromExternalFlags;
		vk::AccessFlags        srcAccessFromExternalFlags;
		vk::AccessFlags        dstAccessFromExternalFlags;

		vk::PipelineStageFlags srcStageToExternalFlags;
		vk::PipelineStageFlags dstStageToExternalFlags;
		vk::AccessFlags        srcAccessToExternalFlags;
		vk::AccessFlags        dstAccessToExternalFlags;

		for ( AttachmentInfo const *attachment = pass.attachments; attachment != pass.attachments + ( pass.numColorAttachments + pass.numDepthStencilAttachments ); attachment++ ) {

			//#ifndef NDEBUG
			//			if ( attachment->resource_id == nullptr ) {
			//				std::cerr << "[ FATAL ] Use of undeclared resource handle. Did you forget to declare this resource handle with the renderer?" << std::endl
			//				          << std::flush;
			//			}
			//			assert( attachment->resource_id != nullptr ); // resource id must not be zero: did you forget to declare this resource with the renderer via renderer->declareResource?
			//#endif

			auto &syncChain = syncChainTable.at( attachment->resource_id );

			const auto &syncInitial = syncChain.at( attachment->initialStateOffset );
			const auto &syncSubpass = syncChain.at( attachment->initialStateOffset + 1 );
			const auto &syncFinal   = syncChain.at( attachment->finalStateOffset );

			bool isDepthStencil = is_depth_stencil_format( attachment->format );

			vk::AttachmentDescription attachmentDescription{};
			attachmentDescription
			    .setFlags( vk::AttachmentDescriptionFlags() ) // relevant for compatibility
			    .setFormat( attachment->format )              // relevant for compatibility
			    .setSamples( vk::SampleCountFlagBits::e1 )    // relevant for compatibility
			    .setLoadOp( attachment->loadOp )
			    .setStoreOp( attachment->storeOp )
			    .setStencilLoadOp( isDepthStencil ? attachment->loadOp : vk::AttachmentLoadOp::eDontCare )
			    .setStencilStoreOp( isDepthStencil ? attachment->storeOp : vk::AttachmentStoreOp::eDontCare )
			    .setInitialLayout( syncInitial.layout )
			    .setFinalLayout( syncFinal.layout );

			if ( PRINT_DEBUG_MESSAGES ) {
				std::cout << "attachment: " << std::hex << attachment->resource_id.debug_name << std::endl;
				std::cout << "layout initial: " << vk::to_string( syncInitial.layout ) << std::endl;
				std::cout << "layout subpass: " << vk::to_string( syncSubpass.layout ) << std::endl;
				std::cout << "layout   final: " << vk::to_string( syncFinal.layout ) << std::endl;
			}

			attachments.emplace_back( attachmentDescription );

			if ( isDepthStencil ) {
				depthAttachmentReference = std::make_unique<vk::AttachmentReference>( attachments.size() - 1, syncSubpass.layout );
			} else {
				colorAttachmentReferences.emplace_back( attachments.size() - 1, syncSubpass.layout );
			}

			srcStageFromExternalFlags |= syncInitial.write_stage;
			dstStageFromExternalFlags |= syncSubpass.write_stage;
			srcAccessFromExternalFlags |= ( syncInitial.visible_access & ANY_WRITE_ACCESS_FLAGS );
			dstAccessFromExternalFlags |= syncSubpass.visible_access; // & ~(syncInitial.visible_access ); // this would make only changes in availability operations happen. it should only happen if there are no src write_access_flags. we leave this out so as to give the driver more info

			// TODO: deal with other subpasses ...

			srcStageToExternalFlags |= syncChain.at( attachment->finalStateOffset - 1 ).write_stage;
			dstStageToExternalFlags |= syncFinal.write_stage;
			srcAccessToExternalFlags |= ( syncChain.at( attachment->finalStateOffset - 1 ).visible_access & ANY_WRITE_ACCESS_FLAGS );
			dstAccessToExternalFlags |= syncFinal.visible_access;

			if ( 0 == static_cast<unsigned int>( srcStageFromExternalFlags ) ) {
				// Ensure that the stage mask is valid if no stages were specified.
				srcStageFromExternalFlags = vk::PipelineStageFlagBits::eTopOfPipe;
			}
		}

		std::vector<vk::SubpassDescription> subpasses;
		subpasses.reserve( 1 );

		vk::SubpassDescription subpassDescription;
		subpassDescription
		    .setFlags( vk::SubpassDescriptionFlags() )
		    .setPipelineBindPoint( vk::PipelineBindPoint::eGraphics )
		    .setInputAttachmentCount( 0 )
		    .setPInputAttachments( nullptr )
		    .setColorAttachmentCount( uint32_t( colorAttachmentReferences.size() ) )
		    .setPColorAttachments( colorAttachmentReferences.data() )
		    .setPResolveAttachments( nullptr ) // must be NULL or have same length as colorAttachments
		    .setPDepthStencilAttachment( depthAttachmentReference.get() )
		    .setPreserveAttachmentCount( 0 )
		    .setPPreserveAttachments( nullptr );

		subpasses.emplace_back( subpassDescription );

		std::vector<vk::SubpassDependency> dependencies;
		dependencies.reserve( 2 );
		{
			if ( PRINT_DEBUG_MESSAGES ) {

				std::cout << "PASS :'"
				          << "index: " << i << " / "
				          << "FIXME: need pass name / identifier "
				          << "'" << std::endl;
				std::cout << "Subpass Dependency: VK_SUBPASS_EXTERNAL to subpass [0]" << std::endl;
				std::cout << "\t srcStage: " << vk::to_string( srcStageFromExternalFlags ) << std::endl;
				std::cout << "\t dstStage: " << vk::to_string( dstStageFromExternalFlags ) << std::endl;
				std::cout << "\tsrcAccess: " << vk::to_string( srcAccessFromExternalFlags ) << std::endl;
				std::cout << "\tdstAccess: " << vk::to_string( dstAccessFromExternalFlags ) << std::endl
				          << std::endl;

				std::cout << "Subpass Dependency: subpass [0] to VK_SUBPASS_EXTERNAL:" << std::endl;
				std::cout << "\t srcStage: " << vk::to_string( srcStageToExternalFlags ) << std::endl;
				std::cout << "\t dstStage: " << vk::to_string( dstStageToExternalFlags ) << std::endl;
				std::cout << "\tsrcAccess: " << vk::to_string( srcAccessToExternalFlags ) << std::endl;
				std::cout << "\tdstAccess: " << vk::to_string( dstAccessToExternalFlags ) << std::endl
				          << std::endl;
			}

			vk::SubpassDependency externalToSubpassDependency;
			externalToSubpassDependency
			    .setSrcSubpass( VK_SUBPASS_EXTERNAL ) // outside of renderpass
			    .setDstSubpass( 0 )                   // first subpass
			    .setSrcStageMask( srcStageFromExternalFlags )
			    .setDstStageMask( dstStageFromExternalFlags )
			    .setSrcAccessMask( srcAccessFromExternalFlags )
			    .setDstAccessMask( dstAccessFromExternalFlags )
			    .setDependencyFlags( vk::DependencyFlagBits::eByRegion );
			vk::SubpassDependency subpassToExternalDependency;
			subpassToExternalDependency
			    .setSrcSubpass( 0 )                   // last subpass
			    .setDstSubpass( VK_SUBPASS_EXTERNAL ) // outside of renderpass
			    .setSrcStageMask( srcStageToExternalFlags )
			    .setDstStageMask( dstStageToExternalFlags )
			    .setSrcAccessMask( srcAccessToExternalFlags )
			    .setDstAccessMask( dstAccessToExternalFlags )
			    .setDependencyFlags( vk::DependencyFlagBits::eByRegion );

			dependencies.emplace_back( std::move( externalToSubpassDependency ) );
			dependencies.emplace_back( std::move( subpassToExternalDependency ) );
		}

		{
			// -- Build hash for compatible renderpass
			//
			// We need to include all information that defines renderpass compatibility.
			//
			// We are not clear whether subpasses must be identical between two compatible renderpasses,
			// therefore we don't include subpass information in calculating renderpass compatibility.

			// -- 1. hash attachments
			// -- 2. hash subpass descriptions for each subpass
			//       subpass descriptions are structs with vectors of index references to attachments

			{
				uint64_t rp_hash = 0;

				// -- hash attachments
				for ( const auto &a : attachments ) {
					// We use offsetof so that we can get everything from flags to the start of the
					// attachmentdescription to (but not including) loadOp. We assume that struct is tightly packed.

					static_assert( sizeof( vk::AttachmentDescription::flags ) +
					                       sizeof( vk::AttachmentDescription::format ) +
					                       sizeof( vk::AttachmentDescription::samples ) ==
					                   offsetof( vk::AttachmentDescription, loadOp ),
					               "AttachmentDescription struct must be tightly packed for efficient hashing" );

					rp_hash = SpookyHash::Hash64( &a, offsetof( vk::AttachmentDescription, loadOp ), rp_hash );
				}

				// -- Hash subpasses
				for ( const auto &s : subpasses ) {

					// note: attachment references are not that straightforward to hash either, as they contain a layout
					// field, which we want to ignore, since it makes no difference for render pass compatibility.

					rp_hash = SpookyHash::Hash64( &s.flags, sizeof( s.flags ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.pipelineBindPoint, sizeof( s.pipelineBindPoint ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.inputAttachmentCount, sizeof( s.inputAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.colorAttachmentCount, sizeof( s.colorAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.preserveAttachmentCount, sizeof( s.preserveAttachmentCount ), rp_hash );

					auto calc_hash_for_attachment_references = []( vk::AttachmentReference const *pAttachmentRefs, unsigned int count, uint64_t seed ) -> uint64_t {
						// We define this as a pure function lambda, and hope for it to be inlined
						if ( pAttachmentRefs == nullptr ) {
							return seed;
						}
						// ----------| invariant: pAttachmentRefs is valid
						for ( auto const *pAr = pAttachmentRefs; pAr != pAttachmentRefs + count; pAr++ ) {
							seed = SpookyHash::Hash64( pAr, sizeof( vk::AttachmentReference::attachment ), seed );
						}
						return seed;
					};

					// -- for each element in attachment reference, add attachment reference index to the hash
					//
					rp_hash = calc_hash_for_attachment_references( s.pColorAttachments, s.colorAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pResolveAttachments, s.colorAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pInputAttachments, s.inputAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pDepthStencilAttachment, 1, rp_hash );

					// -- preserve attachments are special, because they are not stored as attachment references, but as plain indices
					if ( s.pPreserveAttachments ) {
						rp_hash = SpookyHash::Hash64( s.pPreserveAttachments, s.preserveAttachmentCount * sizeof( *s.pPreserveAttachments ), rp_hash );
					}
				}

				// Store *hash for compatible renderpass* with pass so that pipelines can test whether they are compatible.
				//
				// "Compatible renderpass" means the hash is not fully representative of the renderpass,
				// but two renderpasses with same hash should be compatible, as everything that touches
				// renderpass compatibility has been factored into calculating the hash.
				//
				pass.renderpassHash = rp_hash;
			}

			vk::RenderPassCreateInfo renderpassCreateInfo;
			renderpassCreateInfo
			    .setAttachmentCount( uint32_t( attachments.size() ) )
			    .setPAttachments( attachments.data() )
			    .setSubpassCount( uint32_t( subpasses.size() ) )
			    .setPSubpasses( subpasses.data() )
			    .setDependencyCount( uint32_t( dependencies.size() ) )
			    .setPDependencies( dependencies.data() );

			// Create vulkan renderpass object
			pass.renderPass = device.createRenderPass( renderpassCreateInfo );

			AbstractPhysicalResource rp;
			rp.type         = AbstractPhysicalResource::eRenderPass;
			rp.asRenderPass = pass.renderPass;

			// Add vulkan renderpass object to list of owned and life-time tracked resources, so that
			// it can be recycled when not needed anymore.
			frame.ownedResources.emplace_front( std::move( rp ) );
		}
	}
}
// ----------------------------------------------------------------------

// create a list of all unique resources referenced by the rendergraph
// and store it with the current backend frame.
static void frame_create_resource_table( BackendFrameData &frame, le_renderpass_o **passes, size_t numRenderPasses ) {

	using namespace le_renderer;

	frame.syncChainTable.clear();

	for ( auto *pPass = passes; pPass != passes + numRenderPasses; pPass++ ) {

		le_resource_handle_t const *pResources     = nullptr;
		le_resource_info_t const *  pResourceInfos = nullptr;
		size_t                      numResources   = 0;

		renderpass_i.get_used_resources( *pPass, &pResources, &pResourceInfos, &numResources );

		// CHECK: make sure not to append to resources which already exist.
		for ( auto it = pResources; it != pResources + numResources; ++it ) {
			frame.syncChainTable.insert( {*it, {BackendFrameData::ResourceState{}}} );
		}
	}
}

// ----------------------------------------------------------------------

/// \brief fetch vk::Buffer from encoder index if transient, otherwise, fetch from frame available resources.
static inline vk::Buffer frame_data_get_buffer_from_le_resource_id( const BackendFrameData &frame, const le_resource_handle_t &resource ) {

	assert( resource.meta.type == LeResourceType::eBuffer ); // resource type must be buffer

	if ( resource.meta.flags & le_resource_handle_t::FlagBits::eIsVirtual ) {
		return frame.allocatorBuffers[ resource.meta.index ];
	} else {
		return frame.availableResources.at( resource ).asBuffer;
	}
}

// ----------------------------------------------------------------------
static inline vk::Image frame_data_get_image_from_le_resource_id( const BackendFrameData &frame, const le_resource_handle_t &resource ) {

	assert( resource.meta.type == LeResourceType::eImage ); // resource type must be image

	return frame.availableResources.at( resource ).asImage;
}

// ----------------------------------------------------------------------
static inline VkFormat frame_data_get_image_format_from_resource_id( BackendFrameData const &frame, const le_resource_handle_t &resource ) {

	assert( resource.meta.type == LeResourceType::eImage ); // resource type must be image

	return frame.availableResources.at( resource ).info.imageInfo.format;
}

// ----------------------------------------------------------------------
// if specific format for texture was not specified, return format of referenced image
static inline VkFormat frame_data_get_image_format_from_texture_info( BackendFrameData const &frame, LeTextureInfo const &texInfo ) {
	if ( texInfo.imageView.format == 0 ) {
		return ( frame_data_get_image_format_from_resource_id( frame, texInfo.imageView.imageId ) );
	} else {
		return VkFormat( texInfo.imageView.format );
	}
}

// ----------------------------------------------------------------------
// input: Pass
// output: framebuffer, append newly created imageViews to retained resources list.
static void backend_create_frame_buffers( BackendFrameData &frame, vk::Device &device ) {

	for ( auto &pass : frame.passes ) {

		if ( pass.type != LE_RENDER_PASS_TYPE_DRAW ) {
			continue;
		}
		std::vector<vk::ImageView> framebufferAttachments;
		framebufferAttachments.reserve( pass.numColorAttachments + pass.numDepthStencilAttachments );

		for ( AttachmentInfo const *attachment = pass.attachments; attachment != pass.attachments + ( pass.numColorAttachments + pass.numDepthStencilAttachments ); attachment++ ) {

			bool isDepthStencilFormat = is_depth_stencil_format( attachment->format );

			::vk::ImageSubresourceRange subresourceRange;
			subresourceRange
			    .setAspectMask( isDepthStencilFormat ? ( vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil ) : vk::ImageAspectFlagBits::eColor )
			    .setBaseMipLevel( 0 )
			    .setLevelCount( 1 )
			    .setBaseArrayLayer( 0 )
			    .setLayerCount( 1 );

			::vk::ImageViewCreateInfo imageViewCreateInfo;
			imageViewCreateInfo
			    .setImage( frame_data_get_image_from_le_resource_id( frame, attachment->resource_id ) )
			    .setViewType( vk::ImageViewType::e2D )
			    .setFormat( attachment->format )
			    .setComponents( {} ) // default-constructor '{}' means identity
			    .setSubresourceRange( subresourceRange );

			auto imageView = device.createImageView( imageViewCreateInfo );

			framebufferAttachments.push_back( imageView );

			{
				// Retain imageviews in owned resources - they will be released
				// once not needed anymore.

				AbstractPhysicalResource iv;
				iv.type        = AbstractPhysicalResource::eImageView;
				iv.asImageView = imageView;

				frame.ownedResources.emplace_front( std::move( iv ) );
			}
		}

		vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo
		    .setFlags( {} )
		    .setRenderPass( pass.renderPass )
		    .setAttachmentCount( uint32_t( framebufferAttachments.size() ) )
		    .setPAttachments( framebufferAttachments.data() )
		    .setWidth( pass.width )
		    .setHeight( pass.height )
		    .setLayers( 1 );

		pass.framebuffer = device.createFramebuffer( framebufferCreateInfo );

		{
			// Retain framebuffer

			AbstractPhysicalResource fb;
			fb.type          = AbstractPhysicalResource::eFramebuffer;
			fb.asFramebuffer = pass.framebuffer;

			frame.ownedResources.emplace_front( std::move( fb ) );
		}
	}
}

static void backend_create_descriptor_pools( BackendFrameData &frame, vk::Device &device, size_t numRenderPasses ) {

	// Make sure that there is one descriptorpool for every renderpass.
	// descriptor pools which were created previously will be re-used,
	// if we're suddenly rendering more frames, we will add additional
	// descriptorpools.

	// at this point it would be nice to have an idea for each renderpass
	// on how many descriptors to expect, but we cannot know that realistically
	// without going through the command buffer... not ideal.

	// this is why we're creating space for a generous amount of descriptors
	// hoping we're not running out when assembling the command buffer.

	for ( ; frame.descriptorPools.size() < numRenderPasses; ) {

		vk::DescriptorPoolCreateInfo info;

		std::vector<::vk::DescriptorPoolSize> descriptorPoolSizes;

		descriptorPoolSizes.reserve( VK_DESCRIPTOR_TYPE_RANGE_SIZE );

		for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ) {
			descriptorPoolSizes.emplace_back( ::vk::DescriptorType( i ), 1000 ); // 1000 descriptors of each type
		}

		::vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
		descriptorPoolCreateInfo
		    .setMaxSets( 1000 )
		    .setPoolSizeCount( uint32_t( descriptorPoolSizes.size() ) )
		    .setPPoolSizes( descriptorPoolSizes.data() );

		vk::DescriptorPool descriptorPool = device.createDescriptorPool( descriptorPoolCreateInfo );

		frame.descriptorPools.emplace_back( std::move( descriptorPool ) );
	}
}

// ----------------------------------------------------------------------

static void backend_allocate_resources( le_backend_o *self, BackendFrameData &frame, le_renderpass_o **passes, size_t numRenderPasses ) {

	/*

	- Frame is only ever allowed to reference frame-local resources .
	- "Acquire" therefore means we create local copies of backend-wide resources.

	*/

	// -- Make a list of all images to create
	// -- Make a list of all buffers to create

	// make a list of all resources to create
	// - if an image appears more than once, we OR the image's flags.
	// - if a  buffer appears more than once, we OR all the buffer's flags.

	// Locally, in the frame, we store these in a vector,
	// and we could reference them by their offsets, because this method must
	// be uncontested.

	{
		// -- first it is our holy duty to drop any binned resources which were condemned the last time this frame was active.
		// It's possible that this is more than two screen refreshes ago, depending on how many swapchain images there are.
		vk::Device device = self->device->getVkDevice();

		for ( auto &a : frame.binnedResources ) {

			if ( a.second.info.isBuffer() ) {
				device.destroyBuffer( a.second.asBuffer );
			} else {
				device.destroyImage( a.second.asImage );
			}

			vmaFreeMemory( self->mAllocator, a.second.allocation );
		}
		frame.binnedResources.clear();
	}

	using namespace le_renderer;

	std::unordered_map<le_resource_handle_t, ResourceInfo, LeResourceHandleIdentity> declaredResources;

	// -- iterate over all passes
	for ( le_renderpass_o **rp = passes; rp != passes + numRenderPasses; rp++ ) {

		le_resource_handle_t const *pCreateResourceIds = nullptr;
		le_resource_info_t const *  pResourceInfos     = nullptr;
		size_t                      numCreateResources = 0;

		// -- iterate over all resource declarations in this pass
		renderpass_i.get_used_resources( *rp, &pCreateResourceIds, &pResourceInfos, &numCreateResources );

		auto pass_width  = renderpass_i.get_width( *rp );
		auto pass_height = renderpass_i.get_height( *rp );

		if ( pass_width == 0 ) {
			// if zero was chosen this means to use the default extents values for a
			// renderpass, which is to use the frame's current swapchain extents.
			pass_width = frame.swapchainWidth;
			renderpass_i.set_width( *rp, pass_width );
		}

		if ( pass_height == 0 ) {
			// if zero was chosen this means to use the default extents values for a
			// renderpass, which is to use the frame's current swapchain extents.
			pass_height = frame.swapchainHeight;
			renderpass_i.set_height( *rp, pass_height );
		}

		for ( size_t i = 0; i != numCreateResources; ++i ) {

			le_resource_info_t const &  createInfo = pResourceInfos[ i ];     // Resource descriptor (from renderpass)
			le_resource_handle_t const &resourceId = pCreateResourceIds[ i ]; // Hash of resource name

			ResourceInfo resourceCreateInfo{};

			switch ( createInfo.type ) {
			case LeResourceType::eBuffer: {
				resourceCreateInfo.bufferInfo                       = vk::BufferCreateInfo{};
				resourceCreateInfo.bufferInfo.size                  = createInfo.buffer.size;
				resourceCreateInfo.bufferInfo.usage                 = createInfo.buffer.usage;
				resourceCreateInfo.bufferInfo.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
				resourceCreateInfo.bufferInfo.queueFamilyIndexCount = 1;
				resourceCreateInfo.bufferInfo.pQueueFamilyIndices   = &self->queueFamilyIndexGraphics;

			} break;
			case LeResourceType::eImage: {

				// For width and height, we place the current swapchain width and height if
				// given as initially 0.
				// This allows us to express that we want to allocate images by default at
				// the same size as the swapchain, which is especially useful with images
				// which are created as rendertargets.

				auto const &ci         = createInfo.image;                                     // src info data
				auto &      imgInfoRef = resourceCreateInfo.imageInfo = vk::ImageCreateInfo{}; // dst info data

				imgInfoRef.flags                 = ci.flags;
				imgInfoRef.imageType             = VkImageType( ci.imageType );
				imgInfoRef.format                = VkFormat( ci.format );
				imgInfoRef.extent.width          = ci.extent.width != 0 ? ci.extent.width : pass_width;
				imgInfoRef.extent.height         = ci.extent.height != 0 ? ci.extent.height : pass_height;
				imgInfoRef.extent.depth          = ci.extent.depth != 0 ? ci.extent.depth : 1;
				imgInfoRef.mipLevels             = ci.mipLevels;
				imgInfoRef.arrayLayers           = ci.arrayLayers != 0 ? ci.arrayLayers : 1;
				imgInfoRef.samples               = VkSampleCountFlagBits( ci.samples );
				imgInfoRef.tiling                = VkImageTiling( ci.tiling );
				imgInfoRef.usage                 = ci.usage;
				imgInfoRef.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
				imgInfoRef.queueFamilyIndexCount = 1;
				imgInfoRef.pQueueFamilyIndices   = &self->queueFamilyIndexGraphics;
				imgInfoRef.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED; // must be either preinitialized or undefined

			} break;
			case LeResourceType::eUndefined:
				assert( false ); // Resource Type must be defined at this point.
			    break;
			}

			// -- Add createInfo to set of declared resources

			auto it = declaredResources.insert( {resourceId, resourceCreateInfo} );

			if ( it.second == false ) {
				auto &storedResource = it.first->second;
				if ( storedResource == resourceCreateInfo ) {
					continue; // resource is identical
				} else {

					ResourceInfo::ConsolidationResult consolidationResult{};
					storedResource = ResourceInfo::consolidate( storedResource, resourceCreateInfo, &consolidationResult );

					assert( consolidationResult == ResourceInfo::ConsolidationResult::eOk ); // resource was re-declared, we must consolidate the resource before it can be allocated.
				}
			}

		} // end for all create resources

	} // end for all passes

	auto &backendResources = self->only_backend_allocate_resources_may_access.allocatedResources;

	// -- now check if all resources declared in this frame are already available in backend.

	auto allocateResource = []( const VmaAllocator &alloc, const ResourceInfo &resourceInfo ) -> AllocatedResource {
		AllocatedResource       res{};
		VmaAllocationCreateInfo allocationCreateInfo{};
		allocationCreateInfo.flags          = {}; // default flags
		allocationCreateInfo.usage          = VMA_MEMORY_USAGE_GPU_ONLY;
		allocationCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		if ( resourceInfo.isBuffer() ) {
			vmaCreateBuffer( alloc,
			                 &resourceInfo.bufferInfo,
			                 &allocationCreateInfo,
			                 &res.asBuffer,
			                 &res.allocation,
			                 &res.allocationInfo );
		} else {
			vmaCreateImage( alloc,
			                &resourceInfo.imageInfo,
			                &allocationCreateInfo,
			                &res.asImage,
			                &res.allocation,
			                &res.allocationInfo );
		}
		res.info = resourceInfo;
		return res;
	};

	for ( auto &r : declaredResources ) {

		le_resource_handle_t const &resourceId           = r.first;  // hash of resource name
		ResourceInfo const &        declaredResourceInfo = r.second; // current resourceInfo

		auto foundIt = backendResources.find( resourceId ); // find an allocated resource with same name as declared resource

		if ( foundIt == backendResources.end() ) {
			// -- not found, we must allocate this resource and add it to the backend.
			// then add a reference to it to the current frame.

			auto res = allocateResource( self->mAllocator, declaredResourceInfo );

			// -- add resource to list of available resources for this frame
			frame.availableResources.emplace( resourceId, res );

			// -- add resource to backend
			backendResources.insert_or_assign( resourceId, res );

		} else {
			// check if resource descriptor matches.

			auto &resourceInfo = foundIt->second.info;

			if ( resourceInfo == declaredResourceInfo ) {
				// -- descriptor matches.
				// add a copy of this resource allocation to the current frame.

				frame.availableResources.emplace( resourceId, foundIt->second );

			} else {
				// -- descriptor does not match. We must re-allocate this resource, and add the old version of the resource to the recycling bin.

				// -- allocate a new resource

				auto res = allocateResource( self->mAllocator, declaredResourceInfo );

				// Add a copy of old resource to recycling bin for this frame, so that
				// these resources get freed when this frame comes round again.
				//
				// We don't immediately delete the resources, as in-flight frames
				// might still be using them.
				frame.binnedResources.try_emplace( resourceId, foundIt->second );

				//
				frame.availableResources.emplace( resourceId, res );

				// remove old version of resource from backend, and
				// add new version of resource to backend
				backendResources.insert_or_assign( resourceId, res );
			}
		}
	}
}

static void frame_allocate_per_pass_resources( BackendFrameData &frame, vk::Device const &device, le_renderpass_o **passes, size_t numRenderPasses ) {

	static auto const &renderpass_i = Registry::getApi<le_renderer_api>()->le_renderpass_i;

	for ( auto p = passes; p != passes + numRenderPasses; p++ ) {
		// get all texture names for this pass

		const le_resource_handle_t *textureIds     = nullptr;
		size_t                      textureIdCount = 0;
		renderpass_i.get_texture_ids( *p, &textureIds, &textureIdCount );

		const LeTextureInfo *textureInfos     = nullptr;
		size_t               textureInfoCount = 0;
		renderpass_i.get_texture_infos( *p, &textureInfos, &textureInfoCount );

		assert( textureIdCount == textureInfoCount ); // texture info and -id count must be identical, as there is a 1:1 relationship

		for ( size_t i = 0; i != textureIdCount; i++ ) {

			// -- find out if texture with this name has already been alloacted.
			// -- if not, allocate

			const le_resource_handle_t textureId = textureIds[ i ];

			if ( frame.textures.find( textureId ) == frame.textures.end() ) {
				// -- we need to allocate a new texture

				auto &texInfo = textureInfos[ i ];

				vk::ImageSubresourceRange subresourceRange;
				subresourceRange
				    .setAspectMask( vk::ImageAspectFlagBits::eColor )
				    .setBaseMipLevel( 0 )
				    .setLevelCount( 1 )
				    .setBaseArrayLayer( 0 )
				    .setLayerCount( 1 );

				// TODO: fill in additional image view create info based on info from pass...

				vk::ImageViewCreateInfo imageViewCreateInfo{};
				imageViewCreateInfo
				    .setFlags( {} )
				    .setImage( frame_data_get_image_from_le_resource_id( frame, texInfo.imageView.imageId ) )
				    .setViewType( vk::ImageViewType::e2D )
				    .setFormat( vk::Format( frame_data_get_image_format_from_texture_info( frame, texInfo ) ) )
				    .setComponents( {} ) // default component mapping
				    .setSubresourceRange( subresourceRange );

				// TODO: fill in additional sampler create info based on info from pass...
				vk::SamplerCreateInfo samplerCreateInfo{};
				samplerCreateInfo
				    .setFlags( {} )
				    .setMagFilter( vk::Filter( texInfo.sampler.magFilter ) )
				    .setMinFilter( vk::Filter( texInfo.sampler.minFilter ) )
				    .setMipmapMode( ::vk::SamplerMipmapMode::eLinear )
				    .setAddressModeU( ::vk::SamplerAddressMode::eClampToBorder )
				    .setAddressModeV( ::vk::SamplerAddressMode::eClampToBorder )
				    .setAddressModeW( ::vk::SamplerAddressMode::eRepeat )
				    .setMipLodBias( 0.f )
				    .setAnisotropyEnable( VK_FALSE )
				    .setMaxAnisotropy( 0.f )
				    .setCompareEnable( VK_FALSE )
				    .setCompareOp( ::vk::CompareOp::eLess )
				    .setMinLod( 0.f )
				    .setMaxLod( 1.f )
				    .setBorderColor( ::vk::BorderColor::eFloatTransparentBlack )
				    .setUnnormalizedCoordinates( VK_FALSE );

				auto vkSampler   = device.createSampler( samplerCreateInfo );
				auto vkImageView = device.createImageView( imageViewCreateInfo );

				// -- Store Texture with frame so that decoder can find references

				BackendFrameData::Texture tex;
				tex.imageView = vkImageView;
				tex.sampler   = vkSampler;

				frame.textures[ textureId ] = tex;

				{
					// Now store vk objects with frame owned resources, so that
					// they can be destroyed when frame crosses the fence.

					AbstractPhysicalResource sampler;
					AbstractPhysicalResource imgView;

					sampler.asSampler   = vkSampler;
					sampler.type        = AbstractPhysicalResource::Type::eSampler;
					imgView.asImageView = vkImageView;
					imgView.type        = AbstractPhysicalResource::Type::eImageView;

					frame.ownedResources.emplace_front( std::move( sampler ) );
					frame.ownedResources.emplace_front( std::move( imgView ) );
				}
			}
		}
	}

	// we need to store texture (= sampler+imageview) in a map of textures, indexed by their id.
	// we only need to create a new texture if it has a new name, otherwise we will use the same texture.
	// this could lead to some stange effects, but texture names should be universal over the frame.

	// store any resources in ownedResources so that they get destroyed when frame fence is passed
}

// ----------------------------------------------------------------------
// TODO: this should mark acquired resources as used by this frame -
// so that they can only be destroyed iff this frame has been reset.
static bool backend_acquire_physical_resources( le_backend_o *self, size_t frameIndex, le_renderpass_o **passes, size_t numRenderPasses ) {
	auto &frame = self->mFrames[ frameIndex ];

	if ( !self->swapchain->acquireNextImage( frame.semaphorePresentComplete, frame.swapchainImageIndex ) ) {
		return false;
	}

	// ----------| invariant: swapchain acquisition successful.

	frame.swapchainWidth  = self->swapchain->getImageWidth();
	frame.swapchainHeight = self->swapchain->getImageHeight();

	frame.availableResources[ self->backBufferImageHandle ].asImage = self->swapchain->getImage( frame.swapchainImageIndex );
	{
		auto &backbufferInfo  = frame.availableResources[ self->backBufferImageHandle ].info.imageInfo;
		backbufferInfo        = vk::ImageCreateInfo{};
		backbufferInfo.extent = vk::Extent3D( frame.swapchainWidth, frame.swapchainHeight, 1 );
		backbufferInfo.format = VkFormat( self->swapchainImageFormat );
	}

	// Note that at this point memory for scratch buffers for each pass
	// in this frame has already been allocated,
	// as this happens shortly before executeGraph.

	backend_allocate_resources( self, frame, passes, numRenderPasses );

	frame_create_resource_table( frame, passes, numRenderPasses );
	frame_track_resource_state( frame, passes, numRenderPasses, self->swapchainImageFormat, self->backBufferImageHandle );

	vk::Device device = self->device->getVkDevice();
	// -- allocate any transient vk objects such as image samplers, and image views
	frame_allocate_per_pass_resources( frame, device, passes, numRenderPasses );

	backend_create_renderpasses( frame, device );

	// -- make sure that there is a descriptorpool for every renderpass
	backend_create_descriptor_pools( frame, device, numRenderPasses );

	// patch and retain physical resources in bulk here, so that
	// each pass may be processed independently

	backend_create_frame_buffers( frame, device );

	return true;
};

// ----------------------------------------------------------------------
// We return a list of transient allocators which exist for the frame.
// as these allocators are not deleted, but reset every frame, we only create new allocations
// if we don't have enough to cover the demand for this frame. Otherwise we re-use existing
// allocators and allocations.
static le_allocator_o **backend_get_transient_allocators( le_backend_o *self, size_t frameIndex, size_t numAllocators ) {

	using namespace le_backend_vk;

	auto &frame = self->mFrames[ frameIndex ];

	// IMPROVEMENT: if we're dealing with a transfer pass, we might want to use an allocator
	// which allows for variable size - this will allow us to upload large images in one go
	// for example.

	// Only add another buffer to frame-allocated buffers if we don't yet have
	// enough buffers to cover each pass (numAllocators should correspond to
	// number of passes.)
	//
	// NOTE: We compare by '<', since numAllocators may be smaller if number
	// of renderpasses was reduced for some reason.
	for ( size_t i = frame.allocators.size(); i < numAllocators; ++i ) {

		assert( numAllocators < 256 ); // must not have more than 255 allocators, otherwise we cannot store index in LeResourceHandleMeta.

		VkBuffer          buffer = nullptr;
		VmaAllocation     allocation;
		VmaAllocationInfo allocationInfo;

		VmaAllocationCreateInfo createInfo{};
		{
			createInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			createInfo.pool  = frame.allocationPool; // Since we're allocating from a pool all fields but .flags will be taken from the pool

			le_resource_handle_t res = declare_resource_virtual_buffer( uint8_t( i ) );

			memcpy( &createInfo.pUserData, &res, sizeof( void * ) ); // store value of i in userData
		}

		VkBufferCreateInfo bufferCreateInfo;
		{
			// we use the cpp proxy because it's more ergonomic to fill the values.
			vk::BufferCreateInfo bufferInfoProxy;
			bufferInfoProxy
			    .setFlags( {} )
			    .setSize( 1u << 24 ) // 16.77MB
			    .setUsage( self->LE_BUFFER_USAGE_FLAGS_SCRATCH )
			    .setSharingMode( vk::SharingMode::eExclusive )
			    .setQueueFamilyIndexCount( 1 )
			    .setPQueueFamilyIndices( &self->queueFamilyIndexGraphics ); // TODO: use compute queue for compute passes, or transfer for transfer passes
			bufferCreateInfo = bufferInfoProxy;
		}

		auto result = vmaCreateBuffer( self->mAllocator, &bufferCreateInfo, &createInfo, &buffer, &allocation, &allocationInfo );

		assert( result == VK_SUCCESS ); // todo: deal with failed allocation

		// Create a new allocator - note that we assume an alignment of 256 bytes
		le_allocator_o *allocator = le_allocator_linear_i.create( &allocationInfo, 256 );

		frame.allocators.emplace_back( allocator );
		frame.allocatorBuffers.emplace_back( std::move( buffer ) );
		frame.allocations.emplace_back( std::move( allocation ) );
		frame.allocationInfos.emplace_back( std::move( allocationInfo ) );
	}

	return frame.allocators.data();
}

// ----------------------------------------------------------------------
// Decode commandStream for each pass (may happen in parallel)
// translate into vk specific commands.
static void backend_process_frame( le_backend_o *self, size_t frameIndex ) {

	using namespace le_renderer;   // for encoder
	using namespace le_backend_vk; // for device

	auto &frame = self->mFrames[ frameIndex ];

	vk::Device device = self->device->getVkDevice();

	static_assert( sizeof( vk::Viewport ) == sizeof( le::Viewport ), "Viewport data size must be same in vk and le" );
	static_assert( sizeof( vk::Rect2D ) == sizeof( le::Rect2D ), "Rect2D data size must be same in vk and le" );

	static auto maxVertexInputBindings = vk_device_i.get_vk_physical_device_properties( *self->device ).limits.maxVertexInputBindings;

	// TODO: (parallelize) when going wide, there needs to be a commandPool for each execution context so that
	// command buffer generation may be free-threaded.
	auto numCommandBuffers = uint32_t( frame.passes.size() );
	auto cmdBufs           = device.allocateCommandBuffers( {frame.commandPool, vk::CommandBufferLevel::ePrimary, numCommandBuffers} );

	std::array<vk::ClearValue, 16> clearValues{};

	// TODO: (parallel for)
	// note that access to any caches when creating pipelines and layouts and descriptorsets must be
	// mutex-controlled when processing happens concurrently.
	for ( size_t passIndex = 0; passIndex != frame.passes.size(); ++passIndex ) {

		auto &pass           = frame.passes[ passIndex ];
		auto &cmd            = cmdBufs[ passIndex ];
		auto &descriptorPool = frame.descriptorPools[ passIndex ];

		// create frame buffer, based on swapchain and renderpass

		cmd.begin( {::vk::CommandBufferUsageFlagBits::eOneTimeSubmit} );

		for ( size_t i = 0; i != ( pass.numColorAttachments + pass.numDepthStencilAttachments ); ++i ) {
			clearValues[ i ] = pass.attachments[ i ].clearValue;
		}

		// non-draw passes don't need renderpasses.
		if ( pass.type == LE_RENDER_PASS_TYPE_DRAW && pass.renderPass ) {

			vk::RenderPassBeginInfo renderPassBeginInfo;
			renderPassBeginInfo
			    .setRenderPass( pass.renderPass )
			    .setFramebuffer( pass.framebuffer )
			    .setRenderArea( vk::Rect2D( {0, 0}, {pass.width, pass.height} ) )
			    .setClearValueCount( pass.numColorAttachments + pass.numDepthStencilAttachments )
			    .setPClearValues( clearValues.data() );

			cmd.beginRenderPass( renderPassBeginInfo, vk::SubpassContents::eInline );
		}

		// -- Translate intermediary command stream data to api-native instructions

		void *   commandStream = nullptr;
		size_t   dataSize      = 0;
		size_t   numCommands   = 0;
		size_t   commandIndex  = 0;
		uint32_t subpassIndex  = 0;

		struct ArgumentState {
			uint32_t                                    dynamicOffsetCount = 0;  // count of dynamic elements in current pipeline
			std::array<uint32_t, 256>                   dynamicOffsets     = {}; // offset for each dynamic element in current pipeline
			uint32_t                                    setCount           = 0;  // current count of bound descriptorSets (max: 8)
			std::array<std::vector<DescriptorData>, 8>  setData;                 // data per-set
			std::array<vk::DescriptorUpdateTemplate, 8> updateTemplates;         // update templates for currently bound descriptor sets
			std::array<vk::DescriptorSetLayout, 8>      layouts;                 // layouts for currently bound descriptor sets
			std::vector<le_shader_binding_info>         binding_infos;
		} argumentState;

		vk::PipelineLayout currentPipelineLayout;
		vk::DescriptorSet  descriptorSets[ VK_MAX_BOUND_DESCRIPTOR_SETS ] = {}; // currently bound descriptorSets (allocated from pool, therefore we must not worry about freeing, and may re-use freely)

		auto updateArguments = []( const vk::Device &device, const vk::DescriptorPool &descriptorPool_, const ArgumentState &argumentState_, vk::DescriptorSet *descriptorSets ) -> bool {
			// -- allocate descriptors from descriptorpool based on set layout info

			if ( argumentState_.setCount == 0 ) {
				return true;
			}

			// ----------| invariant: there are descriptorSets to allocate

			vk::DescriptorSetAllocateInfo allocateInfo;
			allocateInfo.setDescriptorPool( descriptorPool_ )
			    .setDescriptorSetCount( uint32_t( argumentState_.setCount ) )
			    .setPSetLayouts( argumentState_.layouts.data() );

			// -- allocate some descriptorSets based on current layout
			device.allocateDescriptorSets( &allocateInfo, descriptorSets );

			bool argumentsOk = true;

			// -- write data from descriptorSetData into freshly allocated DescriptorSets
			for ( size_t setId = 0; setId != argumentState_.setCount; ++setId ) {

				// If argumentState contains invalid information (for example if an uniform has not been set yet)
				// this will lead to SEGFAULT. You must ensure that argumentState contains valid information.
				//
				// The most common case for this bug is not providing any data for a uniform used in the shader,
				// we check for this and skip any argumentStates which have invalid data...

				for ( auto &a : argumentState_.setData[ setId ] ) {

					switch ( a.type ) {
					case vk::DescriptorType::eStorageBufferDynamic: //
					case vk::DescriptorType::eUniformBuffer:        //
					case vk::DescriptorType::eUniformBufferDynamic: //
					case vk::DescriptorType::eStorageBuffer:        // fall-through
						// if buffer must have valid buffer bound
						argumentsOk &= ( nullptr != a.buffer );
					    break;
					case vk::DescriptorType::eCombinedImageSampler:
					case vk::DescriptorType::eSampledImage:
						argumentsOk &= ( nullptr != a.imageView ); // if sampler, must have image view
					    break;
					default:
						// TODO: check arguments for other types of descriptors
						argumentsOk &= true;
					    break;
					}

					if ( false == argumentsOk ) {
						// TODO: notify that an argument is not OKAY
						break;
					}
				}

				if ( argumentsOk ) {
					device.updateDescriptorSetWithTemplate( descriptorSets[ setId ], argumentState_.updateTemplates[ setId ], argumentState_.setData[ setId ].data() );
				} else {
					return false;
				}
			}

			return argumentsOk;
		};

		if ( pass.encoder ) {
			encoder_i.get_encoded_data( pass.encoder, &commandStream, &dataSize, &numCommands );
		} else {
			assert( false );
			std::cout << "ERROR: pass does not have valid encoder.";
		}

		le_pipeline_manager_o *pipelineManager = encoder_i.get_pipeline_manager( pass.encoder );

		if ( commandStream != nullptr && numCommands > 0 ) {

			std::vector<vk::Buffer>       vertexInputBindings( maxVertexInputBindings, nullptr );
			void *                        dataIt = commandStream;
			le_pipeline_and_layout_info_t currentPipeline;

			while ( commandIndex != numCommands ) {

				auto header = static_cast<le::CommandHeader *>( dataIt );

				switch ( header->info.type ) {

				case le::CommandType::eBindPipeline: {
					auto *le_cmd = static_cast<le::CommandBindPipeline *>( dataIt );
					if ( pass.type == LE_RENDER_PASS_TYPE_DRAW ) {
						// at this point, a valid renderpass must be bound

						using namespace le_backend_vk;
						// -- potentially compile and create pipeline here, based on current pass and subpass
						currentPipeline = le_pipeline_manager_i.produce_pipeline( pipelineManager, le_cmd->info.psoHash, pass, subpassIndex );

						// -- grab current pipeline layout from cache
						currentPipelineLayout = le_pipeline_manager_i.get_pipeline_layout( pipelineManager, currentPipeline.layout_info.pipeline_layout_key );

						{
							// -- update pipelineData - that's the data values for all descriptors which are currently bound

							argumentState.setCount = uint32_t( currentPipeline.layout_info.set_layout_count );
							argumentState.binding_infos.clear();

							// -- reset dynamic offset count
							argumentState.dynamicOffsetCount = 0;

							// let's create descriptorData vector based on current bindings-
							for ( size_t setId = 0; setId != argumentState.setCount; ++setId ) {

								// look up set layout info via set layout key
								auto const &set_layout_key = currentPipeline.layout_info.set_layout_keys[ setId ];

								auto const &setLayoutInfo = le_pipeline_manager_i.get_descriptor_set_layout( pipelineManager, set_layout_key );

								auto &setData = argumentState.setData[ setId ];

								argumentState.layouts[ setId ]         = setLayoutInfo.vk_descriptor_set_layout;
								argumentState.updateTemplates[ setId ] = setLayoutInfo.vk_descriptor_update_template;

								setData.clear();
								setData.reserve( setLayoutInfo.binding_info.size() );

								for ( auto b : setLayoutInfo.binding_info ) {

									// add an entry for each array element with this binding to setData
									for ( size_t arrayIndex = 0; arrayIndex != b.count; arrayIndex++ ) {
										DescriptorData descriptorData{};
										descriptorData.arrayIndex    = uint32_t( arrayIndex );
										descriptorData.bindingNumber = b.binding;
										descriptorData.type          = vk::DescriptorType( b.type );
										descriptorData.range         = VK_WHOLE_SIZE; // note this could be vk_full_size
										setData.emplace_back( std::move( descriptorData ) );
									}

									if ( b.type == enumToNum( vk::DescriptorType::eStorageBufferDynamic ) ||
									     b.type == enumToNum( vk::DescriptorType::eUniformBufferDynamic ) ) {
										assert( b.count != 0 ); // count cannot be 0

										// store dynamic offset index for this element
										b.dynamic_offset_idx = argumentState.dynamicOffsetCount;

										// increase dynamic offset count by number of elements in this binding
										argumentState.dynamicOffsetCount += b.count;
									}

									// add this binding to list of current bindings
									argumentState.binding_infos.emplace_back( std::move( b ) );
								}
							}

							// -- reset dynamic offsets
							memset( argumentState.dynamicOffsets.data(), 0, sizeof( uint32_t ) * argumentState.dynamicOffsetCount );

							// we write directly into descriptorsetstate when we update descriptors.
							// when we bind a pipeline, we update the descriptorsetstate based
							// on what the pipeline requires.
						}

						cmd.bindPipeline( vk::PipelineBindPoint::eGraphics, currentPipeline.pipeline );

					} else if ( pass.type == LE_RENDER_PASS_TYPE_COMPUTE ) {
						// -- TODO: implement compute pass pipeline binding
					}
				} break;

				case le::CommandType::eDraw: {
					auto *le_cmd = static_cast<le::CommandDraw *>( dataIt );

					// -- update descriptorsets via template if tainted
					bool argumentsOk = updateArguments( device, descriptorPool, argumentState, descriptorSets );

					if ( false == argumentsOk ) {
						break;
					}

					// --------| invariant: arguments were updated successfully

					if ( argumentState.setCount > 0 ) {

						cmd.bindDescriptorSets( vk::PipelineBindPoint::eGraphics,
						                        currentPipelineLayout,
						                        0,
						                        argumentState.setCount,
						                        descriptorSets,
						                        argumentState.dynamicOffsetCount,
						                        argumentState.dynamicOffsets.data() );
					}

					cmd.draw( le_cmd->info.vertexCount, le_cmd->info.instanceCount, le_cmd->info.firstVertex, le_cmd->info.firstInstance );
				} break;

				case le::CommandType::eDrawIndexed: {
					auto *le_cmd = static_cast<le::CommandDrawIndexed *>( dataIt );

					// -- update descriptorsets via template if tainted
					bool argumentsOk = updateArguments( device, descriptorPool, argumentState, descriptorSets );

					if ( false == argumentsOk ) {
						break;
					}

					// --------| invariant: arguments were updated successfully

					if ( argumentState.setCount > 0 ) {

						cmd.bindDescriptorSets( vk::PipelineBindPoint::eGraphics,
						                        currentPipelineLayout,
						                        0,
						                        argumentState.setCount,
						                        descriptorSets,
						                        argumentState.dynamicOffsetCount,
						                        argumentState.dynamicOffsets.data() );
					}

					cmd.drawIndexed( le_cmd->info.indexCount, le_cmd->info.instanceCount, le_cmd->info.firstIndex, le_cmd->info.vertexOffset, le_cmd->info.firstInstance );
				} break;

				case le::CommandType::eSetLineWidth: {
					auto *le_cmd = static_cast<le::CommandSetLineWidth *>( dataIt );
					cmd.setLineWidth( le_cmd->info.width );
				} break;

				case le::CommandType::eSetViewport: {
					auto *le_cmd = static_cast<le::CommandSetViewport *>( dataIt );
					// Since data for viewports *is stored inline*, we increment the typed pointer
					// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
					cmd.setViewport( le_cmd->info.firstViewport, le_cmd->info.viewportCount, reinterpret_cast<vk::Viewport *>( le_cmd + 1 ) );
				} break;

				case le::CommandType::eSetScissor: {
					auto *le_cmd = static_cast<le::CommandSetScissor *>( dataIt );
					// Since data for scissors *is stored inline*, we increment the typed pointer
					// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
					cmd.setScissor( le_cmd->info.firstScissor, le_cmd->info.scissorCount, reinterpret_cast<vk::Rect2D *>( le_cmd + 1 ) );
				} break;

				case le::CommandType::eSetArgumentUbo: {
					// we need to store the data for the dynamic binding which was set as an argument to the ubo
					// this alters our internal state
					auto *le_cmd = static_cast<le::CommandSetArgumentUbo *>( dataIt );

					uint64_t argument_name_id = le_cmd->info.argument_name_id;

					// find binding info with name referenced in command

					auto b = std::find_if( argumentState.binding_infos.begin(), argumentState.binding_infos.end(), [&argument_name_id]( const le_shader_binding_info &e ) -> bool {
						return e.name_hash == argument_name_id;
					} );

					if ( b == argumentState.binding_infos.end() ) {
						std::cout << __FUNCTION__ << "#L" << std::dec << __LINE__ << " : Warning: Invalid argument name id: 0x" << std::hex << argument_name_id << std::endl
						          << std::flush;
						break;
					}

					// ---------| invariant: we found an argument name that matches
					auto setIndex = b->setIndex;
					auto binding  = b->binding;

					auto &bindingData = argumentState.setData[ setIndex ][ binding ];

					bindingData.buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer_id );
					bindingData.range  = std::min<uint32_t>( le_cmd->info.range, b->range ); // CHECK: use range from binding to limit range...

					// If binding is in fact a dynamic binding, set the corresponding dynamic offset
					// and set the buffer offset to 0.
					if ( b->type == enumToNum( vk::DescriptorType::eStorageBufferDynamic ) ||
					     b->type == enumToNum( vk::DescriptorType::eUniformBufferDynamic ) ) {
						auto dynamicOffset                            = b->dynamic_offset_idx;
						bindingData.offset                            = 0;
						argumentState.dynamicOffsets[ dynamicOffset ] = le_cmd->info.offset;
					} else {
						bindingData.offset = le_cmd->info.offset;
					}

				} break;

				case le::CommandType::eSetArgumentTexture: {
					auto *   le_cmd           = static_cast<le::CommandSetArgumentTexture *>( dataIt );
					uint64_t argument_name_id = le_cmd->info.argument_name_id;

					// Find binding info with name referenced in command
					auto b = std::find_if( argumentState.binding_infos.begin(), argumentState.binding_infos.end(), [&argument_name_id]( const le_shader_binding_info &e ) -> bool {
						return e.name_hash == argument_name_id;
					} );

					if ( b == argumentState.binding_infos.end() ) {
						std::cout << "Warning: Invalid texture argument name id: 0x" << std::hex << argument_name_id << std::endl
						          << std::flush;
						break;
					}

					// ---------| invariant: we found an argument name that matches
					auto setIndex = b->setIndex;
					auto binding  = b->binding;

					auto &bindingData = argumentState.setData[ setIndex ][ binding ];

					// fetch texture information based on texture id from command

					auto foundTex = frame.textures.find( le_cmd->info.texture_id );
					if ( foundTex == frame.textures.end() ) {
						std::cerr << "Could not find requested texture: " << le_cmd->info.texture_id << " Ignoring texture binding command." << std::endl
						          << std::flush;
						break;
					}

					// ----------| invariant: texture has been found

					// TODO: we must be able to programmatically figure out the image layout in advance
					// perhaps through resource tracking.
					bindingData.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

					bindingData.arrayIndex = uint32_t( le_cmd->info.array_index );
					bindingData.sampler    = foundTex->second.sampler;
					bindingData.imageView  = foundTex->second.imageView;
					bindingData.type       = vk::DescriptorType::eCombinedImageSampler;

				} break;

				case le::CommandType::eBindIndexBuffer: {
					auto *le_cmd = static_cast<le::CommandBindIndexBuffer *>( dataIt );
					auto  buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer );
					cmd.bindIndexBuffer( buffer, le_cmd->info.offset, vk::IndexType( le_cmd->info.indexType ) );
				} break;

				case le::CommandType::eBindVertexBuffers: {
					auto *le_cmd = static_cast<le::CommandBindVertexBuffers *>( dataIt );

					uint32_t firstBinding = le_cmd->info.firstBinding;
					uint32_t numBuffers   = le_cmd->info.bindingCount;

					// translate le_buffers to vk_buffers
					for ( uint32_t b = 0; b != numBuffers; ++b ) {
						vertexInputBindings[ b + firstBinding ] = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.pBuffers[ b ] );
					}

					cmd.bindVertexBuffers( le_cmd->info.firstBinding, le_cmd->info.bindingCount, &vertexInputBindings[ firstBinding ], le_cmd->info.pOffsets );
				} break;

				case le::CommandType::eWriteToBuffer: {

					// Enqueue copy buffer command
					// TODO: we must sync this before the next read.
					auto *le_cmd = static_cast<le::CommandWriteToBuffer *>( dataIt );

					vk::BufferCopy region( le_cmd->info.src_offset, le_cmd->info.dst_offset, le_cmd->info.numBytes );

					auto srcBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.src_buffer_id );
					auto dstBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.dst_buffer_id );

					cmd.copyBuffer( srcBuffer, dstBuffer, 1, &region );

					break;
				}

				case le::CommandType::eWriteToImage: {

					// TODO: use sync chain to sync
					auto *le_cmd = static_cast<le::CommandWriteToImage *>( dataIt );

					vk::ImageSubresourceLayers layer;
					layer
					    .setAspectMask( vk::ImageAspectFlagBits::eColor )
					    .setMipLevel( 0 )
					    .setBaseArrayLayer( 0 )
					    .setLayerCount( 1 );

					::vk::ImageSubresourceRange subresourceRange;
					subresourceRange
					    .setAspectMask( ::vk::ImageAspectFlagBits::eColor )
					    .setBaseMipLevel( 0 )
					    .setLevelCount( 1 )
					    .setBaseArrayLayer( 0 )
					    .setLayerCount( 1 );

					vk::Extent3D imageExtent{
						le_cmd->info.dst_region.width,
						le_cmd->info.dst_region.height,
						1,
					};

					vk::BufferImageCopy region;
					region
					    .setBufferOffset( le_cmd->info.src_offset )
					    .setBufferRowLength( imageExtent.width )
					    .setBufferImageHeight( imageExtent.height )
					    .setImageSubresource( layer )
					    .setImageOffset( {0, 0, 0} )
					    .setImageExtent( imageExtent );

					auto srcBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.src_buffer_id );
					auto dstImage  = frame_data_get_image_from_le_resource_id( frame, le_cmd->info.dst_image_id );

					::vk::BufferMemoryBarrier bufferTransferBarrier;
					bufferTransferBarrier
					    .setSrcAccessMask( ::vk::AccessFlagBits::eHostWrite )    // after host write
					    .setDstAccessMask( ::vk::AccessFlagBits::eTransferRead ) // ready buffer for transfer read
					    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
					    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
					    .setBuffer( srcBuffer )
					    .setOffset( le_cmd->info.src_offset )
					    .setSize( le_cmd->info.numBytes );

					::vk::ImageMemoryBarrier imageLayoutToTransferDstOptimal;
					imageLayoutToTransferDstOptimal
					    .setSrcAccessMask( {} )                                   // no prior access
					    .setDstAccessMask( ::vk::AccessFlagBits::eTransferWrite ) // ready image for transferwrite
					    .setOldLayout( ::vk::ImageLayout::eUndefined )            // from don't care
					    .setNewLayout( ::vk::ImageLayout::eTransferDstOptimal )   // to transfer destination optimal
					    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
					    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
					    .setImage( dstImage )
					    .setSubresourceRange( subresourceRange );

					cmd.pipelineBarrier(
					    ::vk::PipelineStageFlagBits::eHost,
					    ::vk::PipelineStageFlagBits::eTransfer,
					    {},
					    {},
					    {bufferTransferBarrier},          // buffer: host write -> transfer read
					    {imageLayoutToTransferDstOptimal} // image: prepare for transfer write
					);

					cmd.copyBufferToImage( srcBuffer, dstImage, ::vk::ImageLayout::eTransferDstOptimal, 1, &region );

					break;
				}

				} // end switch header.info.type

				// Move iterator by size of current le_command so that it points
				// to the next command in the list.
				dataIt = static_cast<char *>( dataIt ) + header->info.size;

				++commandIndex;
			}
		}

		// non-draw passes don't need renderpasses.
		if ( pass.type == LE_RENDER_PASS_TYPE_DRAW && pass.renderPass ) {
			cmd.endRenderPass();
		}

		cmd.end();
	}

	// place command buffer in frame store so that it can be submitted.
	for ( auto &&c : cmdBufs ) {
		frame.commandBuffers.emplace_back( c );
	}
}

// ----------------------------------------------------------------------
// FIXME: remove forwarding via renderer to here
static void backend_update_shader_modules( le_backend_o *self ) {
	using namespace le_backend_vk;
	le_pipeline_manager_i.update_shader_modules( self->pipelineCache );
}

// ----------------------------------------------------------------------
// FIXME: remove forwarding via renderer to here
static le_shader_module_o *backend_create_shader_module( le_backend_o *self, char const *path, LeShaderType moduleType ) {
	using namespace le_backend_vk;
	return le_pipeline_manager_i.create_shader_module( self->pipelineCache, path, moduleType );
}

// ----------------------------------------------------------------------

static le_pipeline_manager_o *backend_get_pipeline_cache( le_backend_o *self ) {
	return self->pipelineCache;
}

// ----------------------------------------------------------------------

static bool backend_dispatch_frame( le_backend_o *self, size_t frameIndex ) {

	auto &frame = self->mFrames[ frameIndex ];

	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = {{::vk::PipelineStageFlagBits::eColorAttachmentOutput}};

	vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount( 1 )
	    .setPWaitSemaphores( &frame.semaphorePresentComplete )
	    .setPWaitDstStageMask( wait_dst_stage_mask.data() )
	    .setCommandBufferCount( uint32_t( frame.commandBuffers.size() ) )
	    .setPCommandBuffers( frame.commandBuffers.data() )
	    .setSignalSemaphoreCount( 1 )
	    .setPSignalSemaphores( &frame.semaphoreRenderComplete );

	auto queue = vk::Queue{self->device->getDefaultGraphicsQueue()};

	queue.submit( {submitInfo}, frame.frameFence );

	bool presentSuccessful = self->swapchain->present( self->device->getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex );

	return presentSuccessful;
}

// ----------------------------------------------------------------------

static le_resource_info_t get_default_resource_info_for_image() {
	le_resource_info_t res;

	res.type = LeResourceType::eImage;
	{
		auto &img         = res.image;
		img.format        = VK_FORMAT_R8G8B8A8_UNORM; // TODO: query this via device
		img.flags         = 0;
		img.arrayLayers   = 1;
		img.extent.width  = 0;
		img.extent.height = 0;
		img.extent.depth  = 1;
		img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT;
		img.mipLevels     = 1;
		img.samples       = VK_SAMPLE_COUNT_1_BIT;
		img.imageType     = VK_IMAGE_TYPE_2D;
		img.tiling        = VK_IMAGE_TILING_OPTIMAL;
	}

	return res;
}

static le_resource_info_t get_default_resource_info_for_image_attachment() {
	le_resource_info_t res;

	res.type = LeResourceType::eImage;
	{
		auto &img         = res.image;
		img.format        = VK_FORMAT_R8G8B8A8_UNORM; // TODO: query this via device
		img.flags         = 0;
		img.arrayLayers   = 1;
		img.extent.width  = 0;
		img.extent.height = 0;
		img.extent.depth  = 1;
		img.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		img.mipLevels     = 1;
		img.samples       = VK_SAMPLE_COUNT_1_BIT;
		img.imageType     = VK_IMAGE_TYPE_2D;
		img.tiling        = VK_IMAGE_TILING_OPTIMAL;
	}

	return res;
}

static le_resource_info_t get_default_resource_info_for_depth_stencil_attachment() {
	le_resource_info_t res;

	res.type = LeResourceType::eImage;
	{
		auto &img         = res.image;
		img.format        = VK_FORMAT_D32_SFLOAT_S8_UINT; // TODO: query this via device
		img.flags         = 0;
		img.arrayLayers   = 1;
		img.extent.width  = 0;
		img.extent.height = 0;
		img.extent.depth  = 1;
		img.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		img.mipLevels     = 1;
		img.samples       = VK_SAMPLE_COUNT_1_BIT;
		img.imageType     = VK_IMAGE_TYPE_2D;
		img.tiling        = VK_IMAGE_TILING_OPTIMAL;
	}

	return res;
}

// ----------------------------------------------------------------------

static le_resource_info_t get_default_resource_info_for_buffer() {
	le_resource_info_t res;
	res.type         = LeResourceType::eBuffer;
	res.buffer.size  = 0;
	res.buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	return res;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_backend_vk_api( void *api_ ) {
	auto  api_i        = static_cast<le_backend_vk_api *>( api_ );
	auto &vk_backend_i = api_i->vk_backend_i;

	vk_backend_i.create                     = backend_create;
	vk_backend_i.destroy                    = backend_destroy;
	vk_backend_i.setup                      = backend_setup;
	vk_backend_i.get_num_swapchain_images   = backend_get_num_swapchain_images;
	vk_backend_i.reset_swapchain            = backend_reset_swapchain;
	vk_backend_i.get_transient_allocators   = backend_get_transient_allocators;
	vk_backend_i.poll_frame_fence           = backend_poll_frame_fence;
	vk_backend_i.clear_frame                = backend_clear_frame;
	vk_backend_i.acquire_physical_resources = backend_acquire_physical_resources;
	vk_backend_i.process_frame              = backend_process_frame;
	vk_backend_i.dispatch_frame             = backend_dispatch_frame;

	vk_backend_i.get_pipeline_cache    = backend_get_pipeline_cache;
	vk_backend_i.update_shader_modules = backend_update_shader_modules;
	vk_backend_i.create_shader_module  = backend_create_shader_module;

	vk_backend_i.get_backbuffer_resource = backend_get_backbuffer_resource;

	auto &helpers_i = api_i->helpers_i;

	helpers_i.get_default_resource_info_for_buffer                   = get_default_resource_info_for_buffer;
	helpers_i.get_default_resource_info_for_image                    = get_default_resource_info_for_image;
	helpers_i.get_default_resource_info_for_image_attachment         = get_default_resource_info_for_image_attachment;
	helpers_i.get_default_resource_info_for_depth_stencil_attachment = get_default_resource_info_for_depth_stencil_attachment;

	// register/update submodules inside this plugin

	register_le_device_vk_api( api_ );
	register_le_instance_vk_api( api_ );
	register_le_allocator_linear_api( api_ );
	register_le_pipeline_vk_api( api_ );

	auto &le_instance_vk_i = api_i->vk_instance_i;

	if ( api_i->cUniqueInstance != nullptr ) {
		le_instance_vk_i.post_reload_hook( api_i->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
