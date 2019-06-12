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

#ifndef DEBUG_TAG_RESOURCES
// Whether to tag resources - requires the debugUtils extension to be present.
#	define DEBUG_TAG_RESOURCES true
#endif

// Helper macro to convert le:: enums to vk:: enums
#define LE_ENUM_TO_VK( enum_name, fun_name )                                    \
	static inline vk::enum_name fun_name( le::enum_name const &rhs ) noexcept { \
        return vk::enum_name( rhs );                                            \
	}

#define LE_C_ENUM_TO_VK( enum_name, fun_name, c_enum_name )                   \
	static inline vk::enum_name fun_name( c_enum_name const &rhs ) noexcept { \
        return vk::enum_name( rhs );                                          \
	}

constexpr size_t LE_FRAME_DATA_POOL_BLOCK_SIZE  = 1u << 24; // 16.77 MB
constexpr size_t LE_FRAME_DATA_POOL_BLOCK_COUNT = 1;
constexpr size_t LE_LINEAR_ALLOCATOR_SIZE       = 1u << 24;
// ----------------------------------------------------------------------
/// ResourceCreateInfo is used internally in to translate Renderer-specific structures
/// into Vulkan CreateInfos for buffers and images we wish to allocate in Vulkan.
///
/// The ResourceCreateInfo is then stored with the allocation, so that subsequent
/// requests for resources may check if a requested resource is already available to the
/// backend.
struct ResourceCreateInfo {

	// Since this is a union, the first field will for both be VK_STRUCTURE_TYPE
	// and its value will tell us what type the descriptor represents.
	union {
		VkBufferCreateInfo bufferInfo; // | only one of either ever in use
		VkImageCreateInfo  imageInfo;  // | only one of either ever in use
	};

	// Compares two ResourceCreateInfos, returns true if identical, false if not.
	//
	// FIXME: the comparison of pQueueFamilyIndices is fraught with peril,
	// as we must really compare the contents of the memory pointed at
	// rather than the pointer, and the pointer has no guarantee to be alife.
	bool operator==( const ResourceCreateInfo &rhs ) const {

		if ( bufferInfo.sType == rhs.bufferInfo.sType ) {

			if ( bufferInfo.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO ) {

				return ( bufferInfo.flags == rhs.bufferInfo.flags &&
				         bufferInfo.size == rhs.bufferInfo.size &&
				         bufferInfo.usage == rhs.bufferInfo.usage &&
				         bufferInfo.sharingMode == rhs.bufferInfo.sharingMode &&
				         bufferInfo.queueFamilyIndexCount == rhs.bufferInfo.queueFamilyIndexCount &&
				         bufferInfo.pQueueFamilyIndices == rhs.bufferInfo.pQueueFamilyIndices // should not be compared this way
				);

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
				         imageInfo.initialLayout == rhs.imageInfo.initialLayout &&
				         imageInfo.queueFamilyIndexCount == rhs.imageInfo.queueFamilyIndexCount &&
				         imageInfo.pQueueFamilyIndices == rhs.imageInfo.pQueueFamilyIndices // should not be compared this way
				);
			}

		} else {
			// not the same type of descriptor
			return false;
		}
	}

	// Greater-than operator returns true if rhs is a subset of this.
	// We use this operator to see whether we can re-use an existing resource
	// based on the currently allocated version of a resource.
	//
	// Note that we are only fuzzy where it is safe to be so - which is flags.
	bool operator>=( const ResourceCreateInfo &rhs ) const {

		if ( bufferInfo.sType == rhs.bufferInfo.sType ) {

			if ( bufferInfo.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO ) {

				return ( bufferInfo.flags == rhs.bufferInfo.flags &&
				         bufferInfo.size == rhs.bufferInfo.size &&
				         ( ( bufferInfo.usage & rhs.bufferInfo.usage ) == rhs.bufferInfo.usage ) &&
				         bufferInfo.sharingMode == rhs.bufferInfo.sharingMode &&
				         bufferInfo.queueFamilyIndexCount == rhs.bufferInfo.queueFamilyIndexCount &&
				         bufferInfo.pQueueFamilyIndices == rhs.bufferInfo.pQueueFamilyIndices // should not be compared this way
				);

			} else {

				// For flags to be greater or equal means that all flags from
				// rhs must be found in lhs:
				// flags_rhs == (this.flags & flags_rhs)

				// Note this.format, and this.extent passes the test:
				// a) if this.x is identical with rhs.x,
				// b) iff this.x is defined, *and* rhs.x is undefined.

				return ( ( ( imageInfo.flags & rhs.imageInfo.flags ) == rhs.imageInfo.flags ) &&
				         imageInfo.imageType == rhs.imageInfo.imageType &&
				         ( imageInfo.format == rhs.imageInfo.format || ( imageInfo.format != VK_FORMAT_UNDEFINED && rhs.imageInfo.format == VK_FORMAT_UNDEFINED ) ) &&
				         ( imageInfo.extent.width == rhs.imageInfo.extent.width || ( imageInfo.extent.width != 0 && rhs.imageInfo.extent.width == 0 ) ) &&
				         ( imageInfo.extent.height == rhs.imageInfo.extent.height || ( imageInfo.extent.height != 0 && rhs.imageInfo.extent.height == 0 ) ) &&
				         ( imageInfo.extent.depth == rhs.imageInfo.extent.depth || ( imageInfo.extent.depth != 0 && rhs.imageInfo.extent.depth == 0 ) ) &&
				         imageInfo.mipLevels >= rhs.imageInfo.mipLevels &&
				         imageInfo.arrayLayers >= rhs.imageInfo.arrayLayers &&
				         imageInfo.samples == rhs.imageInfo.samples &&
				         imageInfo.tiling == rhs.imageInfo.tiling &&
				         ( ( imageInfo.usage & rhs.imageInfo.usage ) == rhs.imageInfo.usage ) &&
				         imageInfo.sharingMode == rhs.imageInfo.sharingMode &&
				         imageInfo.initialLayout == rhs.imageInfo.initialLayout &&
				         imageInfo.queueFamilyIndexCount == rhs.imageInfo.queueFamilyIndexCount &&
				         imageInfo.pQueueFamilyIndices == rhs.imageInfo.pQueueFamilyIndices // should not be compared this way
				);
			}

		} else {
			// not the same type of descriptor
			return false;
		}
	}

	bool operator!=( const ResourceCreateInfo &rhs ) const {
		return !operator==( rhs );
	}

	bool isBuffer() const {
		return bufferInfo.sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	}

	bool isImage() const {
		return imageInfo.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	}

	static ResourceCreateInfo from_le_resource_info( const le_resource_info_t &info, uint32_t *pQueueFamilyIndices, uint32_t queueFamilyindexCount );
};

// ----------------------------------------------------------------------

static inline const vk::ClearValue &le_clear_value_to_vk( const LeClearValue &lhs ) {
	static_assert( sizeof( vk::ClearValue ) == sizeof( LeClearValue ), "Clear value type size must be equal between Le and Vk" );
	return reinterpret_cast<const vk::ClearValue &>( lhs );
}

// ----------------------------------------------------------------------

static inline constexpr le::Format vk_format_to_le( const vk::Format &format ) noexcept {
	return le::Format( format );
}

// ----------------------------------------------------------------------

LE_C_ENUM_TO_VK( ImageUsageFlagBits, le_image_usage_flags_to_vk, LeImageUsageFlags_t );
LE_C_ENUM_TO_VK( ImageCreateFlags, le_image_create_flags_to_vk, LeImageCreateFlags );
LE_ENUM_TO_VK( SampleCountFlagBits, le_sample_count_flag_bits_to_vk );
LE_ENUM_TO_VK( ImageTiling, le_image_tiling_to_vk );
LE_ENUM_TO_VK( ImageType, le_image_type_to_vk );
LE_ENUM_TO_VK( Format, le_format_to_vk );
LE_ENUM_TO_VK( AttachmentLoadOp, le_attachment_load_op_to_vk );
LE_ENUM_TO_VK( AttachmentStoreOp, le_attachment_store_op_to_vk );
LE_ENUM_TO_VK( Filter, le_filter_to_vk );
LE_ENUM_TO_VK( SamplerMipmapMode, le_sampler_mipmap_mode_to_vk );
LE_ENUM_TO_VK( SamplerAddressMode, le_sampler_address_mode_to_vk );
LE_ENUM_TO_VK( CompareOp, le_compare_op_to_vk );
LE_ENUM_TO_VK( BorderColor, le_border_color_to_vk );

// ----------------------------------------------------------------------

ResourceCreateInfo ResourceCreateInfo::from_le_resource_info( const le_resource_info_t &info, uint32_t *pQueueFamilyIndices, uint32_t queueFamilyIndexCount ) {
	ResourceCreateInfo res;

	switch ( info.type ) {
	case ( LeResourceType::eBuffer ): {
		res.bufferInfo = vk::BufferCreateInfo()
		                     .setFlags( {} )
		                     .setSize( info.buffer.size )
		                     .setUsage( vk::BufferUsageFlags{info.buffer.usage} )
		                     .setSharingMode( vk::SharingMode::eExclusive )
		                     .setQueueFamilyIndexCount( queueFamilyIndexCount )
		                     .setPQueueFamilyIndices( pQueueFamilyIndices );

	} break;
	case ( LeResourceType::eImage ): {
		auto const &img = info.image;
		res.imageInfo   = vk::ImageCreateInfo()
		                    .setFlags( le_image_create_flags_to_vk( img.flags ) )                 //
		                    .setImageType( le_image_type_to_vk( img.imageType ) )                 //
		                    .setFormat( le_format_to_vk( img.format ) )                           //
		                    .setExtent( {img.extent.width, img.extent.height, img.extent.depth} ) //
		                    .setMipLevels( img.mipLevels )                                        //
		                    .setArrayLayers( img.arrayLayers )                                    //
		                    .setSamples( le_sample_count_flag_bits_to_vk( img.samples ) )         //
		                    .setTiling( le_image_tiling_to_vk( img.tiling ) )                     //
		                    .setUsage( le_image_usage_flags_to_vk( img.usage ) )                  //
		                    .setSharingMode( vk::SharingMode::eExclusive )                        // hardcoded to Exclusive - no sharing between queues
		                    .setQueueFamilyIndexCount( queueFamilyIndexCount )                    //
		                    .setPQueueFamilyIndices( pQueueFamilyIndices )                        //
		                    .setInitialLayout( vk::ImageLayout::eUndefined )                      // must be either pre-initialised, or undefined (most likely)
		    ;

	} break;
	default:
		assert( false ); // we can only create (allocate) buffer or image resources
        break;
	}

	return res;
}

// ResourceState keeps track of the resource stage *before* a barrier
struct ResourceState {
	vk::AccessFlags        visible_access; // which memory access must be be visible - if any of these are WRITE accesses, these must be made available(flushed) before next access - for the next src access we can OR this with ANY_WRITES
	vk::PipelineStageFlags write_stage;    // current or last stage at which write occurs
	vk::ImageLayout        layout;         // current layout (for images)

	bool operator==( const ResourceState &rhs ) const {
		return visible_access == rhs.visible_access &&
		       write_stage == rhs.write_stage &&
		       layout == rhs.layout;
	}

	bool operator!=( const ResourceState &rhs ) const {
		return !operator==( rhs );
	}
};

// ------------------------------------------------------------

struct AllocatedResourceVk {
	VmaAllocation     allocation;
	VmaAllocationInfo allocationInfo;
	union {
		VkBuffer asBuffer;
		VkImage  asImage;
	};
	ResourceCreateInfo info;  // Creation info for resource
	ResourceState      state; // sync state for resource
	uint32_t           padding__;
};

struct le_staging_allocator_o {
	VmaAllocator                   allocator;      // non-owning, refers to backend allocator object
	VkDevice                       device;         // non-owning, refers to vulkan device object
	std::mutex                     mtx;            // protects all staging* elements
	std::vector<vk::Buffer>        buffers;        // 0..n staging buffers used with the current frame (freed on frame clear)
	std::vector<VmaAllocation>     allocations;    // SOA: counterpart to buffers[]
	std::vector<VmaAllocationInfo> allocationInfo; // SOA: counterpart to buffers[]
};

// ------------------------------------------------------------

// Herein goes all data which is associated with the current frame.
// Backend keeps track of multiple frames, exactly one per renderer::FrameData frame.
//
// We do this so that frames own their own memory exclusively, as long as a
// frame only operates only on its own memory, it will never see contention
// with other threads processing other frames concurrently.
struct BackendFrameData {
	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	uint32_t                       swapchainWidth           = 0; // Swapchain may be resized, therefore it needs to be stored with frame
	uint32_t                       swapchainHeight          = 0; // Swapchain may be resized, therefore it needs to be stored with frame
	std::vector<vk::CommandBuffer> commandBuffers;

	struct Texture {
		vk::Sampler   sampler;
		vk::ImageView imageView;
	};

	std::unordered_map<le_resource_handle_t, Texture, LeResourceHandleIdentity>       textures;   // non-owning, references to frame-local textures, cleared on frame fence.
	std::unordered_map<le_resource_handle_t, vk::ImageView, LeResourceHandleIdentity> imageViews; // non-owning, references to frame-local textures, cleared on frame fence.

	// With `syncChainTable` and image_attachment_info_o.syncState, we should
	// be able to create renderpasses. Each resource has a sync chain, and each attachment_info
	// has a struct which holds indices into the sync chain telling us where to look
	// up the sync state for a resource at different stages of renderpass construction.
	std::unordered_map<le_resource_handle_t, std::vector<ResourceState>, LeResourceHandleIdentity> syncChainTable;

	static_assert( sizeof( VkBuffer ) == sizeof( VkImageView ) && sizeof( VkBuffer ) == sizeof( VkImage ), "size of AbstractPhysicalResource components must be identical" );

	// Map from renderer resource id to physical resources - only contains resources this frame uses.
	// Q: Does this table actually own the resources?
	// A: It must not: as it is used to map external resources as well.
	std::unordered_map<le_resource_handle_t, AbstractPhysicalResource, LeResourceHandleIdentity> physicalResources;

	/// \brief vk resources retained and destroyed with BackendFrameData
	std::forward_list<AbstractPhysicalResource> ownedResources;

	/// \brief if user provides explicit resource info, we collect this here, so that we can make sure
	/// that any inferred resourceInfo is compatible with what the user selected.
	std::vector<le_resource_handle_t> declared_resources_id;   // | pre-declared resources (declared via module)
	std::vector<le_resource_info_t>   declared_resources_info; // | pre-declared resources (declared via module)

	std::vector<LeRenderPass>       passes;
	std::vector<vk::DescriptorPool> descriptorPools; // one descriptor pool per pass

	/*

	  Each Frame has one allocation pool from which all allocations for scratch buffers are drawn.

	  When creating encoders, each encoder has their own sub-allocator, each sub-allocator owns an
	  independent block of memory allcated from the frame pool. This way, encoders can work on their
	  own thread.

	 */

	typedef std::unordered_map<le_resource_handle_t, AllocatedResourceVk, LeResourceHandleIdentity> ResourceMap_T;

	ResourceMap_T availableResources; // resources this frame may use
	ResourceMap_T binnedResources;    // resources to delete when this frame comes round to clear()

	VmaPool                        allocationPool;   // pool from which allocations for this frame come from
	std::vector<le_allocator_o *>  allocators;       // one linear sub-allocator per command buffer
	std::vector<vk::Buffer>        allocatorBuffers; // one vkBuffer per command buffer
	std::vector<VmaAllocation>     allocations;      // one allocation per command buffer
	std::vector<VmaAllocationInfo> allocationInfos;  // one allocation info per command buffer

	le_staging_allocator_o *stagingAllocator; // owning
};

// ----------------------------------------------------------------------

/// \brief backend data object
struct le_backend_o {

	std::unique_ptr<le::Instance> instance;
	std::unique_ptr<le::Device>   device;

	pal_window_o *  window    = nullptr; // Non-owning
	le_swapchain_o *swapchain = nullptr; // Owning.

	vk::SurfaceKHR windowSurface = nullptr; // Owning, optional.

	// Default color formats are inferred during setup() based on
	// swapchain surface (color) and device properties (depth/stencil)
	vk::Format swapchainImageFormat                = {}; ///< default image format used for swapchain (backbuffer image must be in this format)
	le::Format defaultFormatColorAttachment        = {}; ///< default image format used for color attachments
	le::Format defaultFormatDepthStencilAttachment = {}; ///< default image format used for depth stencil attachments
	le::Format defaultFormatSampledImage           = {}; ///< default image format used for sampled images

	// Siloed per-frame memory
	std::vector<BackendFrameData> mFrames;

	le_pipeline_manager_o *pipelineCache = nullptr;

	VmaAllocator mAllocator = nullptr;

	uint32_t swapchainWidth  = 0; ///< swapchain width gathered when setting/resetting swapchain
	uint32_t swapchainHeight = 0; ///< swapchain height gathered when setting/resetting swapchain

	uint32_t queueFamilyIndexGraphics = 0; // inferred during setup
	uint32_t queueFamilyIndexCompute  = 0; // inferred during setup

	const le_resource_handle_t swapchainImageHandle = LE_IMG_RESOURCE( "Backbuffer-Image" ); // opaque handle identifying the backbuffer image, initialised in setup()

	struct {
		std::unordered_map<le_resource_handle_t, AllocatedResourceVk, LeResourceHandleIdentity> allocatedResources; // Allocated resources, indexed by resource name hash
	} only_backend_allocate_resources_may_access;                                                                   // Only acquire_physical_resources may read/write

	const vk::BufferUsageFlags LE_BUFFER_USAGE_FLAGS_SCRATCH =
	    vk::BufferUsageFlagBits::eIndexBuffer |
	    vk::BufferUsageFlagBits::eVertexBuffer |
	    vk::BufferUsageFlagBits::eUniformBuffer |
	    vk::BufferUsageFlagBits::eStorageBuffer |
	    vk::BufferUsageFlagBits::eTransferSrc;
};

// ----------------------------------------------------------------------

static inline void vk_format_get_is_depth_stencil( vk::Format format_, bool &isDepth, bool &isStencil ) {

    switch ( format_ ) {
    case vk::Format::eD16Unorm:         // fall-through
    case vk::Format::eX8D24UnormPack32: // fall-through
    case vk::Format::eD32Sfloat:        // fall-through
        isDepth   = true;
        isStencil = false;
        break;
    case vk::Format::eS8Uint:
        isDepth   = false;
        isStencil = true;
        break;
    case vk::Format::eD16UnormS8Uint:  // fall-through
    case vk::Format::eD24UnormS8Uint:  // fall-through
    case vk::Format::eD32SfloatS8Uint: // fall-through
        isDepth = isStencil = true;
        break;

    default:
        isDepth = isStencil = false;
        break;
    }

    return;
}

// ----------------------------------------------------------------------

static vk::SurfaceKHR backend_create_window_surface( le_backend_o *self ) {
	if ( self->window ) {
		using namespace pal_window;
		return window_i.create_surface( self->window, self->instance->getVkInstance() );
	}
	return nullptr;
}

// ----------------------------------------------------------------------
static void backend_destroy_window_surface( le_backend_o *self ) {
	if ( self->windowSurface ) {
		vk::Instance instance = self->instance->getVkInstance();
		instance.destroySurfaceKHR( self->windowSurface );
		std::cout << "Surface was destroyed." << std::endl
		          << std::flush;
	}
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

	// We must destroy the swapchain before self->mAllocator, as
	// the swapchain might have allocated memory using the backend's allocator,
	// and the allocator must still be alive for the swapchain to free objects
	// alloacted through it.

	if ( self->swapchain ) {
		using namespace le_swapchain_vk;
		swapchain_i.destroy( self->swapchain );
		self->swapchain = nullptr;
	}

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

		// destroy staging allocator
		le_staging_allocator_i.destroy( frameData.stagingAllocator );

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

	// destroy window surface if there was a window surface
	backend_destroy_window_surface( self );

	delete self;
}

// ----------------------------------------------------------------------

static void backend_create_swapchain( le_backend_o *self, le_swapchain_settings_t *swapchainSettings_ ) {

	le_swapchain_settings_t swp_settings{};

	if ( swapchainSettings_ ) {
		swp_settings = *swapchainSettings_;
	}

	// Set default settings if not user specified for certain swapchain settings

	if ( swp_settings.imagecount_hint == 0 ) {
		swp_settings.imagecount_hint = 3;
	}

	switch ( swp_settings.type ) {

	case le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN: {
		using namespace le_swapchain_vk;
		// Create an image swapchain
		self->swapchain = swapchain_i.create( swapchain_img_i, self, &swp_settings );
	} break;

	case le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN: {
		using namespace le_swapchain_vk;

		if ( self->window ) {
			// If we're running with a window, we pass through swapchainSettings,
			// and initialise our swapchain as a regular khr swapchain
			using namespace pal_window;

			swp_settings.width_hint              = window_i.get_surface_width( self->window );
			swp_settings.height_hint             = window_i.get_surface_height( self->window );
			swp_settings.khr_settings.vk_surface = self->windowSurface; // we need this so that swapchain can query surface capabilities

			self->swapchain = swapchain_i.create( swapchain_khr_i, self, &swp_settings );

		} else {
			// cannot run a khr swapchain without a window.
		}

	} break;
	}

	// The following settings are not user-hintable, and will get overridden by default
	if ( self->window ) {

	} else {
	}

	using namespace le_swapchain_vk;
	self->swapchainImageFormat = vk::Format( swapchain_i.get_surface_format( self->swapchain )->format );
	self->swapchainWidth       = swapchain_i.get_image_width( self->swapchain );
	self->swapchainHeight      = swapchain_i.get_image_height( self->swapchain );
}

// ----------------------------------------------------------------------

static size_t backend_get_num_swapchain_images( le_backend_o *self ) {
	assert( self->swapchain );
	using namespace le_swapchain_vk;
	return swapchain_i.get_images_count( self->swapchain );
}

// ----------------------------------------------------------------------
// Returns the current swapchain width and height.
// Both values are cached, and re-calculated whenever the swapchain is set / or reset.
static void backend_get_swapchain_extent( le_backend_o *self, uint32_t *p_width, uint32_t *p_height ) {
	*p_width  = self->swapchainWidth;
	*p_height = self->swapchainHeight;
}

// ----------------------------------------------------------------------

static void backend_reset_swapchain( le_backend_o *self ) {
	using namespace le_swapchain_vk;

	swapchain_i.reset( self->swapchain, nullptr );
	// We must update our cached values for swapchain dimensions if the swapchain was reset.
	self->swapchainWidth  = swapchain_i.get_image_width( self->swapchain );
	self->swapchainHeight = swapchain_i.get_image_height( self->swapchain );
}

// ----------------------------------------------------------------------

/// \brief Declare a resource as a virtual buffer
/// \details This is an internal method. Virtual buffers are buffers which don't have individual
/// Vulkan buffer backing. Instead, they use their Frame's buffer for storage. Virtual buffers
/// are used to store Frame-local transient data such as values for shader parameters.
/// Each Encoder uses its own virtual buffer for such purposes.
static le_resource_handle_t declare_resource_virtual_buffer( uint8_t index ) {

	auto resource = LE_RESOURCE( "Encoder-Virtual", LeResourceType::eBuffer ); // virtual resources all have the same id, which means they are not part of the regular roster of resources...

	resource.meta.index = index; // encoder index
	resource.meta.flags = le_resource_handle_t::FlagBits::eIsVirtual;

	return resource;
}

// ----------------------------------------------------------------------

static le_resource_handle_t backend_get_swapchain_resource( le_backend_o *self ) {
	return self->swapchainImageHandle;
}

// ----------------------------------------------------------------------

static VkDevice backend_get_vk_device( le_backend_o *self ) {
	return self->device->getVkDevice();
};

// ----------------------------------------------------------------------

static VkPhysicalDevice backend_get_vk_physical_device( le_backend_o *self ) {
	return self->device->getVkPhysicalDevice();
};

// ----------------------------------------------------------------------

static int32_t backend_allocate_image( le_backend_o *                 self,
                                       VkImageCreateInfo const *      pImageCreateInfo,
                                       VmaAllocationCreateInfo const *pAllocationCreateInfo,
                                       VkImage *                      pImage,
                                       VmaAllocation *                pAllocation,
                                       VmaAllocationInfo *            pAllocationInfo ) {

	auto result = vmaCreateImage( self->mAllocator,
	                              pImageCreateInfo,
	                              pAllocationCreateInfo,
	                              pImage,
	                              pAllocation,
	                              pAllocationInfo );
	return result;
}

// ----------------------------------------------------------------------

static void backend_destroy_image( le_backend_o *self, VkImage image, VmaAllocation allocation ) {
	vmaDestroyImage( self->mAllocator, image, allocation );
}

// ----------------------------------------------------------------------

static int32_t backend_allocate_buffer( le_backend_o *                 self,
                                        VkBufferCreateInfo const *     pBufferCreateInfo,
                                        VmaAllocationCreateInfo const *pAllocationCreateInfo,
                                        VkBuffer *                     pBuffer,
                                        VmaAllocation *                pAllocation,
                                        VmaAllocationInfo *            pAllocationInfo ) {
	auto result = vmaCreateBuffer( self->mAllocator, pBufferCreateInfo, pAllocationCreateInfo, pBuffer, pAllocation, pAllocationInfo );
	return result;
}

// ----------------------------------------------------------------------

static void backend_destroy_buffer( le_backend_o *self, VkBuffer buffer, VmaAllocation allocation ) {
	vmaDestroyBuffer( self->mAllocator, buffer, allocation );
}

// ----------------------------------------------------------------------

static le_device_o *backend_get_le_device( le_backend_o *self ) {
	return *self->device;
}

// ----------------------------------------------------------------------

static void backend_setup( le_backend_o *self, le_backend_vk_settings_t *settings ) {

	assert( settings );
	if ( settings == nullptr ) {
		std::cerr << "FATAL: Must specify settings for backend." << std::endl
		          << std::flush;
		exit( 1 );
	}

	// -- if window surface, query required vk extensions from glfw

	std::vector<char const *> requestedInstanceExtensions;
	{
		if ( settings->pWindow ) {

			// -- insert extensions necessary for glfw window

			uint32_t extensionCount         = 0;
			auto     glfwRequiredExtensions = pal::Window( settings->pWindow ).getRequiredVkExtensions( &extensionCount );

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
	self->window   = settings->pWindow;

	{
		using namespace le_backend_vk;
		self->pipelineCache = le_pipeline_manager_i.create( self->device->getVkDevice() );
	}

	// -- create window surface if requested
	self->windowSurface = backend_create_window_surface( self );

	vk::Device         vkDevice         = self->device->getVkDevice();
	vk::PhysicalDevice vkPhysicalDevice = self->device->getVkPhysicalDevice();

	{
		// -- Create allocator for backend vulkan memory
		// we do this here, because swapchain might want to already use the allocator.

		VmaAllocatorCreateInfo createInfo{};
		createInfo.flags                       = 0;
		createInfo.device                      = vkDevice;
		createInfo.frameInUseCount             = 0;
		createInfo.physicalDevice              = vkPhysicalDevice;
		createInfo.preferredLargeHeapBlockSize = 0; // set to default, currently 256 MB

		vmaCreateAllocator( &createInfo, &self->mAllocator );
	}

	// -- create swapchain if requested

	backend_create_swapchain( self, settings->pSwapchain_settings );

	// -- setup backend memory objects

	auto frameCount = backend_get_num_swapchain_images( self );

	self->mFrames.reserve( frameCount );

	self->queueFamilyIndexGraphics = self->device->getDefaultGraphicsQueueFamilyIndex();
	self->queueFamilyIndexCompute  = self->device->getDefaultComputeQueueFamilyIndex();

	uint32_t memIndexScratchBufferGraphics = 0;
	uint32_t memIndexStagingBufferGraphics = 0;
	{

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

			vmaFindMemoryTypeIndexForBufferInfo( self->mAllocator, &reinterpret_cast<VkBufferCreateInfo &>( bufferInfo ), &allocInfo, &memIndexScratchBufferGraphics );
		}

		{
			// Find memory index for staging buffer - we do this by pretending to create
			// an allocation.

			vk::BufferCreateInfo bufferInfo{};
			bufferInfo
			    .setFlags( {} )
			    .setSize( 1 )
			    .setUsage( vk::BufferUsageFlagBits::eTransferSrc )
			    .setSharingMode( vk::SharingMode::eExclusive )
			    .setQueueFamilyIndexCount( 1 )
			    .setPQueueFamilyIndices( &self->queueFamilyIndexGraphics );

			VmaAllocationCreateInfo allocInfo{};
			allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

			vmaFindMemoryTypeIndexForBufferInfo( self->mAllocator, &reinterpret_cast<VkBufferCreateInfo &>( bufferInfo ), &allocInfo, &memIndexStagingBufferGraphics );
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

			VmaPoolCreateInfo poolInfo{};
			poolInfo.blockSize       = LE_FRAME_DATA_POOL_BLOCK_SIZE; // 16.77MB
			poolInfo.flags           = VmaPoolCreateFlagBits::VMA_POOL_CREATE_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT;
			poolInfo.memoryTypeIndex = memIndexScratchBufferGraphics;
			poolInfo.frameInUseCount = 0;
			poolInfo.minBlockCount   = LE_FRAME_DATA_POOL_BLOCK_COUNT;
			vmaCreatePool( self->mAllocator, &poolInfo, &frameData.allocationPool );
		}

		// -- create a staging allocator for this frame
		using namespace le_backend_vk;
		frameData.stagingAllocator = le_staging_allocator_i.create( self->mAllocator, vkDevice );

		self->mFrames.emplace_back( std::move( frameData ) );
	}

	{
		// Set default image formats

		using namespace le_backend_vk;

		self->defaultFormatColorAttachment        = vk_format_to_le( self->swapchainImageFormat );
		self->defaultFormatDepthStencilAttachment = vk_format_to_le( vk_device_i.get_default_depth_stencil_format( *self->device ) );

		// We hard-code default format for sampled images, since this is the most likely
		// format we will encounter bitmaps to be encoded in, and there is no good way
		// to infer it.
		self->defaultFormatSampledImage = le::Format::eR8G8B8A8Unorm;
	}

	// CHECK: this is where we used to create the vulkan pipeline cache object
}

// ----------------------------------------------------------------------
// Updates sync chain for resourcess referenced in rendergraph
// each renderpass contains offsets into sync chain for given resource used by renderpass.
// resource sync state for images used as renderpass attachments is chosen so that they
// can be implicitly synced using subpass dependencies.
static void frame_track_resource_state( BackendFrameData &frame, le_renderpass_o **ppPasses, size_t numRenderPasses, const le_resource_handle_t &backbufferImageHandle ) {

	// A pipeline barrier is defined as a combination of EXECUTION dependency and MEMORY dependency:
	//
	// * An EXECUTION DEPENDENCY tells us which stage needs to be complete (srcStage) before another named stage (dstStage) may execute.
	// * A  MEMORY DEPENDECY     tells us which memory/cache needs to be made available/flushed (srcAccess) after srcStage,
	//   before another memory/cache can be made visible/invalidated (dstAccess) before dstStage

	// Renderpass implicit sync (per image resource)
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
	//
	//- NOTE texture image resources *must* be explicitly synchronised:

	auto &syncChainTable = frame.syncChainTable;

	{

		// -- backbuffer has their sync state changed outside of our frame graph
		// because submitting to the swapchain changes its sync state.
		// We must adjust the backbuffer sync-chain table to account for this.

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

	using namespace le_renderer;

	auto get_stage_flags_based_on_renderpass_type = []( LeRenderPassType const &rp_type ) -> vk::PipelineStageFlags {
		// write_stage depends on current renderpass type.
		switch ( rp_type ) {
		case LE_RENDER_PASS_TYPE_TRANSFER:
            return vk::PipelineStageFlagBits::eTransfer; // stage for transfer pass
		case LE_RENDER_PASS_TYPE_DRAW:
            return vk::PipelineStageFlagBits::eVertexShader; // earliest stage for draw pass
		case LE_RENDER_PASS_TYPE_COMPUTE:
            return vk::PipelineStageFlagBits::eComputeShader; // stage for compute pass

		default:
			assert( false ); // unreachable - we don't know what kind of stage we're in.
            return vk::PipelineStageFlagBits();
		}
	};

	frame.passes.reserve( numRenderPasses );

	for ( auto pass = ppPasses; pass != ppPasses + numRenderPasses; pass++ ) {

		LeRenderPass currentPass{};

		currentPass.type      = renderpass_i.get_type( *pass );
		currentPass.debugName = renderpass_i.get_debug_name( *pass );
		currentPass.width     = renderpass_i.get_width( *pass );
		currentPass.height    = renderpass_i.get_height( *pass );

		// Find explicit sync ops needed for resources which are not image
		// attachments.
		//
		{
			le_resource_handle_t const *resources       = nullptr;
			LeResourceUsageFlags const *resources_usage = nullptr;
			size_t                      resources_count = 0;
			renderpass_i.get_used_resources( *pass, &resources, &resources_usage, &resources_count );

			for ( size_t i = 0; i != resources_count; ++i ) {
				auto &resource = resources[ i ];
				auto &usage    = resources_usage[ i ];

				auto &syncChain = syncChainTable[ resource ];
				assert( !syncChain.empty() ); // must not be empty - this resource must exist, and have an initial sync state

				LeRenderPass::ExplicitSyncOp syncOp{};

				syncOp.resource_id               = resource;
				syncOp.active                    = true;
				syncOp.sync_chain_offset_initial = uint32_t( syncChain.size() - 1 );

				ResourceState requestedState{}; // State we want our image to be in when pass begins.

				// Define synchronisation requirements for each resource based on resource type,
				// and resource usage.
				//
				if ( usage.type == LeResourceType::eImage ) {

					if ( usage.typed_as.image_usage_flags & LE_IMAGE_USAGE_SAMPLED_BIT ) {

						requestedState.visible_access = vk::AccessFlagBits::eShaderRead;
						requestedState.write_stage = requestedState.write_stage = get_stage_flags_based_on_renderpass_type( currentPass.type );
						requestedState.layout                                   = vk::ImageLayout::eShaderReadOnlyOptimal;

					} else if ( usage.typed_as.image_usage_flags & LE_IMAGE_USAGE_STORAGE_BIT ) {

						requestedState.visible_access = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
						requestedState.write_stage    = get_stage_flags_based_on_renderpass_type( currentPass.type );
						requestedState.layout         = vk::ImageLayout::eGeneral;

					} else if ( usage.typed_as.image_usage_flags & LE_IMAGE_USAGE_TRANSFER_DST_BIT ) {
						// this is an image write operation.

						continue;

						// FIXME: implement - and make sure we're still compatible with the barriers inserted
						// when processing le::CommandType::eWriteToImage.
						//						requestedState.visible_access = vk::AccessFlagBits::eTransferWrite;
						//						requestedState.write_stage    = get_stage_flags_based_on_renderpass_type( currentPass.type );
						//						requestedState.layout         = vk::ImageLayout::eTransferDstOptimal;

					} else {
						continue;
					}

				} else {
					// Continue means nothing is added to sync chain.
					continue;
				}

				// -- we must add an entry to the sync chain to signal the state after change
				// -- we must add an explicit sync op so that the change happens before the pass

				// add target state to sync chain for image.
				syncChain.emplace_back( requestedState );

				syncOp.sync_chain_offset_final = uint32_t( syncChain.size() - 1 );

				// Store an explicit sync op
				currentPass.explicit_sync_ops.emplace_back( syncOp );
			}
		}

		// iterate over all image attachments

		le_image_attachment_info_t const *pImageAttachments   = nullptr;
		le_resource_handle_t const *      pResources          = nullptr;
		size_t                            numImageAttachments = 0;
		renderpass_i.get_image_attachments( *pass, &pImageAttachments, &pResources, &numImageAttachments );
		for ( size_t i = 0; i != numImageAttachments; ++i ) {

			auto const &image_resource_id     = pResources[ i ];
			auto const &image_attachment_info = pImageAttachments[ i ];

			auto &syncChain = syncChainTable[ pResources[ i ] ];

			vk::Format attachmentFormat = vk::Format( frame.availableResources[ image_resource_id ].info.imageInfo.format );

            bool isDepth = false, isStencil = false;
            vk_format_get_is_depth_stencil( attachmentFormat, isDepth, isStencil );
            bool isDepthStencil = isDepth || isStencil;

			AttachmentInfo *currentAttachment = ( currentPass.attachments + ( currentPass.numColorAttachments + currentPass.numDepthStencilAttachments ) );

			if ( isDepthStencil ) {
				currentPass.numDepthStencilAttachments++;
			} else {
				currentPass.numColorAttachments++;
			}

			currentAttachment->resource_id = image_resource_id;
			currentAttachment->format      = attachmentFormat;
			currentAttachment->numSamples  = le_sample_count_flag_bits_to_vk( image_attachment_info.numSamples );
			currentAttachment->loadOp      = le_attachment_load_op_to_vk( image_attachment_info.loadOp );
			currentAttachment->storeOp     = le_attachment_store_op_to_vk( image_attachment_info.storeOp );
			currentAttachment->clearValue  = le_clear_value_to_vk( image_attachment_info.clearValue );

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

				if ( image_attachment_info.loadOp == le::AttachmentLoadOp::eLoad ) {
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

	// ------------------------------------------------------
	// Check for barrier correctness
	//
	// Go through all frames and passes and make sure that any explicit sync ops do refer
	// to sync chain indices which are higher than the current sync chain id for a given resource.
	//
	// If they were lower, that would mean that an implicit sync has already taken care of this
	// image resource operation, in which case we want to deactivate the barrier, as it is not needed.
	//
	// Note that only image resources have a chance of being implicitly synced

	typedef std::unordered_map<le_resource_handle_t, uint32_t, LeResourceHandleIdentity> SyncChainMap;

	SyncChainMap max_sync_index;

	auto insert_if_greater = [&max_sync_index]( le_resource_handle_t const &handle, uint32_t index ) {
		auto found_it = max_sync_index.find( handle );
		if ( found_it == max_sync_index.end() ) {
			// not yet in map, we must insert the new value
			max_sync_index[ handle ] = index;
		} else if ( index > found_it->second ) {
			// new index is higher than value in map, replace map value with new index.
			found_it->second = index;
		}
	};

	for ( auto &p : frame.passes ) {

		// check barrier sync chain index against current sync index.
		//
		// if barrier sync index is higher, barrier must be issued. otherwise, barrier must be removed,
		// as subpass dependency takes care of barrier via implicit sync.

		for ( auto &op : p.explicit_sync_ops ) {

			if ( op.resource_id.meta.type != LeResourceType::eImage ) {
				continue;
			}

			// ---------| invariant: only image resources need checking
			//
			// This is because only image may potentially be synchronised implicitly via
			// subpass dependencies. No such mechanism exists for buffers.
			//
			// We can skip checks for buffer barriers, as we assume they are
			// all needed.

			auto found_it = max_sync_index.find( op.resource_id );
			if ( found_it != max_sync_index.end() && found_it->second >= op.sync_chain_offset_final ) {
				// found an element, and current index is already higher than barrier index.
				op.active = false;
			} else {
				// no element found, or max index is smaller.
				op.active = true;
				// store the current max index, then.
				max_sync_index[ op.resource_id ] = op.sync_chain_offset_final;
			}
		}

		// Update max_sync_index, so that it contains the maximum sync chain index for each
		// attachment image resource used in the current pass.
		//
		for ( size_t a = 0; a != p.numColorAttachments + p.numDepthStencilAttachments; a++ ) {
			auto const &attachmentInfo = p.attachments[ a ];
			insert_if_greater( attachmentInfo.resource_id, attachmentInfo.finalStateOffset );
		}
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

	using namespace le_backend_vk;

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
		le_allocator_linear_i.reset( alloc );
	}

	// -- reset frame-local staging allocator
	le_staging_allocator_i.reset( frame.stagingAllocator );

	// -- remove any texture references
	frame.textures.clear();

	// -- remove any image view references
	frame.imageViews.clear();

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

	// for each attachment, we want to keep track of its last used sync state
	// so that we may know whether to issue a barrier or not.

	for ( auto &pass : frame.passes ) {

		// The rest of this loop only concerns draw passes
		//
		if ( pass.type != LE_RENDER_PASS_TYPE_DRAW ) {
			continue;
		}

		// ---------| Invariant: current pass is a draw pass.

		std::vector<vk::AttachmentDescription> attachments;
		attachments.reserve( pass.numColorAttachments + pass.numDepthStencilAttachments );

		std::vector<vk::AttachmentReference>     colorAttachmentReferences;
        std::unique_ptr<vk::AttachmentReference> dsAttachmentReference;

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

		if ( PRINT_DEBUG_MESSAGES ) {

			std::cout << "* Renderpass: '" << pass.debugName << "'" << std::endl;

			std::cout << std::setw( 30 ) << "Attachment"
			          << " : " << std::setw( 30 ) << "Layout initial"
			          << " : " << std::setw( 30 ) << "Layout subpass"
			          << " : " << std::setw( 30 ) << "Layout final"
			          << std::endl;
		}

		auto const attachments_end = pass.attachments +
		                             ( pass.numColorAttachments + pass.numDepthStencilAttachments );

		for ( AttachmentInfo const *attachment = pass.attachments; attachment != attachments_end; attachment++ ) {

			auto &syncChain = syncChainTable.at( attachment->resource_id );

			const auto &syncInitial = syncChain.at( attachment->initialStateOffset );
			const auto &syncSubpass = syncChain.at( attachment->initialStateOffset + 1 );
			const auto &syncFinal   = syncChain.at( attachment->finalStateOffset );

            bool isDepth   = false;
            bool isStencil = false;
            vk_format_get_is_depth_stencil( attachment->format, isDepth, isStencil );

			vk::AttachmentDescription attachmentDescription{};
			attachmentDescription
			    .setFlags( vk::AttachmentDescriptionFlags() ) // relevant for compatibility
			    .setFormat( attachment->format )              // relevant for compatibility
			    .setSamples( attachment->numSamples )         // relevant for compatibility
			    .setLoadOp( attachment->loadOp )
			    .setStoreOp( attachment->storeOp )
                .setStencilLoadOp( isStencil ? attachment->loadOp : vk::AttachmentLoadOp::eDontCare )
                .setStencilStoreOp( isStencil ? attachment->storeOp : vk::AttachmentStoreOp::eDontCare )
			    .setInitialLayout( syncInitial.layout )
			    .setFinalLayout( syncFinal.layout );

			if ( PRINT_DEBUG_MESSAGES ) {

				std::cout << std::setw( 30 ) << attachment->resource_id.debug_name
				          << " : " << std::setw( 30 ) << vk::to_string( syncInitial.layout )
				          << " : " << std::setw( 30 ) << vk::to_string( syncSubpass.layout )
				          << " : " << std::setw( 30 ) << vk::to_string( syncFinal.layout )
				          << std::setw( 30 ) << "sync chain indices"
				          << " : " << std::setw( 4 ) << std::dec << attachment->initialStateOffset
				          << " : " << std::setw( 4 ) << std::dec << attachment->initialStateOffset + 1
				          << " : " << std::setw( 4 ) << std::dec << attachment->finalStateOffset
				          << std::endl;
			}

			attachments.emplace_back( attachmentDescription );

            if ( isDepth || isStencil ) {
                dsAttachmentReference = std::make_unique<vk::AttachmentReference>( attachments.size() - 1, syncSubpass.layout );
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
				// Ensure that the stage mask is valid if no src stage was specified.
				srcStageFromExternalFlags = vk::PipelineStageFlagBits::eTopOfPipe;
			}
		}

		if ( PRINT_DEBUG_MESSAGES ) {
			std::cout << std::endl
			          << std::flush;
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
            .setPDepthStencilAttachment( dsAttachmentReference.get() )
		    .setPreserveAttachmentCount( 0 )
		    .setPPreserveAttachments( nullptr );

		subpasses.emplace_back( subpassDescription );

		std::vector<vk::SubpassDependency> dependencies;
		dependencies.reserve( 2 );
		{
			if ( PRINT_DEBUG_MESSAGES && false ) {

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
	} // end for all passes
}

// ----------------------------------------------------------------------

/// \brief fetch vk::Buffer from frame local storage based on resource handle flags
/// - allocatorBuffers[index] if transient,
/// - stagingAllocator.buffers[index] if staging,
/// otherwise, fetch from frame available resources based on an id lookup.
static inline vk::Buffer frame_data_get_buffer_from_le_resource_id( const BackendFrameData &frame, const le_resource_handle_t &resource ) {

	assert( resource.meta.type == LeResourceType::eBuffer ); // resource type must be buffer

	if ( resource.meta.flags == le_resource_handle_t::FlagBits::eIsVirtual ) {
		return frame.allocatorBuffers[ resource.meta.index ];
	} else if ( resource.meta.flags == le_resource_handle_t::FlagBits::eIsStaging ) {
		return frame.stagingAllocator->buffers[ resource.meta.index ];
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
static inline VkFormat frame_data_get_image_format_from_texture_info( BackendFrameData const &frame, LeImageSamplerInfo const &texInfo ) {
	if ( texInfo.imageView.format == le::Format::eUndefined ) {
		return ( frame_data_get_image_format_from_resource_id( frame, texInfo.imageView.imageId ) );
	} else {
		return VkFormat( texInfo.imageView.format );
	}
}

// ----------------------------------------------------------------------

vk::ImageAspectFlags get_aspect_flags_from_format( vk::Format const &format ) {
    vk::ImageAspectFlags aspectFlags{};

    bool isDepth   = false;
    bool isStencil = false;
    vk_format_get_is_depth_stencil( format, isDepth, isStencil );

    if ( isDepth || isStencil ) {
        if ( isDepth ) {
            aspectFlags |= vk::ImageAspectFlagBits::eDepth;
        }
        if ( isStencil ) {
            aspectFlags |= vk::ImageAspectFlagBits::eStencil;
        }
    } else {
        aspectFlags |= vk::ImageAspectFlagBits::eColor;
    }

    return aspectFlags;
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

			vk::ImageSubresourceRange subresourceRange;
			subresourceRange
                .setAspectMask( get_aspect_flags_from_format( attachment->format ) )
			    .setBaseMipLevel( 0 )
			    .setLevelCount( 1 )
			    .setBaseArrayLayer( 0 )
			    .setLayerCount( 1 );

			vk::ImageViewCreateInfo imageViewCreateInfo;
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
	// descriptorPools.

	// at this point it would be nice to have an idea for each renderpass
	// on how many descriptors to expect, but we cannot know that realistically
	// without going through the command buffer... yuck.

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
// Returns a VkFormat which will match a given set of LeImageUsageFlags.
// If a matching format cannot be inferred, this method
le::Format infer_image_format_from_le_image_usage_flags( le_backend_o *self, LeImageUsageFlags flags ) {
	le::Format format{};
	if ( flags & ( LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) ) {
		// set to default color format
		format = self->defaultFormatColorAttachment;
	} else if ( flags & LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
		// set to default depth stencil format
		format = self->defaultFormatDepthStencilAttachment;
	} else if ( flags & LE_IMAGE_USAGE_SAMPLED_BIT ) {
		format = self->defaultFormatSampledImage;
	} else {
		// we don't know what to do because we can't infer the intended use of this resource.
		format = le::Format::eUndefined;
	}
	return format;
}

// ----------------------------------------------------------------------
// Allocates and creates a physical vulkan resource using vmaAlloc given an allocator
// Returns an AllocatedResourceVk, currently does not do any error checking.
static inline AllocatedResourceVk allocate_resource_vk( const VmaAllocator &alloc, const ResourceCreateInfo &resourceInfo ) {
	AllocatedResourceVk     res{};
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

// ----------------------------------------------------------------------

// Creates a new staging allocator
// Typically, there is one staging allocator associated to each frame.
static le_staging_allocator_o *staging_allocator_create( VmaAllocator const vmaAlloc, VkDevice const device ) {
	auto self       = new le_staging_allocator_o{};
	self->allocator = vmaAlloc;
	self->device    = device;
	return self;
}

// ----------------------------------------------------------------------

// Allocates a chunk of memory from the vulkan free store via vmaAlloc, and maps it
// for writing at *pData.
//
// If successful, `resource_handle` receives a valid `le_resource_handle` referring to
// this particular chunk of staging memory.
//
// Returns false on error, true on success.
//
// Staging memory is only allowed to be used for staging, that is, only
// TRANSFER_SRC are set for usage flags.
//
// Staging memory is typically cache coherent, ie. does not need to be flushed.
static bool staging_allocator_map( le_staging_allocator_o *self, uint64_t numBytes, void **pData, le_resource_handle_t *resource_handle ) {

	VmaAllocation     allocation; // handle to allocation
	VkBuffer          buffer;     // handle to buffer (returned from vmaMemAlloc)
	VmaAllocationInfo allocationInfo;

	VkBufferCreateInfo bufferCreateInfo = vk::BufferCreateInfo()
	                                          .setSize( numBytes )
	                                          .setSharingMode( vk::SharingMode::eExclusive )
	                                          .setUsage( vk::BufferUsageFlagBits::eTransferSrc );

	VmaAllocationCreateInfo allocationCreateInfo{};
	allocationCreateInfo.flags          = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocationCreateInfo.usage          = VMA_MEMORY_USAGE_CPU_ONLY;
	allocationCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	auto result = vmaCreateBuffer( self->allocator,
	                               &bufferCreateInfo,
	                               &allocationCreateInfo,
	                               &buffer,
	                               &allocation,
	                               &allocationInfo );

	assert( result == VK_SUCCESS );

	if ( result != VK_SUCCESS ) {
		return false;
	}

	{
		// -- Now store our allocation in the allocations vectors.
		//
		// We need to lock the mutex as we are updating all vectors
		// and this might lead to re-allocations.
		//
		// Other encoders might also want to map memory, and
		// they will have to wait for whichever operation in
		// process to finish.
		auto lock = std::scoped_lock( self->mtx );

		size_t allocationIndex = self->allocations.size();

		self->allocations.push_back( allocation );
		self->allocationInfo.push_back( allocationInfo );
		self->buffers.push_back( buffer );

		// Virtual resources all share the same id,
		// but their meta data is different.
		auto resource = LE_BUF_RESOURCE( "Le-Staging-Buffer" );

		// We store the allocation index in the resource handle meta data
		// so that the correct buffer for this handle can be retrieved later.
		resource.meta.index = uint16_t( allocationIndex );
		resource.meta.flags = le_resource_handle_t::FlagBits::eIsStaging;

		// Store the handle for this resource so that the caller
		// may receive it.
		*resource_handle = resource;
	}

	// Map memory so that it may be written to
	vmaMapMemory( self->allocator, allocation, pData );

	return true;
};

// ----------------------------------------------------------------------

/// Frees all allocations held by the staging allocator given in `self`
static void staging_allocator_reset( le_staging_allocator_o *self ) {
	auto lock   = std::scoped_lock( self->mtx );
	auto device = vk::Device{self->device};

	// destroy all buffers
	for ( auto &b : self->buffers ) {
		device.destroyBuffer( b );
	}
	self->buffers.clear();

	// free allocations

	for ( auto &a : self->allocations ) {
		vmaFreeMemory( self->allocator, a );
	}
	self->allocations.clear();

	// clear allocation infos.
	self->allocationInfo.clear();
}

// ----------------------------------------------------------------------

// Destroys a staging allocator (and implicitly all of its derived objects)
static void staging_allocator_destroy( le_staging_allocator_o *self ) {

	// Reset the object first so that dependent objects (vmaAllocations, vulkan objects) are cleaned up.
	staging_allocator_reset( self );

	delete self;
}

// ----------------------------------------------------------------------

// Frees any resources which are marked for being recycled in the current frame.
inline void frame_release_binned_resources( BackendFrameData &frame, vk::Device device, VmaAllocator &allocator ) {
	for ( auto &a : frame.binnedResources ) {
		if ( a.second.info.isBuffer() ) {
			device.destroyBuffer( a.second.asBuffer );
		} else {
			device.destroyImage( a.second.asImage );
		}
		vmaFreeMemory( allocator, a.second.allocation );
	}
	frame.binnedResources.clear();
}

// ----------------------------------------------------------------------
// Allocates all physical Vulkan memory resources (Images/Buffers) referenced to by the frame.
//
// - If a resource is already available to the backend, the previously allocated resource is
//   copied into the frame.
// - If a resource has not yet been seen, it is freshly allocated, then made available to
//   the frame. It is also copied to the backend, so that the following frames may access it.
// - If a resource is requested with properties differing from a resource with the same handle
//   available from the backend, the previous resource is placed in the frame bin for recycling,
//   and a new resource is allocated and copied to the frame. This resource in the backend is
//   replaced by the new version, too. (Effectively, the frame has taken ownership of the old
//   version and keeps it until it disposes of it).
// - If there are resources in the recycling bin of a frame, these will get freed. Freeing
//   happens as a first step, so that resources are only freed once the frame has "come around"
//   and earlier frames which may have still used the old version of the resource have no claim
//   on the old version of the resource anymore.
//
// We are currently not checking for "orphaned" resources (resources which are available in the
// backend, but not used by the frame) - these could possibly be recycled, too.

static void backend_allocate_resources( le_backend_o *self, BackendFrameData &frame, le_renderpass_o **passes, size_t numRenderPasses ) {

	/*
	- Frame is only ever allowed to reference frame-local resources.
	- "Acquire" therefore means we create local copies of backend-wide resource handles.
	*/

	// -- first it is our holy duty to drop any binned resources which were condemned the last time this frame was active.
	// It's possible that this was more than two frames ago, depending on how many swapchain images there are.

	frame_release_binned_resources( frame, self->device->getVkDevice(), self->mAllocator );

	using namespace le_renderer;

	std::vector<le_resource_handle_t>            usedResources;      // (
	std::vector<std::vector<le_resource_info_t>> usedResourcesInfos; // ( usedResourceInfos[index] contains vector of usages for usedResource[index]

	// Iterate over all resource declarations in all passes so that we can collect all resources,
	// and their usage information. Later, we will consolidate their usages so that resources can
	// be re-used across passes.
	//
	// Note that we accumulate all resource infos first, and do consolidation
	// in a separate step. That way, we can first make sure all flags are combined,
	// before we make sure to we find a valid image format which matches all uses...

	assert( frame.swapchainWidth == self->swapchainWidth );
	assert( frame.swapchainHeight == self->swapchainHeight );

	for ( le_renderpass_o **rp = passes; rp != passes + numRenderPasses; rp++ ) {

		auto pass_width  = renderpass_i.get_width( *rp );
		auto pass_height = renderpass_i.get_height( *rp );

		{
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
		}

		le_resource_handle_t const *p_resources             = nullptr;
		LeResourceUsageFlags const *p_resources_usage_flags = nullptr;
		size_t                      resources_count         = 0;

		renderpass_i.get_used_resources( *rp, &p_resources, &p_resources_usage_flags, &resources_count );

		for ( size_t i = 0; i != resources_count; ++i ) {

			le_resource_handle_t const &resource             = p_resources[ i ];             // Resource handle
			LeResourceUsageFlags const &resource_usage_flags = p_resources_usage_flags[ i ]; // Resource usage flags

			assert( resource_usage_flags.type == resource.meta.type ); // Resource Usage Flags must be for matching resource type.

			// Test whether a resource with this id is already in usedResources -
			// if not, resource_index will be identical to usedResource vector size,
			// which is useful, because as soon as we add an element to the vector
			// resource_index will index the correct element.

			auto resource_index = static_cast<size_t>( std::find( usedResources.begin(), usedResources.end(), resource ) - usedResources.begin() );

			if ( resource_index == usedResources.size() ) {

				// Resource not found - we must insert a resource, and an empty vector, to fullfil the invariant
				// that resource_index points at the correct elements

				// Check if resource was declared explicitly via module - if yes,
				// insert resource info from there - otherwise insert an
				// empty entry to indicate that for this resource there are no previous
				// resource infos.

				// We only want to add resources which are actually used in the frame to
				// used_resources, which is why we keep declared resources separate, and
				// only copy their resource info as needed.

				size_t found_resource_index = 0;
				// search for resource id in vector of declared resources.
				for ( auto const &id : frame.declared_resources_id ) {
					if ( id == resource ) {
						// resource found, let's use this declared_resource_index.
						break;
					}
					found_resource_index++;
				}

				if ( found_resource_index == frame.declared_resources_id.size() ) {
					// Nothing found. Insert empty entry
					usedResources.push_back( resource );
					usedResourcesInfos.push_back( {} );
				} else {
					// Explicitly declared resource found. Insert declaration info.
					usedResources.push_back( frame.declared_resources_id[ found_resource_index ] );
					usedResourcesInfos.push_back( {frame.declared_resources_info[ found_resource_index ]} );
				}
			}

			// We must ensure that images which are used as Color, or DepthStencil attachments
			// fit the extents of their renderpass - as this is a Vulkan requirement.
			//
			// We do this here, because we know the extents of the renderpass.
			//
			// We also need to ensure that the extent has 1 as depth value by default.

			le_resource_info_t resourceInfo = {}; // empty resource info
			resourceInfo.type               = resource_usage_flags.type;

			if ( resourceInfo.type == LeResourceType::eImage ) {

				resourceInfo.image.usage = resource_usage_flags.typed_as.image_usage_flags;

				auto &imgInfo   = resourceInfo.image;
				auto &imgExtent = imgInfo.extent;

				if ( imgInfo.usage & ( LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) ) {

					imgInfo.mipLevels   = 1;
					imgInfo.samples     = le::SampleCountFlagBits::e1;
					imgInfo.imageType   = le::ImageType::e2D;
					imgInfo.tiling      = le::ImageTiling::eOptimal;
					imgInfo.arrayLayers = 1;

					imgExtent.width  = pass_width;
					imgExtent.height = pass_height;
				}

				// depth must be at least 1, but may arrive zero-initialised.
				imgExtent.depth = std::max<uint32_t>( imgExtent.depth, 1 );

			} else if ( resourceInfo.type == LeResourceType::eBuffer ) {
				resourceInfo.buffer.usage = resource_usage_flags.typed_as.buffer_usage_flags;
			} else {
				assert( false ); // unreachable
			}

			usedResourcesInfos[ resource_index ].emplace_back( resourceInfo );

		} // end for all resources

	} // end for all passes

	assert( usedResources.size() == usedResourcesInfos.size() );

	// Consolidate usedResourcesInfos so that the first element in the vector of
	// resourceInfos for a resource covers all intended usages of a resource.

	// TODO: if Resource usage changes between passes, (e.g. write-to image, sample-from image)
	// we must somehow annotate that the image has changed.
	// This is complicated somehow through the fact that an image
	// may not actually be written to, as the execute stage is what counts for access to resources.
	//
	// There needs to be a pipeline barrier so that resources are transitioned
	// from their previous usage to their next usage.

	size_t resource_index = 0;
	for ( auto &resourceInfoVersions : usedResourcesInfos ) {

		if ( resourceInfoVersions.empty() )
			continue;

		// ---------| invariant: there is at least a first element.

		le_resource_info_t *const       first_info = resourceInfoVersions.data();
		le_resource_info_t const *const info_end   = first_info + resourceInfoVersions.size();

		switch ( first_info->type ) {
		case LeResourceType::eBuffer: {
			// Consolidate into first_info, beginning with the second element
			for ( auto *info = first_info + 1; info != info_end; info++ ) {
				first_info->buffer.usage |= info->buffer.usage;

				// Make sure buffer can hold maximum of requested number of bytes.
				if ( info->buffer.size != 0 && info->buffer.size > first_info->buffer.size ) {
					first_info->buffer.size = info->buffer.size;
				}
			}

			// Now, we must make sure that the buffer info contains sane values.

			// TODO: emit an error message and emit sane defaults if values fail this test.
			assert( first_info->buffer.usage != 0 );
			assert( first_info->buffer.size != 0 );

		} break;
		case LeResourceType::eImage: {

			// Consolidate into first_info, beginning with the second element
			for ( auto *info = first_info + 1; info != info_end; info++ ) {

				first_info->image.flags |= info->image.flags;
				first_info->image.usage |= info->image.usage;

				// If an image format was explictly set, this takes precedence over eUndefined.
				// Note that we skip this block if both infos have the same format, so if both
				// infos are eUndefined, format stays undefined.

				if ( info->image.format != le::Format::eUndefined && info->image.format != first_info->image.format ) {

					// ----------| invariant: both formats differ, and second format is not undefined

					if ( first_info->image.format == le::Format::eUndefined ) {
						first_info->image.format = info->image.format;
					} else {
						// Houston, we have a problem!
						// Two different formats were explicitly specified for this image.
						assert( false );
					}
				}

				// Make sure the image is as large as it needs to be

				if ( info->image.extent.width != 0 && info->image.extent.width > first_info->image.extent.width ) {
					first_info->image.extent.width = info->image.extent.width;
				}

				if ( info->image.extent.height != 0 && info->image.extent.height > first_info->image.extent.height ) {
					first_info->image.extent.height = info->image.extent.height;
				}

				if ( info->image.extent.depth != 0 && info->image.extent.depth > first_info->image.extent.depth ) {
					first_info->image.extent.depth = info->image.extent.depth;
				}
			}

			// Do a final sanity check to make sure all required fields are valid.

			assert( first_info->image.extent.depth != 0 ); // zero depth is illegal.
			assert( first_info->image.usage != 0 );        // some kind of usage must be specified.

		} break;
		default:
            break;
		}
		resource_index++;
	}

	// Check if all resources declared in this frame are already available in backend.
	// If a resource is not available yet, this resource must be allocated.

	auto printResourceInfo = []( le_resource_handle_t const &handle, ResourceCreateInfo const &info ) {
		std::cout << std::setw( 16 ) << handle.debug_name;
		if ( info.isBuffer() ) {
			std::cout
			    << " : " << std::setw( 4 ) << ( info.bufferInfo.size )
			    << " : " << std::setw( 30 ) << to_string( vk::BufferUsageFlags( info.bufferInfo.usage ) )
			    << std::endl;
		} else {
			std::cout
			    << " : " << std::dec << std::setw( 4 ) << info.imageInfo.extent.width << " x " << std::setw( 4 ) << info.imageInfo.extent.height
			    << " : " << std::setw( 30 ) << to_string( vk::Format( info.imageInfo.format ) )
			    << " : " << std::setw( 30 ) << to_string( vk::ImageUsageFlags( info.imageInfo.usage ) )
			    << std::endl;
		}
		std::cout << std::flush;
	};

	auto &backendResources = self->only_backend_allocate_resources_may_access.allocatedResources;

	auto checkImageFormat = []( le_backend_o *self, le_resource_handle_t const &resource, ResourceCreateInfo *createInfo, LeImageUsageFlags const &usageFlags ) -> bool {
		// If image format was not specified, we must try to
		// infer the image format from usage flags.
		auto inferred_format = infer_image_format_from_le_image_usage_flags( self, usageFlags );

		if ( inferred_format == le::Format::eUndefined ) {
			std::cerr << "FATAL: Cannot infer image format, resource underspecified: '" << resource.debug_name << "'" << std::endl
			          << "Specify usage, or provide explicit format option for resource to fix this error. " << std::endl
			          << "Consider using le::RenderModule::declareResource()" << std::endl
			          << std::flush;

			assert( false ); // we don't have enough information to infer image format.
			return false;
		} else {
			createInfo->imageInfo.format = VkFormat( le_format_to_vk( inferred_format ) );
		}

		return true;
	};

	// If image has mip levels, we implicitly add usage transfer src, so that mip maps may be created by blitting.
	auto checkImageUsageForMipLevels = []( ResourceCreateInfo *createInfo ) {
		if ( createInfo->imageInfo.mipLevels > 1 ) {
			createInfo->imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
	};

	const size_t usedResourcesSize = usedResources.size();
	for ( size_t i = 0; i != usedResourcesSize; ++i ) {

		le_resource_handle_t const &resourceId   = usedResources[ i ];
		le_resource_info_t const &  resourceInfo = usedResourcesInfos[ i ][ 0 ];

		// See if a resource with this id is already available to the backend.

		auto       resourceCreateInfo = ResourceCreateInfo::from_le_resource_info( resourceInfo, &self->queueFamilyIndexGraphics, 0 );
		auto       foundIt            = backendResources.find( resourceId );
		const bool resourceIdNotFound = ( foundIt == backendResources.end() );

		if ( resourceIdNotFound ) {

			// Resource does not yet exist, we must allocate this resource and add it to the backend.
			// Then add a reference to it to the current frame.

			if ( resourceCreateInfo.isImage() ) {

				checkImageUsageForMipLevels( &resourceCreateInfo );

				if ( resourceCreateInfo.imageInfo.format == VK_FORMAT_UNDEFINED ) {
					checkImageFormat( self, resourceId, &resourceCreateInfo, resourceInfo.image.usage );
				}
			}

			if ( PRINT_DEBUG_MESSAGES || true ) {
				std::cout << "Allocating resource: ";
				printResourceInfo( resourceId, resourceCreateInfo );
			}

			auto allocatedResource = allocate_resource_vk( self->mAllocator, resourceCreateInfo );

			// Add resource to map of available resources for this frame
			frame.availableResources.insert( {resourceId, allocatedResource} );

			// Add this newly allocated resource to the backend so that the following frames
			// may use it, too
			backendResources.insert_or_assign( resourceId, allocatedResource );

		} else {

			// If an existing resource has been found, we must check that it
			// was allocated with the same properties as the resource we require

			auto &foundResourceCreateInfo = foundIt->second.info;

			// Note that we use the greater-than operator, which means
			// that if our foundResource is equal to *or a superset of*
			// resourceCreateInfo, we can re-use the found resource.
			//
			if ( foundResourceCreateInfo >= resourceCreateInfo ) {

				// -- found info is either equal or a superset

				// Add a copy of this resource allocation to the current frame.
				frame.availableResources.emplace( resourceId, foundIt->second );

			} else {

				// -- info does not match.

				// We must re-allocate this resource, and add the old version of the resource to the recycling bin.

				// -- allocate a new resource

				if ( resourceCreateInfo.isImage() ) {
					checkImageUsageForMipLevels( &resourceCreateInfo );
					if ( resourceCreateInfo.imageInfo.format == VK_FORMAT_UNDEFINED ) {
						checkImageFormat( self, resourceId, &resourceCreateInfo, resourceInfo.image.usage );
					}
				}

				if ( PRINT_DEBUG_MESSAGES || true ) {
					std::cout << "Re-allocating resource: ";
					printResourceInfo( resourceId, resourceCreateInfo );
				}

				auto allocatedResource = allocate_resource_vk( self->mAllocator, resourceCreateInfo );

				// Add a copy of old resource to recycling bin for this frame, so that
				// these resources get freed when this frame comes round again.
				//
				// We don't immediately delete the resources, as in-flight (preceding) frames
				// might still be using them.
				frame.binnedResources.try_emplace( resourceId, foundIt->second );

				// add the new version of the resource to frame available resources
				frame.availableResources.insert( {resourceId, allocatedResource} );

				// Remove old version of resource from backend, and
				// add new version of resource to backend
				backendResources.insert_or_assign( resourceId, allocatedResource );
			}
		}
	} // end for all used resources

	// If we locked backendResources with a mutex, this would be the right place to release it.

	if ( PRINT_DEBUG_MESSAGES ) {
		std::cout << "Available Resources: " << std::endl
		          << std::setw( 10 ) << "Type"
		          << " : " << std::setw( 30 ) << "debugName"
		          << " : " << std::setw( 30 ) << "Vk Handle : " << std::endl;
		for ( auto const &r : frame.availableResources ) {
			if ( r.second.info.isBuffer() ) {
				std::cout << std::setw( 10 ) << "Buffer"
				          << " : " << std::setw( 30 ) << r.first.debug_name
				          << " : " << std::setw( 30 ) << r.second.asBuffer << std::endl;
			} else {
				std::cout << std::setw( 10 ) << "Image"
				          << " : " << std::setw( 30 ) << r.first.debug_name
				          << " : " << std::setw( 30 ) << r.second.asImage << std::endl;
			}
		}
		std::cout << std::flush;
	}

	// We capture the check for extension as a static, as this is not expected to
	// change for the lifetime of the application, and checking for the extension
	// on each frame is wasteful.
	//
	static bool check_utils_extension_available = self->instance->isExtensionAvailable( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );

	if ( DEBUG_TAG_RESOURCES && check_utils_extension_available ) {
		for ( auto const &r : frame.availableResources ) {

			auto device = vk::Device( self->device->getVkDevice() );

			vk::DebugUtilsObjectNameInfoEXT nameInfo;
			nameInfo
			    .setObjectType( r.first.meta.type == LeResourceType::eImage ? vk::ObjectType::eImage : vk::ObjectType::eBuffer )
			    .setObjectHandle( reinterpret_cast<uint64_t>( r.second.asImage ) )
			    .setPObjectName( r.first.debug_name );

			device.setDebugUtilsObjectNameEXT( &nameInfo );
		}
	}
}

// Allocates Samplers and Textures requested by individual passes
// these are tied to the lifetime of the frame, and will be re-created
static void frame_allocate_per_pass_resources( BackendFrameData &frame, vk::Device const &device, le_renderpass_o **passes, size_t numRenderPasses ) {

	using namespace le_renderer;

	// Create imageviews for all available resources which are of type image
	// and which have usage sampled or storage.
	//
	for ( auto p = passes; p != passes + numRenderPasses; p++ ) {

		const le_resource_handle_t *resources      = nullptr;
		const LeResourceUsageFlags *resource_usage = nullptr;
		size_t                      resource_count = 0;

		renderpass_i.get_used_resources( *p, &resources, &resource_usage, &resource_count );

		for ( size_t i = 0; i != resource_count; ++i ) {
			auto const &r             = resources[ i ];
			auto const &r_usage_flags = resource_usage[ i ];

			if ( r_usage_flags.type == LeResourceType::eImage &&
			     ( r_usage_flags.typed_as.image_usage_flags & ( LE_IMAGE_USAGE_SAMPLED_BIT | LE_IMAGE_USAGE_STORAGE_BIT ) ) ) {

				// We create a default image view for this image and store it with the frame. If no explicit image view
				// for a particular operation has been specified, this default image view is used.

				if ( frame.imageViews.find( r ) != frame.imageViews.end() ) {
					continue;
				}

				// ---------| Invariant: ImageView for this image not yet stored with frame.

				// attempt to look up format via available resources - this is important for
				// unspecified formats which get automatically inferred, in which case we want
				// to set the format to whatever was inferred when the image was allocated and placed
				// in available resources.
				vk::Format imageFormat = vk::Format( frame_data_get_image_format_from_resource_id( frame, r ) );

				// If the format is still undefined at this point, we can only throw out hands up in the air...
				//
				if ( imageFormat == vk::Format::eUndefined ) {
					std::cout << "WARNING: Cannot create default view for image '" << r.debug_name << "', as format is undefined" << std::endl
					          << std::flush;
					continue;
				}

				vk::ImageSubresourceRange subresourceRange;
				subresourceRange
                    .setAspectMask( get_aspect_flags_from_format( imageFormat ) )
				    .setBaseMipLevel( 0 )
				    .setLevelCount( VK_REMAINING_MIP_LEVELS ) // we set VK_REMAINING_MIP_LEVELS which activates all mip levels remaining.
				    .setBaseArrayLayer( 0 )
				    .setLayerCount( 1 );

				vk::ImageViewCreateInfo imageViewCreateInfo{};
				imageViewCreateInfo
				    .setFlags( {} )
				    .setImage( frame_data_get_image_from_le_resource_id( frame, r ) )
				    .setViewType( vk::ImageViewType::e2D )
				    .setFormat( imageFormat )
				    .setComponents( {} ) // default component mapping
				    .setSubresourceRange( subresourceRange );

				auto imageView = device.createImageView( imageViewCreateInfo );

				// Store image view object with frame, indexed by image resource id,
				// so that it can be found quickly if need be.
				frame.imageViews[ r ] = imageView;

				AbstractPhysicalResource imgView{};
				imgView.type        = AbstractPhysicalResource::Type::eImageView;
				imgView.asImageView = imageView;

				frame.ownedResources.emplace_front( std::move( imgView ) );
			}
		}
	}

	// Create Samplers for all images which are used as Textures
	//
	for ( auto p = passes; p != passes + numRenderPasses; p++ ) {

		// Get all texture names for this pass
		const le_resource_handle_t *textureIds     = nullptr;
		size_t                      textureIdCount = 0;
		renderpass_i.get_texture_ids( *p, &textureIds, &textureIdCount );

		const LeImageSamplerInfo *textureInfos     = nullptr;
		size_t                    textureInfoCount = 0;
		renderpass_i.get_texture_infos( *p, &textureInfos, &textureInfoCount );

		assert( textureIdCount == textureInfoCount ); // texture info and -id count must be identical, as there is a 1:1 relationship

		for ( size_t i = 0; i != textureIdCount; i++ ) {

			// -- find out if texture with this name has already been alloacted.
			// -- if not, allocate

			const le_resource_handle_t textureId = textureIds[ i ];

			if ( frame.textures.find( textureId ) == frame.textures.end() ) {
				// -- we need to allocate a new texture

				auto &texInfo = textureInfos[ i ];

				vk::ImageView imageView{};
				{
					// Set or create vkImageview

					auto imageFormat = vk::Format( frame_data_get_image_format_from_texture_info( frame, texInfo ) );

					vk::ImageSubresourceRange subresourceRange;
					subresourceRange
                        .setAspectMask( get_aspect_flags_from_format( imageFormat ) )
					    .setBaseMipLevel( 0 )
					    .setLevelCount( VK_REMAINING_MIP_LEVELS ) // we set VK_REMAINING_MIP_LEVELS which activates all mip levels remaining.
					    .setBaseArrayLayer( 0 )
					    .setLayerCount( 1 );

					// TODO: fill in additional image view create info based on info from pass...

					vk::ImageViewCreateInfo imageViewCreateInfo{};
					imageViewCreateInfo
					    .setFlags( {} )
					    .setImage( frame_data_get_image_from_le_resource_id( frame, texInfo.imageView.imageId ) )
					    .setViewType( vk::ImageViewType::e2D )
					    .setFormat( imageFormat )
					    .setComponents( {} ) // default component mapping
					    .setSubresourceRange( subresourceRange );

					imageView = device.createImageView( imageViewCreateInfo );

					// Store vk object references with frame-owned resources, so that
					// the vk objects can be destroyed when frame crosses the fence.

					AbstractPhysicalResource res;
					res.asImageView = imageView;
					res.type        = AbstractPhysicalResource::Type::eImageView;

					frame.ownedResources.emplace_front( std::move( res ) );
				}

				vk::Sampler sampler{};
				{
					// Create VkSampler object on device.

					vk::SamplerCreateInfo samplerCreateInfo{};
					samplerCreateInfo
					    .setFlags( {} )
					    .setMagFilter( le_filter_to_vk( texInfo.sampler.magFilter ) )
					    .setMinFilter( le_filter_to_vk( texInfo.sampler.minFilter ) )
					    .setMipmapMode( le_sampler_mipmap_mode_to_vk( texInfo.sampler.mipmapMode ) )
					    .setAddressModeU( le_sampler_address_mode_to_vk( texInfo.sampler.addressModeU ) )
					    .setAddressModeV( le_sampler_address_mode_to_vk( texInfo.sampler.addressModeV ) )
					    .setAddressModeW( le_sampler_address_mode_to_vk( texInfo.sampler.addressModeW ) )
					    .setMipLodBias( texInfo.sampler.mipLodBias )
					    .setAnisotropyEnable( texInfo.sampler.anisotropyEnable )
					    .setMaxAnisotropy( texInfo.sampler.maxAnisotropy )
					    .setCompareEnable( texInfo.sampler.compareEnable )
					    .setCompareOp( le_compare_op_to_vk( texInfo.sampler.compareOp ) )
					    .setMinLod( texInfo.sampler.minLod )
					    .setMaxLod( texInfo.sampler.maxLod )
					    .setBorderColor( le_border_color_to_vk( texInfo.sampler.borderColor ) )
					    .setUnnormalizedCoordinates( texInfo.sampler.unnormalizedCoordinates );

					sampler = device.createSampler( samplerCreateInfo );

					// Now store vk object references with frame-owned resources, so that
					// the vk objects can be destroyed when frame crosses the fence.

					AbstractPhysicalResource res;

					res.asSampler = sampler;
					res.type      = AbstractPhysicalResource::Type::eSampler;
					frame.ownedResources.emplace_front( std::move( res ) );
				}

				// -- Store Texture with frame so that decoder can find references
				BackendFrameData::Texture tex;
				tex.imageView = imageView;
				tex.sampler   = sampler;

				frame.textures[ textureId ] = tex;
			}
		} // end for all textureIds
	}     // end for all passes
}

// ----------------------------------------------------------------------
// This is one of the most important methods of backend -
// where we associate virtual with physical resources, allocate physical
// resources as needed, and keep track of sync state of physical resources.
static bool backend_acquire_physical_resources( le_backend_o *              self,
                                                size_t                      frameIndex,
                                                le_renderpass_o **          passes,
                                                size_t                      numRenderPasses,
                                                le_resource_handle_t const *declared_resources,
                                                le_resource_info_t const *  declared_resources_infos,
                                                size_t const &              declared_resources_count ) {

	auto &frame = self->mFrames[ frameIndex ];

	{
		// Acquire swapchain image

		using namespace le_swapchain_vk;

		if ( !swapchain_i.acquire_next_image( self->swapchain, frame.semaphorePresentComplete, frame.swapchainImageIndex ) ) {
			return false;
		}

		// ----------| invariant: swapchain acquisition successful.

		frame.swapchainWidth  = swapchain_i.get_image_width( self->swapchain );
		frame.swapchainHeight = swapchain_i.get_image_height( self->swapchain );

		frame.availableResources[ self->swapchainImageHandle ].asImage = swapchain_i.get_image( self->swapchain, frame.swapchainImageIndex );
		{
			auto &backbufferInfo  = frame.availableResources[ self->swapchainImageHandle ].info.imageInfo;
			backbufferInfo        = vk::ImageCreateInfo{};
			backbufferInfo.extent = vk::Extent3D( frame.swapchainWidth, frame.swapchainHeight, 1 );
			backbufferInfo.format = VkFormat( self->swapchainImageFormat );
		}
	}

	{
		// Setup declared resources per frame - These are resources declared using resource infos
		// which are explicitly declared by user via the rendermodule, but which may or may not be
		// actually used in the frame.

		frame.declared_resources_id   = {declared_resources, declared_resources + declared_resources_count};
		frame.declared_resources_info = {declared_resources_infos, declared_resources_infos + declared_resources_count};
	}

	// Note that at this point memory for scratch buffers for each pass
	// in this frame has already been allocated, as this happens shortly before executeGraph.

	backend_allocate_resources( self, frame, passes, numRenderPasses );

	// Initialise sync chain table - each resource receives initial state
	// from current entry in frame.availableResources resource map.
	frame.syncChainTable.clear();
	for ( auto const &res : frame.availableResources ) {
		frame.syncChainTable.insert( {res.first, {res.second.state}} );
	}

	// -- build sync chain for each resource, create explicit sync barrier requests for resources
	// which cannot be impliciltly synced.
	frame_track_resource_state( frame, passes, numRenderPasses, self->swapchainImageHandle );

	// At this point we know the state for each resource at the end of the sync chain.
	// this state will be the initial state for the resource

	{
		// Update final sync state for each pre-existing backend resource.
		auto &backendResources = self->only_backend_allocate_resources_may_access.allocatedResources;
		for ( auto const &tbl : frame.syncChainTable ) {
			auto &resId       = tbl.first;
			auto &resSyncList = tbl.second;

			assert( !resSyncList.empty() ); // sync list must have entries

			// find element with matching resource ID in list of backend resources

			auto res = backendResources.find( resId );
			if ( res != backendResources.end() ) {
				// element found.
				// Set sync state for this resource to value of last elment in the sync chain.
				res->second.state = resSyncList.back();
			} else {
				assert( false ); // frame local resource must be available as a backend resource, otherwise something fishy is going on.
			}
		}

		// If we use a mutex to protect backend-wide resources, we can release it now.
	}

	vk::Device device = self->device->getVkDevice();

	// -- allocate any transient vk objects such as image samplers, and image views
	frame_allocate_per_pass_resources( frame, device, passes, numRenderPasses );

	// create renderpasses - use sync chain to apply implicit syncing for image attachment resources
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
			    .setSize( LE_LINEAR_ALLOCATOR_SIZE )
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

static le_staging_allocator_o *backend_get_staging_allocator( le_backend_o *self, size_t frameIndex ) {
	return self->mFrames[ frameIndex ].stagingAllocator;
}

// ----------------------------------------------------------------------
// Decode commandStream for each pass (may happen in parallel)
// translate into vk specific commands.
static void backend_process_frame( le_backend_o *self, size_t frameIndex ) {

	if ( PRINT_DEBUG_MESSAGES ) {
		std::cout << "** Process Frame #" << std::dec << std::setw( 8 ) << frameIndex << " **" << std::endl
		          << std::flush;
	}

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

		{

			if ( PRINT_DEBUG_MESSAGES ) {
				std::cout << "Renderpass: '" << pass.debugName << "'" << std::endl
				          << std::flush;
			}

			// -- Issue sync barriers for all resources which require explicit sync.
			//
			// We must to this here, as the spec requires barriers to happen
			// before renderpass begin.
			//
			for ( auto const &op : pass.explicit_sync_ops ) {
				// fill in sync op

				if ( op.active == false ) {
					continue;
				}

				// ---------| invariant: barrier is active.

				auto const &syncChain = frame.syncChainTable[ op.resource_id ];

				auto const &stateInitial = syncChain[ op.sync_chain_offset_initial ];
				auto const &stateFinal   = syncChain[ op.sync_chain_offset_final ];

				if ( stateInitial != stateFinal ) {
					// we must issue an image barrier

					if ( PRINT_DEBUG_MESSAGES ) {

						//
						// --------| invariant: barrier is active.

						// print out sync chain for sampled image
						std::cout << "\t Explicit Barrier for: " << op.resource_id.debug_name << std::endl;

						std::cout << "\t " << std::setw( 3 ) << "#"
						          << " : " << std::setw( 30 ) << "visible_access"
						          << " : " << std::setw( 30 ) << "write_stage"
						          << " : "
						          << "layout" << std::endl;

						auto const &syncChain = frame.syncChainTable.at( op.resource_id );

						for ( size_t i = op.sync_chain_offset_initial; i <= op.sync_chain_offset_final; i++ ) {
							auto const &s = syncChain[ i ];

							std::cout << "\t " << std::setw( 3 ) << std::dec << i
							          << " : " << std::setw( 30 ) << to_string( s.visible_access )
							          << " : " << std::setw( 30 ) << to_string( s.write_stage )
							          << " : " << to_string( s.layout ) << std::endl;
						}

						std::cout << std::flush;
					}

					auto dstImage = frame_data_get_image_from_le_resource_id( frame, op.resource_id );

					vk::ImageSubresourceRange rangeAllMiplevels;
					rangeAllMiplevels
					    .setAspectMask( vk::ImageAspectFlagBits::eColor )
					    .setBaseMipLevel( 0 )
					    .setLevelCount( 1 ) // we want all miplevels to be in transferDstOptimal.
					    .setBaseArrayLayer( 0 )
					    .setLayerCount( 1 );

					vk::ImageMemoryBarrier imageLayoutTransfer;
					imageLayoutTransfer
					    .setSrcAccessMask( stateInitial.visible_access ) // no prior access
					    .setDstAccessMask( stateFinal.visible_access )   // ready image for transferwrite
					    .setOldLayout( stateInitial.layout )             // from vk::ImageLayout::eUndefined
					    .setNewLayout( stateFinal.layout )               // to transfer_dst_optimal
					    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
					    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
					    .setImage( dstImage )
					    .setSubresourceRange( rangeAllMiplevels );

					cmd.pipelineBarrier(
					    uint32_t( stateInitial.write_stage ) == 0 ? vk::PipelineStageFlagBits::eTopOfPipe : stateInitial.write_stage, // srcStage, top of pipe if not set.
					    stateFinal.write_stage,                                                                                       // dstStage
					    {},
					    {},
					    {},                   // buffer: host write -> transfer read
					    {imageLayoutTransfer} // image: transfer layout
					);
				}
			} // end for all explicit sync ops.
		}

		// Draw passes must begin by opening a Renderpass context.
		if ( pass.type == LE_RENDER_PASS_TYPE_DRAW && pass.renderPass ) {

			for ( size_t i = 0; i != ( pass.numColorAttachments + pass.numDepthStencilAttachments ); ++i ) {
				clearValues[ i ] = pass.attachments[ i ].clearValue;
			}

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
						argumentsOk &= ( nullptr != a.bufferInfo.buffer );
                        break;
					case vk::DescriptorType::eCombinedImageSampler:
					case vk::DescriptorType::eSampledImage:
					case vk::DescriptorType::eStorageImage:
						argumentsOk &= ( nullptr != a.imageInfo.imageView ); // if sampler, must have valid image view
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

			// This is legit behaviour for draw passes which are used only to clear attachments,
			// in which case they don't need to include any draw commands.

			// assert( false );
			//std::cout << "WARNING: pass '" << pass.debugName << "' does not have valid encoder." << std::endl
			//          << std::flush;
		}

		if ( commandStream != nullptr && numCommands > 0 ) {

			le_pipeline_manager_o *pipelineManager = encoder_i.get_pipeline_manager( pass.encoder );

			std::vector<vk::Buffer>       vertexInputBindings( maxVertexInputBindings, nullptr );
			void *                        dataIt = commandStream;
			le_pipeline_and_layout_info_t currentPipeline;

			while ( commandIndex != numCommands ) {

				auto header = static_cast<le::CommandHeader *>( dataIt );

				switch ( header->info.type ) {

				case le::CommandType::eBindGraphicsPipeline: {
					auto *le_cmd = static_cast<le::CommandBindGraphicsPipeline *>( dataIt );
					if ( pass.type == LE_RENDER_PASS_TYPE_DRAW ) {
						// at this point, a valid renderpass must be bound

						using namespace le_backend_vk;
						// -- potentially compile and create pipeline here, based on current pass and subpass
						currentPipeline = le_pipeline_manager_i.produce_pipeline( pipelineManager, le_cmd->info.gpsoHandle, pass, subpassIndex );

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
										DescriptorData descriptorData{
                                            .type          = vk::DescriptorType( b.type ),
                                            .bindingNumber = uint32_t( b.binding ),
                                            .arrayIndex    = uint32_t( arrayIndex ),
										};
										descriptorData.bufferInfo.range = VK_WHOLE_SIZE;
										setData.emplace_back( descriptorData );
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

					} else {
						// -- TODO: warn that graphics pipelines may only be bound within
						// draw passes.
					}
				} break;

				case le::CommandType::eBindComputePipeline: {
					auto *le_cmd = static_cast<le::CommandBindComputePipeline *>( dataIt );
					if ( pass.type == LE_RENDER_PASS_TYPE_COMPUTE ) {
						// at this point, a valid renderpass must be bound

						using namespace le_backend_vk;
						// -- potentially compile and create pipeline here, based on current pass and subpass
						currentPipeline = le_pipeline_manager_i.produce_compute_pipeline( pipelineManager, le_cmd->info.cpsoHandle );

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
										DescriptorData descriptorData{
                                            .type          = vk::DescriptorType( b.type ),
                                            .bindingNumber = uint32_t( b.binding ),
                                            .arrayIndex    = uint32_t( arrayIndex ),
										};
										descriptorData.bufferInfo.range = VK_WHOLE_SIZE;

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

						cmd.bindPipeline( vk::PipelineBindPoint::eCompute, currentPipeline.pipeline );

					} else {
						// -- TODO: warn that compute pipelines may only be bound within
						// compute passes.
					}

				} break;

				case le::CommandType::eDispatch: {
					auto *le_cmd = static_cast<le::CommandDispatch *>( dataIt );

					// -- update descriptorsets via template if tainted
					bool argumentsOk = updateArguments( device, descriptorPool, argumentState, descriptorSets );

					if ( false == argumentsOk ) {
						break;
					}

					// --------| invariant: arguments were updated successfully

					if ( argumentState.setCount > 0 ) {

						cmd.bindDescriptorSets( vk::PipelineBindPoint::eCompute,
						                        currentPipelineLayout,
						                        0,
						                        argumentState.setCount,
						                        descriptorSets,
						                        argumentState.dynamicOffsetCount,
						                        argumentState.dynamicOffsets.data() );
					}

					cmd.dispatch( le_cmd->info.groupCountX, le_cmd->info.groupCountY, le_cmd->info.groupCountZ );
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

				case le::CommandType::eSetArgumentData: {
					// we need to store the data for the dynamic binding which was set as an argument to the ubo
					// this alters our internal state
					auto *le_cmd = static_cast<le::CommandSetArgumentData *>( dataIt );

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

					auto &bindingData = argumentState.setData[ setIndex ][ binding ].bufferInfo;

					bindingData.buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer_id );
					bindingData.range  = std::max<uint32_t>( le_cmd->info.range, b->range );

					if ( bindingData.range == 0 ) {
						// If no range was specified, we must default to VK_WHOLE_SIZE,
						// as a range setting of 0 is not allowed in Vulkan.
						bindingData.range = VK_WHOLE_SIZE;
					}

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

				case le::CommandType::eBindArgumentBuffer: {
					// we need to store the data for the dynamic binding which was set as an argument to the ubo
					// this alters our internal state
					auto *le_cmd = static_cast<le::CommandBindArgumentBuffer *>( dataIt );

					uint64_t argument_name_id = le_cmd->info.argument_name_id;

					// find binding info with name referenced in command

					auto b = std::find_if( argumentState.binding_infos.begin(), argumentState.binding_infos.end(),
					                       [&argument_name_id]( const le_shader_binding_info &e ) -> bool {
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

					auto &bindingData = argumentState.setData[ setIndex ][ binding ].bufferInfo;

					bindingData.buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer_id );
					bindingData.range  = std::min<uint64_t>( le_cmd->info.range, b->range ); // CHECK: use range from binding to limit range...

					if ( bindingData.range == 0 ) {

						// If no range was specified, we must default to VK_WHOLE_SIZE,
						// as a range setting of 0 is not allowed in Vulkan.

						bindingData.range = VK_WHOLE_SIZE;
					}

					// If binding is in fact a dynamic binding, set the corresponding dynamic offset
					// and set the buffer offset to 0.
					if ( b->type == enumToNum( vk::DescriptorType::eStorageBufferDynamic ) ||
					     b->type == enumToNum( vk::DescriptorType::eUniformBufferDynamic ) ) {
						auto dynamicOffset                            = b->dynamic_offset_idx;
						bindingData.offset                            = 0;
						argumentState.dynamicOffsets[ dynamicOffset ] = uint32_t( le_cmd->info.offset );
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
						std::cerr << "Could not find requested texture: " << le_cmd->info.texture_id.debug_name << " Ignoring texture binding command." << std::endl
						          << std::flush;
						break;
					}

					// ----------| invariant: texture has been found

					// FIXME: (sync) image layout at this point *must* be shaderReadOnlyOptimal.
					bindingData.imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
					bindingData.imageInfo.sampler     = foundTex->second.sampler;
					bindingData.imageInfo.imageView   = foundTex->second.imageView;

					bindingData.arrayIndex = uint32_t( le_cmd->info.array_index );
					bindingData.type       = vk::DescriptorType::eCombinedImageSampler;

				} break;

				case le::CommandType::eSetArgumentImage: {
					auto *   le_cmd           = static_cast<le::CommandSetArgumentImage *>( dataIt );
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

					auto foundImgView = frame.imageViews.find( le_cmd->info.image_id );
					if ( foundImgView == frame.imageViews.end() ) {
						std::cerr << "Could not find image view for image: " << le_cmd->info.image_id.debug_name << " Ignoring image binding command." << std::endl
						          << std::flush;
						break;
					}

					// ----------| invariant: image view has been found

					// FIXME: (sync) image layout at this point *must* be general, if we wanted to write to this image.
					bindingData.imageInfo.imageLayout = vk::ImageLayout::eGeneral;
					bindingData.imageInfo.imageView   = foundImgView->second;

					bindingData.type       = vk::DescriptorType::eStorageImage;
					bindingData.arrayIndex = uint32_t( le_cmd->info.array_index );

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

					auto srcBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.src_buffer_id );
					auto dstImage  = frame_data_get_image_from_le_resource_id( frame, le_cmd->info.dst_image_id );

					// We define a range that covers all miplevels. this is useful as it allows us to transform
					// Image layouts in bulk, covering the full mip chain.
					vk::ImageSubresourceRange rangeAllRemainingMiplevels;
					rangeAllRemainingMiplevels
					    .setAspectMask( vk::ImageAspectFlagBits::eColor )
					    .setBaseMipLevel( le_cmd->info.dst_miplevel )
					    .setLevelCount( VK_REMAINING_MIP_LEVELS ) // we want all miplevels to be in transferDstOptimal.
					    .setBaseArrayLayer( 0 )
					    .setLayerCount( 1 );

					{
						vk::BufferMemoryBarrier bufferTransferBarrier;
						bufferTransferBarrier
						    .setSrcAccessMask( vk::AccessFlagBits::eHostWrite )    // after host write
						    .setDstAccessMask( vk::AccessFlagBits::eTransferRead ) // ready buffer for transfer read
						    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
						    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
						    .setBuffer( srcBuffer )
						    .setOffset( 0 ) // we assume a fresh buffer was allocated, so offset must be 0
						    .setSize( le_cmd->info.numBytes );

						vk::ImageMemoryBarrier imageLayoutToTransferDstOptimal;
						imageLayoutToTransferDstOptimal
						    .setSrcAccessMask( {} )                                 // no prior access
						    .setDstAccessMask( vk::AccessFlagBits::eTransferWrite ) // ready image for transferwrite
						    .setOldLayout( {} )                                     // from vk::ImageLayout::eUndefined
						    .setNewLayout( vk::ImageLayout::eTransferDstOptimal )   // to transfer_dst_optimal
						    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
						    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
						    .setImage( dstImage )
						    .setSubresourceRange( rangeAllRemainingMiplevels );

						cmd.pipelineBarrier(
						    vk::PipelineStageFlagBits::eHost,
						    vk::PipelineStageFlagBits::eTransfer,
						    {},
						    {},
						    {bufferTransferBarrier},          // buffer: host write -> transfer read
						    {imageLayoutToTransferDstOptimal} // image: prepare for transfer write
						);
					}

					{
						// Copy data for first mip level from buffer to image.
						//
						// Then use the first mip level as a source for subsequent mip levels.
						// When copying from a lower mip level to a higher mip level, we must make
						// sure to add barriers, as these blit operations are transfers.
						//

						vk::ImageSubresourceLayers imageSubresourceLayers;
						imageSubresourceLayers
						    .setAspectMask( vk::ImageAspectFlagBits::eColor )
						    .setMipLevel( 0 )
						    .setBaseArrayLayer( 0 )
						    .setLayerCount( 1 );

						vk::BufferImageCopy region;
						region
						    .setBufferOffset( 0 )                                       // buffer offset is 0 as staging buffer is a fresh, specially allocated buffer
						    .setBufferRowLength( 0 )                                    // 0 means tightly packed
						    .setBufferImageHeight( 0 )                                  // 0 means tightly packed
						    .setImageSubresource( std::move( imageSubresourceLayers ) ) // stored inline
						    .setImageOffset( {le_cmd->info.offset_w, le_cmd->info.offset_h, 0} )
						    .setImageExtent( {le_cmd->info.image_w, le_cmd->info.image_h, 1} );

						cmd.copyBufferToImage( srcBuffer, dstImage, vk::ImageLayout::eTransferDstOptimal, 1, &region );
					}

					if ( le_cmd->info.num_miplevels > 1 ) {

						// We generate additional miplevels by issueing scaled blits from one image subresource to the
						// next higher mip level subresource.

						// For this to work, we must first make sure that the image subresource we just wrote to
						// is ready to be read back. We do this by issueing a read-after-write barrier, and with
						// the same barrier we also transition the source subresource image to transfer_src_optimal
						// layout (which is a requirement for blitting operations)
						//
						// The target image subresource is already in layout transfer_dst_optimal, as this is the
						// layout we applied to the whole mip chain when

						const uint32_t         base_miplevel = le_cmd->info.dst_miplevel;
						vk::ImageMemoryBarrier prepareBlit;
						prepareBlit
						    .setSrcAccessMask( vk::AccessFlagBits::eTransferWrite ) // transfer write
						    .setDstAccessMask( vk::AccessFlagBits::eTransferRead )  // ready image for transfer read
						    .setOldLayout( vk::ImageLayout::eTransferDstOptimal )   // from transfer dst optimal
						    .setNewLayout( vk::ImageLayout::eTransferSrcOptimal )   // to shader readonly optimal
						    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
						    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
						    .setImage( dstImage )
						    .setSubresourceRange( {vk::ImageAspectFlagBits::eColor, base_miplevel, 1, 0, 1} );

						cmd.pipelineBarrier( vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, {prepareBlit} );

						// Now blit from the srcMipLevel to dstMipLevel

						int32_t srcImgWidth  = int32_t( le_cmd->info.image_w );
						int32_t srcImgHeight = int32_t( le_cmd->info.image_h );

						for ( uint32_t dstMipLevel = le_cmd->info.dst_miplevel + 1; dstMipLevel < le_cmd->info.num_miplevels; dstMipLevel++ ) {

							// Blit from lower mip level into next higher mip level
							auto srcMipLevel = dstMipLevel - 1;

							// Calculate width and height for next image in mip chain as half the corresponding source
							// image dimension, unless dimension is smaller or equal to 2, in which case clamp to 1.
							auto dstImgWidth  = srcImgWidth > 2 ? srcImgWidth >> 1 : 1;
							auto dstImgHeight = srcImgHeight > 2 ? srcImgHeight >> 1 : 1;

							vk::ImageSubresourceRange rangeSrcMipLevel( vk::ImageAspectFlagBits::eColor, srcMipLevel, 1, 0, 1 );
							vk::ImageSubresourceRange rangeDstMipLevel( vk::ImageAspectFlagBits::eColor, dstMipLevel, 1, 0, 1 );

							vk::ImageBlit region;

							vk::Offset3D offsetZero = {0, 0, 0};
							vk::Offset3D offsetSrc  = {srcImgWidth, srcImgHeight, 1};
							vk::Offset3D offsetDst  = {dstImgWidth, dstImgHeight, 1};
							region
							    .setSrcSubresource( {vk::ImageAspectFlagBits::eColor, srcMipLevel, 0, 1} )
							    .setDstSubresource( {vk::ImageAspectFlagBits::eColor, dstMipLevel, 0, 1} )
							    .setSrcOffsets( {offsetZero, offsetSrc} )
							    .setDstOffsets( {offsetZero, offsetDst} )
							    //
							    ;

							cmd.blitImage( dstImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, 1, &region, vk::Filter::eLinear );

							// Now we barrier Read after Write, and transition our freshly blitted subresource to transferSrc,
							// so that the next iteration may read from it.

							vk::ImageMemoryBarrier finishBlit;
							finishBlit
							    .setSrcAccessMask( vk::AccessFlagBits::eTransferWrite ) // transfer write
							    .setDstAccessMask( vk::AccessFlagBits::eTransferRead )  // ready image for shader read
							    .setOldLayout( vk::ImageLayout::eTransferDstOptimal )   // from transfer dst optimal
							    .setNewLayout( vk::ImageLayout::eTransferSrcOptimal )   // to shader readonly optimal
							    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
							    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
							    .setImage( dstImage )
							    .setSubresourceRange( rangeDstMipLevel );

							cmd.pipelineBarrier( vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, {finishBlit} );

							// Store this miplevel image's dimensions for next iteration
							srcImgHeight = dstImgHeight;
							srcImgWidth  = dstImgWidth;
						}

					} // end if mipLevelCount > 1

					// Transition image from transfer src optimal to shader read only optimal layout

					{
						vk::ImageMemoryBarrier imageLayoutToShaderReadOptimal;

						if ( le_cmd->info.num_miplevels > 1 ) {

							// If there were additional miplevels, the miplevel generation logic ensures that all subresources
							// are left in transfer_src layout.

							imageLayoutToShaderReadOptimal
							    .setSrcAccessMask( {} )                                  // nothing to flush, as previous barriers ensure flush
							    .setDstAccessMask( vk::AccessFlagBits::eShaderRead )     // ready image for shader read
							    .setOldLayout( vk::ImageLayout::eTransferSrcOptimal )    // all subresources are in transfer src optimal
							    .setNewLayout( vk::ImageLayout::eShaderReadOnlyOptimal ) // to shader readonly optimal
							    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
							    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
							    .setImage( dstImage )
							    .setSubresourceRange( rangeAllRemainingMiplevels );
						} else {

							// If there are no additional miplevels, the single subresource will still be in
							// transfer_dst layout after pixel data was uploaded to it.

							imageLayoutToShaderReadOptimal
							    .setSrcAccessMask( vk::AccessFlagBits::eTransferWrite )  // no need to flush anything, that's been done by barriers before
							    .setDstAccessMask( vk::AccessFlagBits::eShaderRead )     // ready image for shader read
							    .setOldLayout( vk::ImageLayout::eTransferDstOptimal )    // the single one subresource is in transfer dst optimal
							    .setNewLayout( vk::ImageLayout::eShaderReadOnlyOptimal ) // to shader readonly optimal
							    .setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
							    .setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
							    .setImage( dstImage )
							    .setSubresourceRange( rangeAllRemainingMiplevels );
						}

						cmd.pipelineBarrier(
						    vk::PipelineStageFlagBits::eTransfer,
						    vk::PipelineStageFlagBits::eFragmentShader,
						    {},
						    {},
						    {},                              // buffers: nothing to do
						    {imageLayoutToShaderReadOptimal} // images: prepare for shader read
						);
					}

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
static void backend_update_shader_modules( le_backend_o *self ) {
	using namespace le_backend_vk;
	le_pipeline_manager_i.update_shader_modules( self->pipelineCache );
}

// ----------------------------------------------------------------------
static le_shader_module_o *backend_create_shader_module( le_backend_o *self, char const *path, const LeShaderStageEnum &moduleType ) {
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

	using namespace le_swapchain_vk;

	bool presentSuccessful = swapchain_i.present( self->swapchain, self->device->getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex );

	return presentSuccessful;
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
	vk_backend_i.get_staging_allocator      = backend_get_staging_allocator;
	vk_backend_i.poll_frame_fence           = backend_poll_frame_fence;
	vk_backend_i.clear_frame                = backend_clear_frame;
	vk_backend_i.acquire_physical_resources = backend_acquire_physical_resources;
	vk_backend_i.process_frame              = backend_process_frame;
	vk_backend_i.dispatch_frame             = backend_dispatch_frame;

	vk_backend_i.get_pipeline_cache    = backend_get_pipeline_cache;
	vk_backend_i.update_shader_modules = backend_update_shader_modules;
	vk_backend_i.create_shader_module  = backend_create_shader_module;

	vk_backend_i.get_swapchain_resource = backend_get_swapchain_resource;
	vk_backend_i.get_swapchain_extent   = backend_get_swapchain_extent;

	auto &private_backend_i                  = api_i->private_backend_vk_i;
	private_backend_i.get_vk_device          = backend_get_vk_device;
	private_backend_i.get_vk_physical_device = backend_get_vk_physical_device;
	private_backend_i.get_le_device          = backend_get_le_device;
	private_backend_i.allocate_image         = backend_allocate_image;
	private_backend_i.destroy_image          = backend_destroy_image;
	private_backend_i.allocate_buffer        = backend_allocate_buffer;
	private_backend_i.destroy_buffer         = backend_destroy_buffer;

	auto &staging_allocator_i   = api_i->le_staging_allocator_i;
	staging_allocator_i.create  = staging_allocator_create;
	staging_allocator_i.destroy = staging_allocator_destroy;
	staging_allocator_i.map     = staging_allocator_map;
	staging_allocator_i.reset   = staging_allocator_reset;

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
