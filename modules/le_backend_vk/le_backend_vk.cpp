#include "le_core.h"
#include "le_backend_vk.h"
#include "le_log.h"
#include "util/vk_mem_alloc/vk_mem_alloc.h" // for allocation
#include "le_backend_types_internal.h"      // includes vulkan.hpp
#include "le_swapchain_vk.h"
#include "le_window.h"
#include "le_renderer.h"
#include "private/le_resource_handle_t.inl"
#include "3rdparty/src/spooky/SpookyV2.h" // for hashing renderpass gestalt

#include <bitset>
#include <cassert>
#include <filesystem>
#include <system_error>
#include <vector>
#include <string>
#include <unordered_map>
#include <forward_list>
#include <sstream>
#include <iomanip>
#include <list>
#include <set>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstring> // for memcpy
#include <array>
#include <algorithm> // for std::find

#include "util/volk/volk.h"

#include "le_backend_vk_settings.inl"
#include "private/le_backend_vk/vk_to_str_helpers.inl"

#include "private/le_backend_vk/le_backend_vk_instance.inl"

static constexpr auto LOGGER_LABEL = "le_backend";
#ifdef _MSC_VER
#	include <Windows.h> // for getModule
#else
#	include <unistd.h> // for getexepath
#endif

#ifdef _WIN32
#	define __PRETTY_FUNCTION__ __FUNCSIG__
#	include <intrin.h> // for __lzcnt
#endif

#ifndef LE_PRINT_DEBUG_MESSAGES
#	define LE_PRINT_DEBUG_MESSAGES false
#endif

#ifndef DEBUG_TAG_RESOURCES
// Whether to tag resources - requires the debugUtils extension to be present.
#	define DEBUG_TAG_RESOURCES true
#endif

// Helper macro to convert le:: enums toVk enums
#define LE_ENUM_TO_VK( enum_name, fun_name )                                 \
	static inlineVkenum_name fun_name( le::enum_name const& rhs ) noexcept { \
		returnVkenum_name( rhs );                                            \
	}

#define LE_C_ENUM_TO_VK( enum_name, fun_name, c_enum_name )                \
	static inlineVkenum_name fun_name( c_enum_name const& rhs ) noexcept { \
		returnVkenum_name( rhs );                                          \
	}

LE_WRAP_ENUM_IN_STRUCT( VkFormat, VkFormatEnum ); // define wrapper struct `VkFormatEnum`

constexpr size_t LE_FRAME_DATA_POOL_BLOCK_SIZE  = 1u << 24; // 16.77 MB
constexpr size_t LE_FRAME_DATA_POOL_BLOCK_COUNT = 1;
constexpr size_t LE_LINEAR_ALLOCATOR_SIZE       = 1u << 24;

static constexpr VkImageSubresourceRange LE_IMAGE_SUBRESOURCE_RANGE_ALL_MIPLEVELS{
    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel   = 0,
    .levelCount     = VK_REMAINING_MIP_LEVELS,
    .baseArrayLayer = 0,
    .layerCount     = VK_REMAINING_ARRAY_LAYERS,
};

struct LeRtxBlasCreateInfo {
	le_rtx_blas_info_handle handle;
	uint64_t                buffer_size;
	uint64_t                scratch_buffer_size; // Requested scratch buffer size for bottom level acceleration structure
	uint64_t                device_address;      // 64bit address used by the top-level acceleration structure instances buffer.
	VkBuffer                buffer;              // OWNING : buffer used to store acceleration structure in device memory
	                                             // Used to to refer back to this bottom-level acceleration structure.
	                                             // Queried via vkGetAccelerationStructureDeviceAddressKHR after creating the acceleration structure.
	                                             // This is not my idea, but how the API is laid out...
};

struct LeRtxTlasCreateInfo {
	le_rtx_tlas_info_handle handle;
	uint64_t                buffer_size;
	uint64_t                scratch_buffer_size; // requested scratch buffer size for top level acceleration structure
	VkBuffer                buffer;              // OWNING: buffer used to store acceleration structure
};

// ----------------------------------------------------------------------
/// ResourceCreateInfo is used internally in to translate Renderer-specific structures
/// into Vulkan CreateInfos for buffers and images we wish to allocate in Vulkan.
///
/// The ResourceCreateInfo is then stored with the allocation, so that subsequent
/// requests for resources may check if a requested resource is already available to the
/// backend.
///
struct ResourceCreateInfo {

	LeResourceType type;

	union {
		VkBufferCreateInfo  bufferInfo; // | only one of either ever in use
		VkImageCreateInfo   imageInfo;  // | only one of either ever in use
		LeRtxBlasCreateInfo blasInfo;
		LeRtxTlasCreateInfo tlasInfo;
	};

	// Compares two ResourceCreateInfos, returns true if identical, false if not.
	//
	// FIXME: the comparison of pQueueFamilyIndices is fraught with peril,
	// as we must really compare the contents of the memory pointed at
	// rather than the pointer, and the pointer has no guarantee to be alife.
	bool operator==( const ResourceCreateInfo& rhs ) const {

		if ( type != rhs.type ) {
			return false;
		}

		if ( isBuffer() ) {

			return ( bufferInfo.flags == rhs.bufferInfo.flags &&
			         bufferInfo.size == rhs.bufferInfo.size &&
			         bufferInfo.usage == rhs.bufferInfo.usage &&
			         bufferInfo.sharingMode == rhs.bufferInfo.sharingMode
			         // bufferInfo.queueFamilyIndexCount == rhs.bufferInfo.queueFamilyIndexCount &&
			         // bufferInfo.pQueueFamilyIndices == rhs.bufferInfo.pQueueFamilyIndices // these two entries are ignored, as we assume sharingMode to be EXCLUSIVE
			);

		} else if ( isImage() ) {

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
			         imageInfo.initialLayout == rhs.imageInfo.initialLayout
			         // imageInfo.queueFamilyIndexCount == rhs.imageInfo.queueFamilyIndexCount &&
			         //  imageInfo.pQueueFamilyIndices == rhs.imageInfo.pQueueFamilyIndices // these two entries are ignored, as we assume sharingMode to be EXCLUSIVE
			);
		} else if ( isBlas() ) {
			return blasInfo.handle == rhs.blasInfo.handle &&
			       blasInfo.scratch_buffer_size == rhs.blasInfo.scratch_buffer_size &&
			       blasInfo.buffer_size == rhs.blasInfo.buffer_size;
		} else if ( isTlas() ) {
			return tlasInfo.handle == rhs.tlasInfo.handle &&
			       tlasInfo.scratch_buffer_size == rhs.tlasInfo.scratch_buffer_size &&
			       tlasInfo.buffer_size == rhs.tlasInfo.buffer_size;
			;
		} else {
			assert( false && "createInfo must be of known type" );
		}
		return false; // unreachable
	}

	// Greater-than operator returns true if rhs is a subset of this.
	// We use this operator to see whether we can re-use an existing resource
	// based on the currently allocated version of a resource.
	//
	// Note that we are only fuzzy where it is safe to be so - which is flags.
	bool operator>=( const ResourceCreateInfo& rhs ) const {

		if ( type != rhs.type ) {
			return false;
		}

		if ( isBuffer() ) {

			return ( bufferInfo.flags == rhs.bufferInfo.flags &&
			         bufferInfo.size == rhs.bufferInfo.size &&
			         ( ( bufferInfo.usage & rhs.bufferInfo.usage ) == rhs.bufferInfo.usage ) &&
			         bufferInfo.sharingMode == rhs.bufferInfo.sharingMode
			         // bufferInfo.queueFamilyIndexCount == rhs.bufferInfo.queueFamilyIndexCount &&
			         // bufferInfo.pQueueFamilyIndices == rhs.bufferInfo.pQueueFamilyIndices // ignored, as we assume sharingMode to be EXCLUSIVE
			);

		} else if ( isImage() ) {

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
			         imageInfo.initialLayout == rhs.imageInfo.initialLayout
			         // imageInfo.queueFamilyIndexCount == rhs.imageInfo.queueFamilyIndexCount &&
			         //( void* )imageInfo.pQueueFamilyIndices == ( void* )rhs.imageInfo.pQueueFamilyIndices // ignored, as we assume sharingMode to be EXCLUSIVE
			);
		} else if ( isBlas() ) {
			// NOTE: we don't compare scratch_buffer_sz, as scratch buffer sz is only available
			// *after* a resource has been allocated, and cannot therefore tell us anything useful
			// about whether a resource needs to be re-allocated...
			return blasInfo.handle == rhs.blasInfo.handle;
		} else if ( isTlas() ) {
			// NOTE: we don't compare scratch_buffer_sz, as scratch buffer sz is only available
			// *after* a resource has been allocated, and cannot therefore tell us anything useful
			// about whether a resource needs to be re-allocated...
			return tlasInfo.handle == rhs.tlasInfo.handle;
		}

		return false; // unreachable
	}

	bool operator!=( const ResourceCreateInfo& rhs ) const {
		return !operator==( rhs );
	}

	bool isBuffer() const {
		return type == LeResourceType::eBuffer;
	}

	bool isImage() const {
		return type == LeResourceType::eImage;
	}

	bool isBlas() const {
		return type == LeResourceType::eRtxBlas;
	}
	bool isTlas() const {
		return type == LeResourceType::eRtxTlas;
	}

	static ResourceCreateInfo from_le_resource_info( const le_resource_info_t& info );
};

// ----------------------------------------------------------------------

// bottom-level acceleration structure
struct le_rtx_blas_info_o {
	std::vector<le_rtx_geometry_t>       geometries;
	VkBuildAccelerationStructureFlagsKHR flags;
};

// top-level acceleration structure
struct le_rtx_tlas_info_o {
	uint32_t                             instances_count;
	VkBuildAccelerationStructureFlagsKHR flags;
};

// ----------------------------------------------------------------------

template <typename T>
class KillList : NoCopy, NoMove {
	std::mutex      mtx;
	std::vector<T*> infos;

  public:
	~KillList() {
		auto lck = std::scoped_lock( mtx );
		for ( auto& el : infos ) {
			delete el;
		}
	}
	void add_element( T* el ) {
		auto lck = std::scoped_lock( mtx );
		infos.push_back( el );
	}
};

// ----------------------------------------------------------------------

// Convert a log2 of sample count to the corresponding `vk::SampleCountFlagBits` enum
VkSampleCountFlagBits le_sample_count_log_2_to_vk( uint32_t sample_count_log2 ) {

	// this method is a quick and dirty hack, but as long as the
	// following static asserts hold true, it will work.

	static_assert( uint32_t( VK_SAMPLE_COUNT_1_BIT ) == 1 << 0, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( VK_SAMPLE_COUNT_2_BIT ) == 1 << 1, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( VK_SAMPLE_COUNT_4_BIT ) == 1 << 2, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( VK_SAMPLE_COUNT_8_BIT ) == 1 << 3, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( VK_SAMPLE_COUNT_16_BIT ) == 1 << 4, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( VK_SAMPLE_COUNT_32_BIT ) == 1 << 5, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( VK_SAMPLE_COUNT_64_BIT ) == 1 << 6, "SampleCountFlagBits conversion failed." );

	return VkSampleCountFlagBits( 1 << sample_count_log2 );
}

// ----------------------------------------------------------------------

// returns log2 of number of samples, so that number of samples can be
// calculated as `num_samples = 1 << log2_num_samples`
inline uint16_t get_sample_count_log_2( uint32_t const& sample_count ) {
#if defined( _MSC_VER )
	auto lz = __lzcnt( sample_count );
#else
	auto lz = __builtin_clz( sample_count );
#endif
	return 31 - lz;
}

// ----------------------------------------------------------------------

ResourceCreateInfo ResourceCreateInfo::from_le_resource_info( const le_resource_info_t& info ) {
	ResourceCreateInfo res{};

	res.type = info.type;

	switch ( info.type ) {
	case ( LeResourceType::eBuffer ): {
		res.bufferInfo = {
		    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .pNext                 = nullptr, // optional
		    .flags                 = 0,       // optional
		    .size                  = info.buffer.size,
		    .usage                 = VkBufferUsageFlags( info.buffer.usage ),
		    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		    .queueFamilyIndexCount = 0, // optional
		    .pQueueFamilyIndices   = nullptr,
		};

	} break;
	case ( LeResourceType::eImage ): {
		auto const& img = info.image;
		res.imageInfo   = {
		      .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		      .pNext                 = nullptr,                         // optional
		      .flags                 = VkImageCreateFlags( img.flags ), // optional
		      .imageType             = VkImageType( img.imageType ),
		      .format                = VkFormat( img.format ),
		      .extent                = { img.extent.width, img.extent.height, img.extent.depth },
		      .mipLevels             = img.mipLevels,
		      .arrayLayers           = img.arrayLayers,
		      .samples               = le_sample_count_log_2_to_vk( img.sample_count_log2 ),
		      .tiling                = VkImageTiling( img.tiling ),
		      .usage                 = VkImageUsageFlags( img.usage ),
		      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		      .queueFamilyIndexCount = 0, // optional
		      .pQueueFamilyIndices   = nullptr,
		      .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };

	} break;
	case ( LeResourceType::eRtxBlas ): {
		res.blasInfo.handle              = info.blas.info;
		res.blasInfo.scratch_buffer_size = 0;
		res.blasInfo.buffer_size         = 0;
		break;
	}
	case ( LeResourceType::eRtxTlas ): {
		res.tlasInfo.handle              = info.tlas.info;
		res.tlasInfo.scratch_buffer_size = 0;
		res.tlasInfo.buffer_size         = 0;
		break;
	}
	default:
		assert( false ); // we can only create (allocate) buffer or image resources
		break;
	}

	return res;
}

// ResourceState tracks the state of a resource as it would appear in scope 1 of a possible barrier,
//
// Note: Execution barrier defines:
//                                      scope 1 (happens-before) | barrier | scope 2
// See Section 7.1 in the spec.
//
// When two ResourceStates are chained, we can then infer the correct barriers, as scope 1
// of the second ResourceState becomes scope 2 for the barrier between them.
//
struct ResourceState {
	VkPipelineStageFlags2 stage;          // Pipeline stage (implies earlier logical stages) that needs to happen-before
	VkAccessFlags2        visible_access; // Which memory access in this stage currenty has visible memory -
	                                      // if any of these are WRITE accesses, these must be made available(flushed)
	                                      // before next access - for the next src access we can OR this with ANY_WRITES
	VkImageLayout layout;                 // Current layout (for images)
	uint32_t      renderpass_index;       // which renderpass currently uses this resource  -1 being outside of scope (e.g. swapchain)

	bool operator==( const ResourceState& rhs ) const {
		return visible_access == rhs.visible_access &&
		       stage == rhs.stage &&
		       layout == rhs.layout;
	}

	bool operator!=( const ResourceState& rhs ) const {
		return !operator==( rhs );
	}
};

// ------------------------------------------------------------

struct AllocatedResourceVk {
	VmaAllocation     allocation;
	VmaAllocationInfo allocationInfo;
	union {
		VkBuffer                   buffer;
		VkImage                    image;
		VkAccelerationStructureKHR blas; // bottom level acceleration structure
		VkAccelerationStructureKHR tlas; // top level acceleration structure
	} as;
	ResourceCreateInfo info;  // Creation info for resource
	ResourceState      state; // sync state for resource
	uint32_t           padding__;
};

struct le_staging_allocator_o {
	VmaAllocator                   allocator;      // non-owning, refers to backend allocator object
	VkDevice                       device;         // non-owning, refers to vulkan device object
	std::mutex                     mtx;            // protects all staging* elements
	std::vector<VkBuffer>          buffers;        // 0..n staging buffers used with the current frame (freed on frame clear)
	std::vector<VmaAllocation>     allocations;    // SOA: counterpart to buffers[]
	std::vector<VmaAllocationInfo> allocationInfo; // SOA: counterpart to buffers[]
};

// ------------------------------------------------------------

struct swapchain_state_t {
	VkSemaphore presentComplete = nullptr;
	VkSemaphore renderComplete  = nullptr;

	uint32_t image_idx          = uint32_t( ~0 );
	uint32_t surface_width      = 0;
	uint32_t surface_height     = 0;
	bool     present_successful = false;
	bool     acquire_successful = false;
};

// Herein goes all data which is associated with the current frame.
// Backend keeps track of multiple frames, exactly one per renderer::FrameData frame.
//
// We do this so that frames own their own memory exclusively, as long as a
// frame only operates only on its own memory, it will never see contention
// with other threads processing other frames concurrently.
struct BackendFrameData {

	VkFence  frameFence  = nullptr; // protects the frame - cpu waits on gpu to pass fence before deleting/recycling frame
	uint64_t frameNumber = 0;       // current frame number

	struct CommandPool {
		VkCommandPool                pool;                // One pool per submission - must be allocated from the same queue the commands get submitted to.
		std::vector<VkCommandBuffer> buffers;             // Allocated from pool, reset when frame gets recycled via pool.reset
		uint32_t                     vk_queue_family_idx; // vulkan queue family index from which pool was allocated
		bool                         is_used = false;
	};

	struct PerQueueSubmissionData {
		uint32_t              queue_idx;               // backend device queue index
		VkQueueFlags          queue_flags;             // queue flags for this submission
		std::vector<uint32_t> pass_indices;            // which passes from the current frame to add to this submission, count tells us about number of command buffers that need to be alloated
		CommandPool*          command_pool;            // non-owning. which command pool from the list of available command pools
		std::string           debug_root_passes_names; // name of root passes
	};                                                 //
	std::vector<PerQueueSubmissionData> queue_submission_data;
	std::vector<CommandPool*>           available_command_pools; // Owning. reset on frame recycle, delete all objects on BackendFrameData::destroy

	std::vector<swapchain_state_t> swapchain_state;

	struct Texture {
		VkSampler   sampler;
		VkImageView imageView;
	};

	using texture_map_t = std::unordered_map<le_texture_handle, Texture>;

	std::unordered_map<le_img_resource_handle, VkImageView> imageViews; // non-owning, references to frame-local textures, cleared on frame fence.

	// With `syncChainTable` and image_attachment_info_o.syncState, we should
	// be able to create renderpasses. Each resource has a sync chain, and each attachment_info
	// has a struct which holds indices into the sync chain telling us where to look
	// up the sync state for a resource at different stages of renderpass construction.
	using sync_chain_table_t = std::unordered_map<le_resource_handle, std::vector<ResourceState>>;
	sync_chain_table_t syncChainTable;

	static_assert( sizeof( VkBuffer ) == sizeof( VkImageView ) && sizeof( VkBuffer ) == sizeof( VkImage ), "size of AbstractPhysicalResource components must be identical" );

	// Map from renderer resource id to physical resources - only contains resources this frame uses.
	// Q: Does this table actually own the resources?
	// A: It must not: as it is used to map external resources as well.
	std::unordered_map<le_resource_handle, AbstractPhysicalResource> physicalResources;

	/// \brief vk resources retained and destroyed with BackendFrameData.
	/// These resources (such as samplers, imageviews, framebuffers) are transient,
	/// and lifetime of these resources is tied to the frame fence.
	std::forward_list<AbstractPhysicalResource> ownedResources;

	/// \brief if user provides explicit resource info, we collect this here, so that we can make sure
	/// that any inferred resourceInfo is compatible with what the user selected.
	/// there is no guarantee that declared resources are unique, which means we must consolidate.
	std::vector<le_resource_handle> declared_resources_id;   // | pre-declared resources (explicitly declared via rendergraph)
	std::vector<le_resource_info_t> declared_resources_info; // | pre-declared resources (explicitly declared via rendergraph)

	std::vector<BackendRenderPass>   passes;
	std::vector<le::RootPassesField> queue_submission_keys; // One key per isolated queue invocation,
	                                                        // each key represents an isolated tree from the graph,
	                                                        // with each bit representing a contributing root node index.
	                                                        // Passes internally store to which root node they contribute,
	                                                        // which allows us to associate passes with each entry in this vector.
	std::vector<std::string> debug_root_passes_names;       // names for root passis in RootPassesField

	std::vector<texture_map_t> textures_per_pass; // non-owning, references to frame-local textures, cleared on frame fence.

	std::vector<VkDescriptorPool> descriptorPools; // one descriptor pool per pass

	typedef std::unordered_map<le_resource_handle, AllocatedResourceVk> ResourceMap_T;

	ResourceMap_T availableResources; // resources this frame may use - each entry represents an association between a le_resource_handle and a vk resource
	ResourceMap_T binnedResources;    // resources to delete when this frame comes round to clear()

	/*

	  Each Frame has one allocation pool from which all allocations for scratch buffers are drawn.

	  When creating encoders, each encoder has their own sub-allocator, each sub-allocator owns an
	  independent block of memory allcated from the frame pool. This way, encoders can work on their
	  own thread.

	 */
	VmaPool allocationPool; // pool from which allocations for this frame come from

	std::vector<le_allocator_o*>   allocators;       // owning; typically one per `le_worker_thread`.
	std::vector<VkBuffer>          allocatorBuffers; // per allocator: one vkBuffer
	std::vector<VmaAllocation>     allocations;      // per allocator: one allocation
	std::vector<VmaAllocationInfo> allocationInfos;  // per allocator: one allocationInfo

	le_staging_allocator_o* stagingAllocator; // owning: allocator for large objects to GPU memory

	bool must_create_queues_dot_graph = false;
};

struct modern_swapchain_data_t {
	le_swapchain_o*    swapchain;                // owned
	VkSurfaceFormatKHR swapchain_surface_format; // contains VkFormat and (optional) color space
	VkSurfaceKHR       swapchain_surface;        // owned, optional
	uint32_t           height;
	uint32_t           width;
};

/// \brief backend data object
struct le_backend_o {

	le_backend_vk_instance_o*   instance;
	std::unique_ptr<le::Device> device;

	std::vector<BackendQueueInfo*>         queues;                                              // queues which were created via device
	std::unordered_map<uint32_t, uint32_t> default_queue_for_family_index;                      // map from queue_family index to index of default queue in queues for this queue family
	uint32_t                               queue_default_graphics_idx                  = 0;     // TODO: set to correct index if other than 0; must be index of default graphics queue, 0 by default
	bool                                   must_track_resources_queue_family_ownership = false; // Whether we must keep track of queue family indices per resource - this applies only if not all queues have the same queue family index

	std::vector<le_swapchain_o*> swapchains; // Owning.

	std::atomic<uint64_t>                                 modern_swapchains_next_handle; // monotonically increasing handle to index modern_swapchains
	std::unordered_map<uint64_t, modern_swapchain_data_t> modern_swapchains;             // Container of owned swapchains. Access only on the main thread.

	std::vector<VkSurfaceKHR> windowSurfaces; // owning. one per window swapchain.

	// Default color formats are inferred during setup() based on
	// swapchain surface (color) and device properties (depth/stencil)
	std::vector<VkFormat>               swapchainImageFormat; ///< default image format used for swapchain (backbuffer image must be in this format)
	std::vector<uint32_t>               swapchainWidth;       ///< swapchain width gathered when setting/resetting swapchain
	std::vector<uint32_t>               swapchainHeight;      ///< swapchain height gathered when setting/resetting swapchain
	std::vector<le_img_resource_handle> swapchain_resources;  ///< resource handle for image associated with each swapchain

	le::Format defaultFormatColorAttachment        = {}; ///< default image format used for color attachments
	le::Format defaultFormatDepthStencilAttachment = {}; ///< default image format used for depth stencil attachments
	le::Format defaultFormatSampledImage           = {}; ///< default image format used for sampled images

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_props{};

	// Siloed per-frame memory
	std::vector<BackendFrameData> mFrames;
	uint64_t                      totalFrameCount = 0; // total number of rendered or in-flight frames

	le_pipeline_manager_o* pipelineCache = nullptr;

	VmaAllocator mAllocator = nullptr;

	uint32_t queueFamilyIndexGraphics = 0; // inferred during setup

	KillList<le_rtx_blas_info_o> rtx_blas_info_kill_list; // used to keep track rtx_blas_infos.
	KillList<le_rtx_tlas_info_o> rtx_tlas_info_kill_list; // used to keep track rtx_blas_infos.

	// Vulkan resources which are available to all frames.
	// Generally, a resource needs to stay alive until the last frame that uses it has crossed its fence.
	// FIXME: Access to this map needs to be secured via mutex...
	struct {
		std::unordered_map<le_resource_handle, AllocatedResourceVk> allocatedResources; // Allocated resources, indexed by resource name hash
	} only_backend_allocate_resources_may_access;                                       // Only acquire_physical_resources may read/write

	std::unordered_map<le_resource_handle, uint64_t> resource_queue_family_ownership[ 2 ]; // per-resource queue family ownership - we use this to detect queue family ownership change for resources
};

// State of arguments for currently bound pipeline - we keep this here,
// so that we can update in bulk before draw, or dispatch command is issued.
//
struct ArgumentState {
	uint32_t                                   dynamicOffsetCount = 0;  // count of dynamic elements in current pipeline
	std::array<uint32_t, 256>                  dynamicOffsets     = {}; // offset for each dynamic element in current pipeline
	uint32_t                                   setCount           = 0;  // current count of bound descriptorSets (max: 8)
	std::array<std::vector<DescriptorData>, 8> setData;                 // data per-set

	std::array<VkDescriptorUpdateTemplate, 8> updateTemplates; // update templates for currently bound descriptor sets
	std::array<VkDescriptorSetLayout, 8>      layouts;         // layouts for currently bound descriptor sets
	std::vector<le_shader_binding_info>       binding_infos;
};

struct DescriptorSetState {
	VkDescriptorSetLayout       setLayout;
	std::vector<DescriptorData> setData;
};

struct RtxState {
	bool               is_set;
	le_resource_handle sbt_buffer; // shader binding table buffer
	uint64_t           ray_gen_sbt_offset;
	uint64_t           ray_gen_sbt_size;
	uint64_t           miss_sbt_offset;
	uint64_t           miss_sbt_stride;
	uint64_t           miss_sbt_size;
	uint64_t           hit_sbt_offset;
	uint64_t           hit_sbt_stride;
	uint64_t           hit_sbt_size;
	uint64_t           callable_sbt_offset;
	uint64_t           callable_sbt_stride;
	uint64_t           callable_sbt_size;
};

// ----------------------------------------------------------------------

static inline void le_format_get_is_depth_stencil( le::Format const& format_, bool& isDepth, bool& isStencil ) {

	switch ( format_ ) {
	case le::Format::eD16Unorm:         // fall-through
	case le::Format::eX8D24UnormPack32: // fall-through
	case le::Format::eD32Sfloat:        // fall-through
		isDepth   = true;
		isStencil = false;
		break;
	case le::Format::eS8Uint:
		isDepth   = false;
		isStencil = true;
		break;
	case le::Format::eD16UnormS8Uint:  // fall-through
	case le::Format::eD24UnormS8Uint:  // fall-through
	case le::Format::eD32SfloatS8Uint: // fall-through
		isDepth = isStencil = true;
		break;

	default:
		isDepth = isStencil = false;
		break;
	}

	return;
}

// ----------------------------------------------------------------------
static VkBufferUsageFlags defaults_get_buffer_usage_scratch() {

	VkBufferUsageFlags flags =
	    VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
	    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
	    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	// Enable shader_device_address for scratch buffer, if raytracing feature is requested
	static auto settings = le_backend_vk::api->backend_settings_singleton;
	if ( settings->requested_device_features.ray_tracing_pipeline.rayTracingPipeline ) {
		flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	}

	return flags;
}
// ----------------------------------------------------------------------

static void backend_create_window_surface( le_backend_o* self, le_swapchain_settings_t* settings ) {

	assert( settings->type == le_swapchain_settings_t::LE_KHR_SWAPCHAIN );
	assert( settings->khr_settings.window );

	using namespace le_window;
	VkInstance instance               = le_backend_vk::vk_instance_i.get_vk_instance( self->instance );
	settings->khr_settings.vk_surface = window_i.create_surface( settings->khr_settings.window, instance );

	assert( settings->khr_settings.vk_surface );

	self->windowSurfaces.emplace_back( settings->khr_settings.vk_surface );
}

// ----------------------------------------------------------------------

static void backend_destroy_window_surfaces( le_backend_o* self ) {
	static auto logger = LeLog( LOGGER_LABEL );

	for ( auto& surface : self->windowSurfaces ) {
		VkInstance instance = le_backend_vk::vk_instance_i.get_vk_instance( self->instance );
		vkDestroySurfaceKHR( instance, surface, nullptr );
		logger.info( "Surface %x destroyed", surface );
	}
	self->windowSurfaces.clear();
}

// ----------------------------------------------------------------------

static le_backend_o* backend_create() {
	auto self = new le_backend_o;
	return self;
}

// ----------------------------------------------------------------------

static void backend_destroy_modern_swapchain( le_backend_o* self, modern_swapchain_data_t& swapchain_info ) {
	static auto logger = LeLog( LOGGER_LABEL );
	using namespace le_swapchain_vk;
	using namespace le_backend_vk;

	VkInstance instance = vk_instance_i.get_vk_instance( self->instance );

	if ( swapchain_info.swapchain ) {

		// we must first destroy the khr swapchain object, then the surface if there is a surface.
		swapchain_i.destroy( swapchain_info.swapchain );

		if ( swapchain_info.swapchain_surface ) {
			vkDestroySurfaceKHR( instance, swapchain_info.swapchain_surface, nullptr );
			logger.info( "Surface %x destroyed", swapchain_info.swapchain_surface );
		}
	}
}

// ----------------------------------------------------------------------

static void backend_destroy( le_backend_o* self ) {
	static auto logger = LeLog( LOGGER_LABEL );

	if ( self->pipelineCache ) {
		using namespace le_backend_vk;
		le_pipeline_manager_i.destroy( self->pipelineCache );
		self->pipelineCache = nullptr;
	}

	VkDevice   device   = self->device->getVkDevice(); // may be nullptr if device was not created
	VkInstance instance = le_backend_vk::vk_instance_i.get_vk_instance( self->instance );

	// We must destroy the swapchain before self->mAllocator, as
	// the swapchain might have allocated memory using the backend's allocator,
	// and the allocator must still be alive for the swapchain to free objects
	// alloacted through it.

	for ( auto& s : self->swapchains ) {
		using namespace le_swapchain_vk;
		swapchain_i.destroy( s );
	}
	self->swapchains.clear();

	// modern swapchain: remove any leftover surfaces

	{
		for ( auto& [ swp_handle, swapchain_info ] : self->modern_swapchains ) {
			backend_destroy_modern_swapchain( self, swapchain_info );
		}

		self->modern_swapchains.clear();
	}

	//

	vkDeviceWaitIdle( self->device.get()->getVkDevice() );

	for ( auto& frameData : self->mFrames ) {

		using namespace le_backend_vk;

		// -- destroy per-frame data

		vkDestroyFence( device, frameData.frameFence, nullptr );

		for ( auto& swapchain_state : frameData.swapchain_state ) {
			vkDestroySemaphore( device, swapchain_state.presentComplete, nullptr );
			vkDestroySemaphore( device, swapchain_state.renderComplete, nullptr );
		}
		frameData.swapchain_state.clear();

		{
			for ( auto& cp : frameData.available_command_pools ) {
				vkDestroyCommandPool( device, cp->pool, nullptr );
				delete cp;
			}
			frameData.queue_submission_data.clear();
			frameData.available_command_pools.clear(); // cleanup stale pointers
		}

		for ( auto& d : frameData.descriptorPools ) {
			vkDestroyDescriptorPool( device, d, nullptr );
		}

		{
			// Destroy linear allocators, and the buffers allocated for them.
			assert( frameData.allocatorBuffers.size() == frameData.allocators.size() &&
			        frameData.allocatorBuffers.size() == frameData.allocations.size() &&
			        frameData.allocatorBuffers.size() == frameData.allocationInfos.size() );

			VkBuffer*      buffer     = frameData.allocatorBuffers.data();
			VmaAllocation* allocation = frameData.allocations.data();

			for ( auto allocator = frameData.allocators.begin(); allocator != frameData.allocators.end(); allocator++, buffer++, allocation++ ) {
				le_allocator_linear_i.destroy( *allocator );
				vmaDestroyBuffer( self->mAllocator, *buffer, *allocation );
			}

			frameData.allocators.clear();
			frameData.allocatorBuffers.clear();
			frameData.allocations.clear();
			frameData.allocationInfos.clear();
		}

		vmaDestroyPool( self->mAllocator, frameData.allocationPool );

		// destroy staging allocator
		le_staging_allocator_i.destroy( frameData.stagingAllocator );

		// remove any binned resources
		for ( auto& a : frameData.binnedResources ) {

			if ( a.second.info.isBuffer() ) {
				vkDestroyBuffer( device, a.second.as.buffer, nullptr );
			} else {
				vkDestroyImage( device, a.second.as.image, nullptr );
			}
			if ( a.second.info.isBlas() ) {
				vkDestroyBuffer( device, a.second.info.blasInfo.buffer, nullptr );
				vkDestroyAccelerationStructureKHR( device, a.second.as.blas, nullptr );
			}
			if ( a.second.info.isTlas() ) {
				vkDestroyBuffer( device, a.second.info.tlasInfo.buffer, nullptr );
				vkDestroyAccelerationStructureKHR( device, a.second.as.tlas, nullptr );
			}
			vmaFreeMemory( self->mAllocator, a.second.allocation );
		}
		frameData.binnedResources.clear();
	}

	self->mFrames.clear();

	// Remove any resources still alive in the backend.
	// At this point we're running single-threaded, so we can ignore the
	// ownership claim on allocatedResources.
	for ( auto& a : self->only_backend_allocate_resources_may_access.allocatedResources ) {

		switch ( a.second.info.type ) {
		case LeResourceType::eImage:
			vkDestroyImage( device, a.second.as.image, nullptr );
			break;
		case LeResourceType::eBuffer:
			vkDestroyBuffer( device, a.second.as.buffer, nullptr );
			break;
		case LeResourceType::eRtxBlas:
			vkDestroyBuffer( device, a.second.info.blasInfo.buffer, nullptr );
			vkDestroyAccelerationStructureKHR( device, a.second.as.blas, nullptr );
			break;
		case LeResourceType::eRtxTlas:
			vkDestroyBuffer( device, a.second.info.tlasInfo.buffer, nullptr );
			vkDestroyAccelerationStructureKHR( device, a.second.as.tlas, nullptr );
			break;
		default:
			assert( false && "Unknown resource type" );
		}

		vmaFreeMemory( self->mAllocator, a.second.allocation );
	}

	self->only_backend_allocate_resources_may_access.allocatedResources.clear();

	if ( self->mAllocator ) {
		vmaDestroyAllocator( self->mAllocator );
		self->mAllocator = nullptr;
	}

	// destroy window surface if there was a window surface
	backend_destroy_window_surfaces( self );

	{
		// destroy timeline semaphores which were allocated per-queue
		// and destroy BackendQueueInfo objects which were allocated on the free store
		for ( auto& q : self->queues ) {
			vkDestroySemaphore( self->device->getVkDevice(), q->semaphore, nullptr );
			q->semaphore = nullptr;
			delete ( q );
		}
		self->queues.clear();
	}

	// we must delete the device which was allocated from an instance
	// before we destroy the instance.
	self->device.reset();

	// Instance should be the last vulkan object to go.
	le_backend_vk::vk_instance_i.destroy( self->instance );

	delete self;
}

// ----------------------------------------------------------------------

static void backend_create_swapchains( le_backend_o* self, uint32_t num_settings, le_swapchain_settings_t* settings ) {

	using namespace le_swapchain_vk;
	static auto logger = LeLog( LOGGER_LABEL );

	assert( num_settings && "num_settings must not be zero" );

	for ( size_t i = 0; i != num_settings; i++, settings++ ) {
		le_swapchain_o* swapchain = nullptr;

		switch ( settings->type ) {

		case le_swapchain_settings_t::Type::LE_DIRECT_SWAPCHAIN: {
			// Create a windowless swapchain
			swapchain = swapchain_i.create( api->swapchain_direct_i, self, settings );
		} break;
		case le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN: {
			if ( settings->khr_settings.window != nullptr ) {
				backend_create_window_surface( self, settings );
				swapchain = swapchain_i.create( le_swapchain_vk::api->swapchain_khr_i, self, settings );
				break;
			} else {
				settings->type = le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN;
				logger.warn( "Automatically selected Image Swapchain as no window was specified" );
				settings->img_settings          = {};
				settings->img_settings.pipe_cmd = "";
			}

		} // deliberate fallthrough in case no window was specified
		case le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN: {
			// Create an image swapchain
			swapchain = swapchain_i.create( api->swapchain_img_i, self, settings );
		} break;
		}

		assert( swapchain );

		self->swapchainImageFormat.push_back( VkFormat( swapchain_i.get_surface_format( swapchain )->format ) );
		self->swapchainWidth.push_back( swapchain_i.get_image_width( swapchain ) );
		self->swapchainHeight.push_back( swapchain_i.get_image_height( swapchain ) );

		self->swapchains.push_back( swapchain );
	}
}

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// --- new swapchain interface
static le_swapchain_handle backend_add_swapchain( le_backend_o* self, le_swapchain_settings_t* const settings ) {
	le_swapchain_o* swapchain = nullptr;
	static auto     logger    = LeLog( LOGGER_LABEL );
	using namespace le_swapchain_vk;

	VkSurfaceKHR maybe_swapchain_surface = nullptr;

	switch ( settings->type ) {

	case le_swapchain_settings_t::Type::LE_DIRECT_SWAPCHAIN: {
		// Create a windowless swapchain
		swapchain = swapchain_i.create( api->swapchain_direct_i, self, settings );
	} break;
	case le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN: {
		if ( settings->khr_settings.window != nullptr ) {
			VkInstance instance = le_backend_vk::vk_instance_i.get_vk_instance( self->instance );
			maybe_swapchain_surface =
			    settings->khr_settings.vk_surface =
			        le_window::window_i.create_surface( settings->khr_settings.window, instance );
			swapchain = swapchain_i.create( le_swapchain_vk::api->swapchain_khr_i, self, settings );
			break;
		} else {
			settings->type = le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN;
			logger.warn( "Automatically selected Image Swapchain as no window was specified" );
			settings->img_settings          = {};
			settings->img_settings.pipe_cmd = "";
		}

	} // deliberate fallthrough in case no window was specified
	case le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN: {
		// Create an image swapchain
		swapchain = swapchain_i.create( api->swapchain_img_i, self, settings );
	} break;
	}

	assert( swapchain );

	modern_swapchain_data_t modern_swapchain_info{
	    .swapchain                = swapchain,
	    .swapchain_surface_format = *swapchain_i.get_surface_format( swapchain ),
	    .swapchain_surface        = maybe_swapchain_surface,
	    .height                   = swapchain_i.get_image_width( swapchain ),
	    .width                    = swapchain_i.get_image_height( swapchain ),
	};

	auto swapchain_index               = ++self->modern_swapchains_next_handle; // note pre-increment
	const auto& [ pair, was_emplaced ] = self->modern_swapchains.emplace( swapchain_index, modern_swapchain_info );

	if ( !was_emplaced ) {
		logger.error( "swapchain already existed: %x", swapchain_index );
	}

	// We must hand out a swapchain handle - the handle is just a (unique) number,
	// and we use it to remove the swapchain if needed.

	// TODO: add code to remove the swapchain when destroying

	return reinterpret_cast<le_swapchain_handle>( swapchain_index );
}

// ----------------------------------------------------------------------

static bool backend_remove_swapchain( le_backend_o* self, le_swapchain_handle swapchain_handle ) {

	auto it = self->modern_swapchains.find( reinterpret_cast<uint64_t>( swapchain_handle ) );

	if ( it != self->modern_swapchains.end() ) {
		backend_destroy_modern_swapchain( self, it->second );
		self->modern_swapchains.erase( it );
		return true;
	} else {
		return false;
	}
}
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

static size_t backend_get_data_frames_count( le_backend_o* self ) {
	return self->mFrames.size();
}

// ----------------------------------------------------------------------
// Returns the current swapchain width and height.
// Both values are cached, and re-calculated whenever the swapchain is set / or reset.
static void backend_get_swapchain_extent( le_backend_o* self, uint32_t index, uint32_t* p_width, uint32_t* p_height ) {
	*p_width  = self->swapchainWidth[ index ];
	*p_height = self->swapchainHeight[ index ];
}

// ----------------------------------------------------------------------

bool backend_get_swapchain_info( le_backend_o* self, uint32_t* count, uint32_t* p_width, uint32_t* p_height, le_img_resource_handle* p_handle ) {

	if ( *count < self->swapchain_resources.size() ) {
		*count = uint32_t( self->swapchain_resources.size() );
		return false;
	}

	// ---------| invariant: count is equal or larger than number of swapchain resources

	uint32_t num_items = *count = uint32_t( self->swapchain_resources.size() );

	memcpy( p_width, self->swapchainWidth.data(), sizeof( uint32_t ) * num_items );
	memcpy( p_height, self->swapchainHeight.data(), sizeof( uint32_t ) * num_items );
	memcpy( p_handle, self->swapchain_resources.data(), sizeof( le_resource_handle ) * num_items );

	return true;
}
// ----------------------------------------------------------------------

static le_img_resource_handle backend_get_swapchain_resource( le_backend_o* self, uint32_t index ) {
	return self->swapchain_resources[ index ];
}

// ----------------------------------------------------------------------

static uint32_t backend_get_swapchain_count( le_backend_o* self ) {
	return uint32_t( self->swapchain_resources.size() );
}

// ----------------------------------------------------------------------

static void backend_reset_swapchain( le_backend_o* self, uint32_t index ) {
	using namespace le_swapchain_vk;
	static auto logger = LeLog( LOGGER_LABEL );

	assert( index < self->swapchains.size() );

	swapchain_i.reset( self->swapchains[ index ], nullptr );

	logger.info( "Resetting swapchain with index: %d", index );

	// We must update our cached values for swapchain dimensions if the swapchain was reset.

	self->swapchainWidth[ index ]  = swapchain_i.get_image_width( self->swapchains[ index ] );
	self->swapchainHeight[ index ] = swapchain_i.get_image_height( self->swapchains[ index ] );
}

// ----------------------------------------------------------------------
/// \brief reset any swapchains for which at least one swapchain_state
/// did not present successfully
static void backend_reset_failed_swapchains( le_backend_o* self ) {
	using namespace le_swapchain_vk;

	for ( uint32_t i = 0; i != self->swapchains.size(); ++i ) {
		for ( auto const& f : self->mFrames ) {
			if ( false == f.swapchain_state[ i ].present_successful ||
			     false == f.swapchain_state[ i ].acquire_successful ) {
				backend_reset_swapchain( self, i );
				break;
			}
		}
	}
}
// ----------------------------------------------------------------------

/// \brief Declare a resource as a virtual buffer
/// \details This is an internal method. Virtual buffers are buffers which don't have individual
/// Vulkan buffer backing. Instead, they use their Frame's buffer for storage. Virtual buffers
/// are used to store Frame-local transient data such as values for shader parameters.
/// Each Encoder uses its own virtual buffer for such purposes.
static le_buf_resource_handle declare_resource_virtual_buffer( uint8_t index ) {

	le_buf_resource_handle resource =
	    le_renderer::renderer_i.produce_buf_resource_handle( "Encoder-Virtual", le_buf_resource_usage_flags_t::eIsVirtual, index );

	return resource;
}

// ----------------------------------------------------------------------

static VkDevice backend_get_vk_device( le_backend_o const* self ) {
	return self->device->getVkDevice();
};

// ----------------------------------------------------------------------

static VkPhysicalDevice backend_get_vk_physical_device( le_backend_o const* self ) {
	return self->device->getVkPhysicalDevice();
};

// ----------------------------------------------------------------------

static le_device_o* backend_get_le_device( le_backend_o* self ) {
	return *self->device;
}

// ----------------------------------------------------------------------

static le_backend_vk_instance_o* backend_get_instance( le_backend_o* self ) {
	return self->instance;
}

// ----------------------------------------------------------------------
// ffdecl.
static le_allocator_o** backend_create_transient_allocators( le_backend_o* self, size_t frameIndex, size_t numAllocators );
// ----------------------------------------------------------------------

// ----------------------------------------------------------------------

static inline uint32_t getMemoryIndexForGraphicsScratchBuffer( VmaAllocator const& allocator, uint32_t queueFamilyGraphics ) {

	// Find memory index for scratch buffer - we do this by pretending to create
	// an allocation.
	static const VkBufferUsageFlags BUFFER_USAGE_FLAGS_SCRATCH = defaults_get_buffer_usage_scratch();

	VkBufferCreateInfo bufferInfo{
	    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .pNext                 = nullptr, // optional
	    .flags                 = 0,       // optional
	    .size                  = 1,
	    .usage                 = BUFFER_USAGE_FLAGS_SCRATCH,
	    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0, // optional
	    .pQueueFamilyIndices   = nullptr,
	};

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	uint32_t memIndexScratchBufferGraphics = 0;
	vmaFindMemoryTypeIndexForBufferInfo( allocator, &bufferInfo, &allocInfo, &memIndexScratchBufferGraphics );
	return memIndexScratchBufferGraphics;
}

static inline uint32_t getMemoryIndexForGraphicsStagingBuffer( VmaAllocator const& allocator, uint32_t queueFamilyGraphics ) {

	// Find memory index for staging buffer - we do this by pretending to create
	// an allocation.

	VkBufferCreateInfo bufferInfo{
	    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .pNext                 = nullptr, // optional
	    .flags                 = 0,       // optional
	    .size                  = 1,
	    .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0, // optional
	    .pQueueFamilyIndices   = nullptr,
	};

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	uint32_t memIndexStagingBufferGraphics = 0;
	vmaFindMemoryTypeIndexForBufferInfo( allocator, &bufferInfo, &allocInfo, &memIndexStagingBufferGraphics );
	return memIndexStagingBufferGraphics;
}

// ----------------------------------------------------------------------

static void backend_initialise( le_backend_o* self, std::vector<char const*> requested_instance_extensions, std::vector<char const*> requested_device_extensions ) {
	using namespace le_backend_vk;
	self->instance = vk_instance_i.create( requested_instance_extensions.data(), uint32_t( requested_instance_extensions.size() ) );
	self->device   = std::make_unique<le::Device>( self->instance, requested_device_extensions.data(), uint32_t( requested_device_extensions.size() ) );

	{ // initialise queues
		uint32_t num_queues = 0;
		uint32_t previous_queue_family_index;
		vk_device_i.get_queues_info( *self->device, &num_queues, nullptr, nullptr, nullptr );
		std::vector<VkQueue>      queues( num_queues );
		std::vector<uint32_t>     queues_family_index( num_queues );
		std::vector<VkQueueFlags> queues_flags( num_queues );

		vk_device_i.get_queues_info( *self->device, &num_queues, queues.data(), queues_family_index.data(), queues_flags.data() );

		VkSemaphoreTypeCreateInfo type_info = {
		    .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		    .pNext         = nullptr, // optional
		    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		    .initialValue  = 0,
		};
		VkSemaphoreCreateInfo semaphore_create_info = {
		    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		    .pNext = &type_info, // optional
		    .flags = 0,          // optional
		};

		for ( uint32_t i = 0; i != num_queues; i++ ) {
			BackendQueueInfo* queue_info = new BackendQueueInfo{
			    .queue                = queues[ i ],
			    .queue_flags          = queues_flags[ i ],
			    .semaphore            = nullptr,
			    .semaphore_wait_value = 0,
			    .queue_family_index   = queues_family_index[ i ],
			};

			{
				// If not all queues are from the same queue family, then we must
				// keep track of queue ownership for resources
				if ( i > 0 && queues_family_index[ i ] != previous_queue_family_index ) {
					self->must_track_resources_queue_family_ownership = true;
				}
				previous_queue_family_index = queues_family_index[ i ];
			}

			// Fetch the first graphics enabled queue and make this our default graphics queue -
			// this queue will be used for swapchain present.
			if ( self->queueFamilyIndexGraphics == uint32_t( ~0 ) && ( queues_flags[ i ] & VK_QUEUE_GRAPHICS_BIT ) ) {
				self->queue_default_graphics_idx = i;
				self->queueFamilyIndexGraphics   = queues_family_index[ i ];
			}
			// create one timeline semaphore for every queue.
			vkCreateSemaphore( self->device->getVkDevice(), &semaphore_create_info, nullptr, &queue_info->semaphore );
			self->queues.push_back( queue_info );

			// try to store the queue index together with the queue family index - this will
			// only add an element if a queue family is not already present in the map.
			self->default_queue_for_family_index.try_emplace( queue_info->queue_family_index, i );
		}
	}

	if ( self->must_track_resources_queue_family_ownership ) {
		le::Log( LOGGER_LABEL ).info( "Multiple queue families detected - tracking queue ownership per-resource." );
	}
	self->pipelineCache = le_pipeline_manager_i.create( *self->device );
}
// ----------------------------------------------------------------------

static void backend_create_main_allocator( VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VmaAllocator* allocator ) {
	VmaAllocatorCreateInfo createInfo{};

	// Enable shader_device_address for scratch buffer, if raytracing feature is requested
	auto settings = le_backend_vk::api->backend_settings_singleton;
	if ( settings->requested_device_features.vk_12.bufferDeviceAddress ) {
		createInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	}
	createInfo.device                      = device;
	createInfo.frameInUseCount             = 0;
	createInfo.physicalDevice              = physical_device;
	createInfo.preferredLargeHeapBlockSize = 0; // set to default, currently 256 MB
	createInfo.instance                    = instance;

	vmaCreateAllocator( &createInfo, allocator );
}

// ----------------------------------------------------------------------

static void backend_setup( le_backend_o* self ) {

	using namespace le_backend_vk;

	auto settings = api->backend_settings_singleton;

	assert( settings );
	if ( settings == nullptr ) {
		le::Log( LOGGER_LABEL ).error( "FATAL: Must specify settings for backend." );
		exit( 1 );
	} else {
		// Freeze settings, as these will be the settings that apply for the lifetime of the backend.
		settings->readonly = true;
	}

	// -- initialise backend

	backend_initialise( self, settings->required_instance_extensions, settings->required_device_extensions );

	VkDevice         vkDevice         = self->device->getVkDevice();
	VkPhysicalDevice vkPhysicalDevice = self->device->getVkPhysicalDevice();
	VkInstance       vkInstance       = vk_instance_i.get_vk_instance( self->instance );

	// -- query rtx properties, and store them with backend
	self->device->getRaytracingProperties( &static_cast<VkPhysicalDeviceRayTracingPipelinePropertiesKHR&>( self->ray_tracing_props ) );

	// -- Create allocator for backend vulkan memory
	// we do this here, because swapchain might want to already use the allocator.

	backend_create_main_allocator( vkInstance, vkPhysicalDevice, vkDevice, &self->mAllocator );

	// -- create swapchain if requested

	backend_create_swapchains( self, uint32_t( settings->swapchain_settings.size() ), settings->swapchain_settings.data() );

	// -- setup backend memory objects

	auto frameCount = 3; // TODO: WE MUST INFER THE CORRECT NUMBER OF DATA FRAMES - FOR NOW, SET TO 3

	self->mFrames.reserve( frameCount );

	uint32_t memIndexScratchBufferGraphics = getMemoryIndexForGraphicsScratchBuffer( self->mAllocator, self->queueFamilyIndexGraphics ); // used for transient command buffer allocations

	assert( vkDevice ); // device must come from somewhere! It must have been introduced to backend before, or backend must create device used by everyone else...

	{
		// Create image handles for swapchain images

		self->swapchain_resources.reserve( self->swapchains.size() );
		char swapchain_name[ 64 ];

		for ( uint32_t j = 0; j != self->swapchains.size(); j++ ) {
			snprintf( swapchain_name, sizeof( swapchain_name ), "Le_Swapchain_Image_Handle[%d]", j );
			self->swapchain_resources.emplace_back(
			    le_renderer::renderer_i.produce_img_resource_handle(
			        swapchain_name, 0, nullptr, le_img_resource_usage_flags_t::eIsRoot ) );
		}

		// assert( !self->swapchain_resources.empty() && "swapchain_resources must not be empty" );
	}

	for ( size_t i = 0; i != frameCount; ++i ) {

		// -- Set up per-frame resources

		BackendFrameData frameData{};
		frameData.frameNumber = i;

		{
			// Set up swapchain state per frame
			//
			frameData.swapchain_state.resize( self->swapchains.size() );
			VkSemaphoreCreateInfo const create_info = {
			    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			    .pNext = nullptr, // optional
			    .flags = 0,       // optional
			};

			for ( auto& state : frameData.swapchain_state ) {
				vkCreateSemaphore( vkDevice, &create_info, nullptr, &state.presentComplete );
				vkCreateSemaphore( vkDevice, &create_info, nullptr, &state.renderComplete );
			}
		}

		{
			VkFenceCreateInfo create_info = {
			    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			    .pNext = nullptr, // optional
			    .flags = 0,       // optional
			};

			vkCreateFence( vkDevice, &create_info, nullptr, &frameData.frameFence ); // frence starts out as sigmalled
		}

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

	self->totalFrameCount = frameCount; // running total of frames

	{
		// We want to make sure to have at least one allocator.
		size_t num_allocators = std::max<size_t>( 1, settings->concurrency_count );

		for ( size_t i = 0; i != frameCount; ++i ) {
			// -- create linear allocators for each frame
			backend_create_transient_allocators( self, i, num_allocators );
		}
	}

	{
		// Set default image formats

		// FIXME: we should make sure that these default formats are per-swapchain.

		using namespace le_backend_vk;

		// assert( !self->swapchainImageFormat.empty() && "must have at least one swapchain image format available." );

		self->defaultFormatColorAttachment = le::Format::eB8G8R8A8Unorm;
		// static_cast<le::Format>( self->swapchainImageFormat[ 0 ] );
		self->defaultFormatDepthStencilAttachment = le::Format::eD24UnormS8Uint;
		// static_cast<le::Format>( VkFormat( *vk_device_i.get_default_depth_stencil_format( *self->device ) ) );

		// We hard-code default format for sampled images, since this is the most likely
		// format we will encounter bitmaps to be encoded in, and there is no good way
		// to infer it.
		self->defaultFormatSampledImage = le::Format::eR8G8B8A8Unorm;
	}
}

// ----------------------------------------------------------------------
// Add image attachments to leRenderPass
// Update syncchain for images affected.
static void le_renderpass_add_attachments( le_renderpass_o const* pass, BackendRenderPass& currentPass, BackendFrameData& frame, le::SampleCountFlagBits const& sampleCount ) {

	using namespace le_renderer;

	// FIXME: We must ensure that color attachments are listed before possible depth/stencil attachment,
	// because if a resolve is required, attachment reference indices will be off by one.

	auto numSamplesLog2 = get_sample_count_log_2( uint32_t( sampleCount ) );

	le_image_attachment_info_t const* pImageAttachments   = nullptr;
	le_img_resource_handle const*     pResources          = nullptr;
	size_t                            numImageAttachments = 0;

	renderpass_i.get_image_attachments( pass, &pImageAttachments, &pResources, &numImageAttachments );
	for ( size_t i = 0; i != numImageAttachments; ++i ) {

		auto const& image_attachment_info = pImageAttachments[ i ];

		le_img_resource_handle img_resource = pResources[ i ];

		// If we're dealing with a multisampled renderpass, the color attachments must be mapped
		// to "virtual" image resources which share everything with the original image resource
		// apart from the sample count.
		//
		// The "original" image resource will then be mapped to a resolve attachment further down.
		//
		if ( numSamplesLog2 != 0 ) {
			img_resource = le_renderer::renderer_i.produce_img_resource_handle(
			    img_resource->data->debug_name, uint8_t( numSamplesLog2 ), img_resource, 0 );
		}

		auto& syncChain = frame.syncChainTable[ img_resource ];

		assert( !syncChain.empty() && "SyncChain must not be empty" );

		auto const& attachmentFormat = le::Format( frame.availableResources[ img_resource ].info.imageInfo.format );

		bool isDepth = false, isStencil = false;
		le_format_get_is_depth_stencil( attachmentFormat, isDepth, isStencil );
		bool isDepthStencil = isDepth || isStencil;

		AttachmentInfo* currentAttachment =
		    currentPass.attachments +
		    currentPass.numColorAttachments +
		    currentPass.numDepthStencilAttachments +
		    currentPass.numResolveAttachments;

		if ( isDepthStencil ) {
			currentPass.numDepthStencilAttachments++;
			currentAttachment->type = AttachmentInfo::Type::eDepthStencilAttachment;
		} else {
			currentPass.numColorAttachments++;
			currentAttachment->type = AttachmentInfo::Type::eColorAttachment;
		}

		currentAttachment->resource   = img_resource;
		currentAttachment->format     = le::Format( attachmentFormat );
		currentAttachment->numSamples = sampleCount;
		currentAttachment->loadOp     = image_attachment_info.loadOp;
		currentAttachment->storeOp    = image_attachment_info.storeOp;
		currentAttachment->clearValue = image_attachment_info.clearValue;

		{
			// track resource state before entering a subpass

			auto&         previousSyncState = syncChain.back();
			ResourceState beforeFirstUse{ previousSyncState };

			if ( currentAttachment->loadOp == le::AttachmentLoadOp::eLoad ) {
				// we must now specify which stages need to be visible for which coming memory access
				if ( isDepthStencil ) {
					beforeFirstUse.visible_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
					beforeFirstUse.stage          = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;

				} else {
					// we need to make visible the information from color attachment output stage
					// to anyone using read or write on the color attachment.
					beforeFirstUse.visible_access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT; // note that read does only need to be made visible, not available
					beforeFirstUse.stage          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				}
			} else if ( currentAttachment->loadOp == le::AttachmentLoadOp::eClear ) {
				// resource.loadOp must be either CLEAR / or DONT_CARE
				beforeFirstUse.stage          = isDepthStencil
				                                    ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
				                                    : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				beforeFirstUse.visible_access = VkAccessFlagBits2( 0 );
			}

			currentAttachment->initialStateOffset = uint16_t( syncChain.size() );
			syncChain.emplace_back( std::move( beforeFirstUse ) ); // attachment initial state for a renderpass - may be loaded/cleared on first use
			                                                       // * sync state: ready for load/store *
		}

		{
			// track resource state before subpass

			auto&         previousSyncState = syncChain.back();
			ResourceState beforeSubpass{ previousSyncState };

			if ( image_attachment_info.loadOp == le::AttachmentLoadOp::eLoad ) {
				// resource.loadOp most be LOAD

				// we must now specify which stages need to be visible for which coming memory access
				if ( isDepthStencil ) {
					beforeSubpass.visible_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					beforeSubpass.stage          = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
					beforeSubpass.layout         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				} else {
					// we need to make visible the information from color attachment output stage
					// to anyone using read or write on the color attachment.
					beforeSubpass.visible_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
					beforeSubpass.stage          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
					beforeSubpass.layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				}

			} else {

				// load op is either CLEAR, or DONT_CARE

				if ( isDepthStencil ) {
					beforeSubpass.visible_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					beforeSubpass.stage          = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
					beforeSubpass.layout         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				} else {
					beforeSubpass.visible_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
					beforeSubpass.stage          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
					beforeSubpass.layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				}
			}

			syncChain.emplace_back( std::move( beforeSubpass ) );
		}

		// TODO: here, go through command instructions for renderpass and update resource chain if necessary.
		// If resource is modified by commands inside the renderpass, this needs to be added to the sync chain here.

		// Whichever next resource state will be in the sync chain will be the resource state we should transition to
		// when defining the last_subpass_to_external dependency
		// which is why, optimistically, we designate the index of the next, not yet written state here -
		currentAttachment->finalStateOffset = uint16_t( syncChain.size() );

	} // end foreach image attachment

	// -- Check whether this is a multisampled renderpass.
	// If not, we're done.

	if ( numSamplesLog2 == 0 ) {
		return;
	}

	// ----------| invariant: this is a multisampled renderpass.

	// We must add resolve attachments for each image, so that
	// each image at sample_count_log2 == 0 is placed into a resolve
	// attachment.

	for ( size_t i = 0; i != numImageAttachments; ++i ) {

		auto const& image_attachment_info = pImageAttachments[ i ];

		le_img_resource_handle img_resource = pResources[ i ];
		auto&                  syncChain    = frame.syncChainTable[ img_resource ];

		auto const& attachmentFormat = le::Format( frame.availableResources[ img_resource ].info.imageInfo.format );

		bool isDepth = false, isStencil = false;
		le_format_get_is_depth_stencil( attachmentFormat, isDepth, isStencil );
		bool isDepthStencil = isDepth || isStencil;

		AttachmentInfo* currentAttachment = currentPass.attachments +
		                                    currentPass.numColorAttachments +
		                                    currentPass.numDepthStencilAttachments +
		                                    currentPass.numResolveAttachments;

		// we're dealing with a resolve attachment here.
		currentPass.numResolveAttachments++;

		currentAttachment->resource   = img_resource;
		currentAttachment->format     = le::Format( attachmentFormat );
		currentAttachment->numSamples = le::SampleCountFlagBits::e1; // this is a requirement for resolve passes.
		currentAttachment->loadOp     = le::AttachmentLoadOp::eDontCare;
		currentAttachment->storeOp    = image_attachment_info.storeOp;
		currentAttachment->clearValue = image_attachment_info.clearValue;
		currentAttachment->type       = AttachmentInfo::Type::eResolveAttachment;

		{
			// track resource state before entering a subpass

			auto& previousSyncState = syncChain.back();
			auto  beforeFirstUse{ previousSyncState };

			currentAttachment->initialStateOffset = uint16_t( syncChain.size() );
			syncChain.emplace_back( beforeFirstUse ); // attachment initial state for a renderpass - may be loaded/cleared on first use
			                                          // * sync state: ready for load/store *
		}

		{
			// track resource state before subpass

			auto& previousSyncState = syncChain.back();
			auto  beforeSubpass{ previousSyncState };

			{
				// load op is either CLEAR, or DONT_CARE

				if ( isDepthStencil ) {
					beforeSubpass.visible_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
					beforeSubpass.stage          = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
					beforeSubpass.layout         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				} else {
					beforeSubpass.visible_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
					beforeSubpass.stage          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
					beforeSubpass.layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				}
			}

			syncChain.emplace_back( std::move( beforeSubpass ) );
		}

		// TODO: here, go through command instructions for renderpass and update resource chain if necessary.
		// If resource is modified by commands inside the renderpass, this needs to be added to the sync chain here.

		// Whichever next resource state will be in the sync chain will be the resource state we should transition to
		// when defining the last_subpass_to_external dependency
		// which is why, optimistically, we designate the index of the next, not yet written state here -
		currentAttachment->finalStateOffset = uint16_t( syncChain.size() );

	} // end foreach image attachment
}

static std::string to_string_le_access_flags2( const le::AccessFlags2& tp ) {
	uint64_t    flags = tp;
	std::string result;
	int         bit_pos = 0;
	while ( flags ) {
		if ( flags & 1 ) {
			if ( false == result.empty() ) {
				result.append( " | " );
			}
			result.append( to_str( le::AccessFlagBits2( 1ULL << bit_pos ) ) );
		}
		flags >>= 1;
		bit_pos++;
	}
	return result;
}
// ----------------------------------------------------------------------
// Updates sync chain for resourcess referenced in rendergraph
// each renderpass contains offsets into sync chain for given resource used by renderpass.
// resource sync state for images used as renderpass attachments is chosen so that they
// can be implicitly synced using subpass dependencies.
static void le_renderpass_add_explicit_sync( le_renderpass_o const* pass, BackendRenderPass& currentPass, BackendFrameData::sync_chain_table_t& syncChainTable ) {
	using namespace le_renderer;
	le_resource_handle const* resources        = nullptr;
	le::AccessFlags2 const*   resources_access = nullptr;
	size_t                    resources_count  = 0;
	renderpass_i.get_used_resources( pass, &resources, &resources_access, &resources_count );

	currentPass.resources.assign( resources, resources + resources_count );

	auto get_stage_flags_based_on_renderpass_type = []( le::QueueFlagBits const& rp_type ) -> VkPipelineStageFlags2 {
		// write_stage depends on current renderpass type.
		switch ( rp_type ) {
		case le::QueueFlagBits::eTransfer:
			return VK_PIPELINE_STAGE_2_TRANSFER_BIT; // stage for transfer pass
		case le::QueueFlagBits::eGraphics:
			return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT; // earliest stage for draw pass
		case le::QueueFlagBits::eCompute:
			return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; // stage for compute pass

		default:
			assert( false ); // unreachable - we don't know what kind of stage we're in.
			return VkPipelineStageFlags2();
		}
	};

	for ( size_t i = 0; i != resources_count; ++i ) {
		auto const& resource = resources[ i ];

		auto& syncChain = syncChainTable[ resource ];
		assert( !syncChain.empty() ); // must not be empty - this resource must exist, and have an initial sync state

		ExplicitSyncOp syncOp{};

		syncOp.resource                  = resource;
		syncOp.active                    = true;
		syncOp.sync_chain_offset_initial = uint32_t( syncChain.size() - 1 );

		ResourceState requestedState{}; // State we want our image to be in when pass begins.

		// If resource is an image and it either gets sampled or used as storage,
		// add an entry to the sync chain for this resource representing the state
		// that we expect the resource to be in when the pass begins.
		//
		if ( resource->data->type == LeResourceType::eImage ) {

			// le::Log( LOGGER_LABEL ).info( " resource: %40s, access { %-60s }", resource->data->debug_name, to_string_le_access_flags2( resources_access[ i ] ).c_str() );

			if ( resources_access[ i ] & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT ) {
				requestedState.visible_access = resources_access[ i ];
				requestedState.stage          = get_stage_flags_based_on_renderpass_type( currentPass.type );
				requestedState.layout         = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			} else if ( resources_access[ i ] & ( VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
			                                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
			                                      VK_ACCESS_2_SHADER_READ_BIT |
			                                      VK_ACCESS_2_SHADER_WRITE_BIT ) ) {
				requestedState.visible_access = resources_access[ i ];
				requestedState.stage          = get_stage_flags_based_on_renderpass_type( currentPass.type );
				requestedState.layout         = VK_IMAGE_LAYOUT_GENERAL;
			}

			else {
				continue;
			}

		} else {
			// Resources other than Images are ignored.

			// TODO: whe should probably process buffers here, as skipping the loop here
			// means that Buffers don't ever get added to explicit_sync_ops.
			continue;
		}

		// -- we must add an entry to the sync chain to signal the state after change
		syncChain.emplace_back( requestedState );

		syncOp.sync_chain_offset_final = uint32_t( syncChain.size() - 1 );

		// -- we must add an explicit sync op so that the change happens before the pass - this applies to
		// passes which don't do implicit syncing, such as compute or transfer passes.
		currentPass.explicit_sync_ops.emplace_back( syncOp );
	}
}

static void frame_track_resource_state( BackendFrameData& frame, le_renderpass_o** ppPasses, size_t numRenderPasses, const std::vector<le_img_resource_handle>& backbufferImageHandles ) {

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
	static auto logger = LeLog( LOGGER_LABEL );

	auto& syncChainTable = frame.syncChainTable;

	for ( auto& swapchain_image : backbufferImageHandles ) {

		// -- backbuffer has their sync state changed outside of our frame graph
		// because submitting to the swapchain changes its sync state.
		// We must adjust the backbuffer sync-chain table to account for this.

		le_img_resource_handle backbuffer = swapchain_image;

		auto backbufferIt = syncChainTable.find( backbuffer );
		if ( backbufferIt != syncChainTable.end() ) {
			auto& backbufferState          = backbufferIt->second.front();
			backbufferState.stage          = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; // we need this, since semaphore waits on this stage
			backbufferState.visible_access = VkAccessFlagBits2( 0 );                          // semaphore took care of availability - we can assume memory is already available
		} else {
			logger.warn( "No reference to backbuffer found in renderpass" );
		}
	}

	using namespace le_renderer;

	frame.passes.reserve( numRenderPasses );

	for ( auto pass = ppPasses; pass != ppPasses + numRenderPasses; pass++ ) {

		BackendRenderPass currentPass{};

		renderpass_i.get_queue_sumbission_info( *pass, &currentPass.type, &currentPass.root_passes_affinity );

		memcpy( currentPass.debugName, renderpass_i.get_debug_name( *pass ), sizeof( currentPass.debugName ) );

		renderpass_i.get_framebuffer_settings( *pass, &currentPass.width, &currentPass.height, &currentPass.sampleCount );

		// Find explicit sync ops needed for resources which are not attachments
		//
		le_renderpass_add_explicit_sync( *pass, currentPass, syncChainTable );

		// Iterate over all image attachments
		le_renderpass_add_attachments( *pass, currentPass, frame, currentPass.sampleCount );

		// Note that we "steal" the encoder from the renderer pass -
		// it becomes now our (the backend's) job to destroy it.
		currentPass.encoder = renderpass_i.steal_encoder( *pass );

		frame.passes.emplace_back( std::move( currentPass ) );
	} // end for all passes

	for ( auto& s_entry : syncChainTable ) {
		const auto& resource_handle = s_entry.first;
		auto&       sync_chain      = s_entry.second;

		auto finalState{ sync_chain.back() };

		if ( std::find( backbufferImageHandles.begin(), backbufferImageHandles.end(), resource_handle ) != backbufferImageHandles.end() ) {
			finalState.stage          = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT; // Everything: Drain the pipeline
			finalState.visible_access = VK_ACCESS_2_MEMORY_READ_BIT;            // Cached memory must be made visible to memory read access ...
			finalState.layout         = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;        // ... so that it can perform layout transition to present_src
		} else {
			// we mimick implicit dependency here, which exists for a final subpass
			// see p.210 vk spec (chapter 7, render pass)
			finalState.stage          = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			finalState.visible_access = VkAccessFlagBits2( 0 );
		}

		sync_chain.emplace_back( std::move( finalState ) );
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
	// Note that only resources of type image may be implicitly synced.

	typedef std::unordered_map<le_resource_handle, uint32_t> SyncChainMap;

	SyncChainMap max_sync_index;

	auto insert_if_greater = [ &max_sync_index ]( le_resource_handle const& key, uint32_t value ) {
		// Updates map entry to highest value
		auto& element = max_sync_index[ key ];
		element       = std::max( element, value );
	};

	for ( auto& p : frame.passes ) {

		// Check barrier sync chain index against current sync index.
		//
		// If barrier sync index is higher, barrier must be issued. Otherwise,
		// barrier must be removed, as subpass dependency already takes care
		// of synchronisation implicitly.

		for ( auto& op : p.explicit_sync_ops ) {

			if ( op.resource->data->type != LeResourceType::eImage ) {
				continue;
			}

			// ---------| invariant: only image resources need checking
			//
			// This is because only image may potentially be synchronised implicitly via
			// subpass dependencies. No such mechanism exists for buffers.
			//
			// We can skip checks for buffer barriers, as we assume they are
			// all needed.

			auto found_it = max_sync_index.find( op.resource );
			if ( found_it != max_sync_index.end() && found_it->second >= op.sync_chain_offset_final ) {
				// found an element, and current index is already higher than barrier index.
				op.active = false;
			} else {
				// no element found, or max index is smaller.
				op.active = true;
				// store the current max index, then.
				max_sync_index[ op.resource ] = op.sync_chain_offset_final;
			}
		}

		// Update max_sync_index, so that it contains the maximum sync chain index for each
		// attachment image resource used in the current pass.
		const size_t numAttachments = p.numColorAttachments +
		                              p.numDepthStencilAttachments +
		                              p.numResolveAttachments;

		for ( size_t a = 0; a != numAttachments; a++ ) {
			auto const& attachmentInfo = p.attachments[ a ];
			insert_if_greater( attachmentInfo.resource, attachmentInfo.finalStateOffset );
		}
	}
}

// ----------------------------------------------------------------------

/// \brief polls frame fence, returns true if fence has been crossed, false otherwise.
static bool backend_poll_frame_fence( le_backend_o* self, size_t frameIndex ) {
	auto&    frame  = self->mFrames[ frameIndex ];
	VkDevice device = self->device->getVkDevice();

	// Non-blocking, polling
	// auto result = device.getFenceStatus( {frame.frameFence} );

	// NOTE: this may block.
	auto result = vkWaitForFences( device, 1, &frame.frameFence, true, 100'000'000 );
	// le::Log( LOGGER_LABEL ).info( "=[%3d]== frame fence cleared", frameIndex );

	if ( result != VK_SUCCESS ) {
		return false;
	} else {
		return true;
	}
}

// ----------------------------------------------------------------------
/// \brief: Frees all frame local resources
/// \preliminary: frame fence must have been crossed.
static bool backend_clear_frame( le_backend_o* self, size_t frameIndex ) {

	static auto logger = LeLog( LOGGER_LABEL );

	using namespace le_backend_vk;

	auto&    frame  = self->mFrames[ frameIndex ];
	VkDevice device = self->device->getVkDevice();

	// le::Log( LOGGER_LABEL ).info( "=[%3d]== clear frame ", frameIndex );

	// -------- Invariant: fence has been crossed, all resources protected by fence
	//          can now be claimed back.

	vkResetFences( device, 1, &frame.frameFence );

	// -- reset all frame-local sub-allocators
	for ( auto& alloc : frame.allocators ) {
		le_allocator_linear_i.reset( alloc );
	}

	// -- reset frame-local staging allocator
	le_staging_allocator_i.reset( frame.stagingAllocator );

	// -- remove any texture references
	frame.textures_per_pass.clear();

	// -- remove any image view references
	frame.imageViews.clear();

	// -- remove any frame-local copy of allocated resources
	frame.availableResources.clear();

	frame.must_create_queues_dot_graph = false;
	frame.debug_root_passes_names.clear();

	for ( auto& d : frame.descriptorPools ) {
		vkResetDescriptorPool( device, d, VkDescriptorPoolResetFlags() );
	}

	{ // clear resources owned exclusively by this frame

		for ( auto& r : frame.ownedResources ) {
			switch ( r.type ) {
			case AbstractPhysicalResource::eBuffer:
				vkDestroyBuffer( device, r.asBuffer, nullptr );
				break;
			case AbstractPhysicalResource::eFramebuffer:
				vkDestroyFramebuffer( device, r.asFramebuffer, nullptr );
				break;
			case AbstractPhysicalResource::eImage:
				vkDestroyImage( device, r.asImage, nullptr );
				break;
			case AbstractPhysicalResource::eImageView:
				vkDestroyImageView( device, r.asImageView, nullptr );
				break;
			case AbstractPhysicalResource::eRenderPass:
				vkDestroyRenderPass( device, r.asRenderPass, nullptr );
				break;
			case AbstractPhysicalResource::eSampler:
				vkDestroySampler( device, r.asSampler, nullptr );
				break;

			case AbstractPhysicalResource::eUndefined:
				logger.warn( "%s: abstract physical resource has unknown type (%p) and cannot be deleted. leaking...", __PRETTY_FUNCTION__, r.type );
				break;
			}
		}
		frame.ownedResources.clear();
	}

	for ( auto& cp : frame.available_command_pools ) {
		if ( cp->is_used ) {
			vkFreeCommandBuffers( device, cp->pool, uint32_t( cp->buffers.size() ), cp->buffers.data() ); // shouldn't clearing the pool implicitly free all command buffers allocated from the pool?
			vkResetCommandPool( device, cp->pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT );
			cp->is_used = false; // mark this command pool as available for recycling.
		}
		// Note that we don't clear `cp->buffers` - no need to do this as buffers
		// get resized and overwritten whenever a pool gets re-used.
	}
	frame.queue_submission_data.clear();

	frame.physicalResources.clear();
	frame.syncChainTable.clear();

	for ( auto& f : frame.passes ) {
		if ( f.encoder ) {
			using namespace le_renderer;
			encoder_i.destroy( f.encoder );
			f.encoder = nullptr;
		}
	}
	frame.passes.clear();
	frame.frameNumber = self->totalFrameCount++; // note post-increment

	return true;
};
// ----------------------------------------------------------------------
// we use this to mask out any reads in srcAccess, as it never makes sense to flush reads
static constexpr auto ANY_WRITE_VK_ACCESS_2_FLAGS =
    ( VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
      VK_ACCESS_2_HOST_WRITE_BIT |
      VK_ACCESS_2_MEMORY_WRITE_BIT |
      VK_ACCESS_2_SHADER_WRITE_BIT |
      VK_ACCESS_2_TRANSFER_WRITE_BIT |
      VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_NV |
      VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT );

static void backend_create_renderpasses( BackendFrameData& frame, VkDevice& device ) {

	static auto logger = LeLog( LOGGER_LABEL );

	// create renderpasses
	const auto& syncChainTable = frame.syncChainTable;

	// Note: This should be trivial to parallelize.
	for ( auto& pass : frame.passes ) {

		// The rest of this loop only concerns draw passes
		//
		if ( pass.type != le::QueueFlagBits::eGraphics ) {
			continue;
		}

		// ---------| Invariant: current pass is a draw pass.

		std::vector<VkAttachmentDescription2> attachments;
		attachments.reserve( pass.numColorAttachments + pass.numDepthStencilAttachments );

		std::vector<VkAttachmentReference2> colorAttachmentReferences;
		std::vector<VkAttachmentReference2> resolveAttachmentReferences;
		VkAttachmentReference2*             dsAttachmentReference = nullptr;

		// We must accumulate these flags over all attachments - they are the
		// union of all flags required by all attachments in a pass.
		VkPipelineStageFlags2 srcStageFromExternalFlags  = 0;
		VkPipelineStageFlags2 dstStageFromExternalFlags  = 0;
		VkAccessFlags2        srcAccessFromExternalFlags = 0;
		VkAccessFlags2        dstAccessFromExternalFlags = 0;

		VkPipelineStageFlags2 srcStageToExternalFlags  = 0;
		VkPipelineStageFlags2 dstStageToExternalFlags  = 0;
		VkAccessFlags2        srcAccessToExternalFlags = 0;
		VkAccessFlags2        dstAccessToExternalFlags = 0;

		if ( LE_PRINT_DEBUG_MESSAGES ) {
			logger.info( "* Renderpass: '%s'", pass.debugName );
			logger.info( " %40s : %30s : %30s : %30s", "Attachment", "Layout initial", "Layout subpass", "Layout final" );
		}

		auto const attachments_end = pass.attachments +
		                             pass.numColorAttachments +
		                             pass.numDepthStencilAttachments +
		                             pass.numResolveAttachments;

		for ( AttachmentInfo const* attachment = pass.attachments; attachment != attachments_end; attachment++ ) {

			auto& syncChain = syncChainTable.at( attachment->resource );

			const auto& syncInitial = syncChain.at( attachment->initialStateOffset );
			const auto& syncSubpass = syncChain.at( attachment->initialStateOffset + 1 );
			const auto& syncFinal   = syncChain.at( attachment->finalStateOffset );

			bool isDepth   = false;
			bool isStencil = false;
			le_format_get_is_depth_stencil( attachment->format, isDepth, isStencil );

			VkAttachmentDescription2 attachmentDescription{
			    .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
			    .pNext          = nullptr,                        // optional
			    .flags          = VkAttachmentDescriptionFlags(), // optional
			    .format         = VkFormat( attachment->format ),
			    .samples        = VkSampleCountFlagBits( attachment->numSamples ),
			    .loadOp         = VkAttachmentLoadOp( attachment->loadOp ),
			    .storeOp        = VkAttachmentStoreOp( attachment->storeOp ),
			    .stencilLoadOp  = isStencil ? VkAttachmentLoadOp( attachment->loadOp ) : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			    .stencilStoreOp = isStencil ? VkAttachmentStoreOp( attachment->storeOp ) : VK_ATTACHMENT_STORE_OP_DONT_CARE,
			    .initialLayout  = syncInitial.layout,
			    .finalLayout    = syncFinal.layout,
			};

			if ( LE_PRINT_DEBUG_MESSAGES ) {
				logger.info( " %38s@%d : %30s → %30s → %30s | sync chain indices: %4d : %4d : %4d",
				             attachment->resource->data->debug_name, 1 << attachment->resource->data->num_samples,
				             to_str_vk_image_layout( syncInitial.layout ),
				             to_str_vk_image_layout( syncSubpass.layout ),
				             to_str_vk_image_layout( syncFinal.layout ),
				             attachment->initialStateOffset,
				             attachment->initialStateOffset + 1,
				             attachment->finalStateOffset );
			}

			attachments.emplace_back( attachmentDescription );

			switch ( attachment->type ) {
			case AttachmentInfo::Type::eDepthStencilAttachment:
				dsAttachmentReference = new VkAttachmentReference2{
				    .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
				    .pNext      = nullptr, // optional
				    .attachment = uint32_t( attachments.size() - 1 ),
				    .layout     = syncSubpass.layout,
				    .aspectMask = 0,
				}; // cleanup at the end of loop
				break;
			case AttachmentInfo::Type::eColorAttachment:
				colorAttachmentReferences.push_back( {
				    .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
				    .pNext      = nullptr, // optional
				    .attachment = uint32_t( attachments.size() - 1 ),
				    .layout     = syncSubpass.layout,
				    .aspectMask = 0,
				} );
				break;
			case AttachmentInfo::Type::eResolveAttachment:
				resolveAttachmentReferences.push_back( {
				    .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
				    .pNext      = nullptr, // optional
				    .attachment = uint32_t( attachments.size() - 1 ),
				    .layout     = syncSubpass.layout,
				    .aspectMask = 0,
				} );
				break;
			}

			srcStageFromExternalFlags |= syncInitial.stage;
			dstStageFromExternalFlags |= syncSubpass.stage;
			srcAccessFromExternalFlags |= ( syncInitial.visible_access & ANY_WRITE_VK_ACCESS_2_FLAGS );
			dstAccessFromExternalFlags |= syncSubpass.visible_access; // & ~(syncInitial.visible_access );
			// this would make only changes in availability operations happen. it should only happen if there are no src write_access_flags. we leave this out so as to give the driver more info

			// TODO: deal with other subpasses ...

			srcStageToExternalFlags |= syncChain.at( attachment->finalStateOffset - 1 ).stage;
			dstStageToExternalFlags |= syncFinal.stage;
			srcAccessToExternalFlags |= ( syncChain.at( attachment->finalStateOffset - 1 ).visible_access & ANY_WRITE_VK_ACCESS_2_FLAGS );
			dstAccessToExternalFlags |= syncFinal.visible_access;

			if ( 0 == uint64_t( srcStageFromExternalFlags ) ) {
				// Ensure that the stage mask is valid if no src stage was specified.
				srcStageFromExternalFlags = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			}
		}

		if ( LE_PRINT_DEBUG_MESSAGES ) {
			logger.info( "" );
		}

		std::vector<VkSubpassDescription2> subpasses;
		subpasses.reserve( 1 );

		{
			VkSubpassDescription2 subpassDescription{
			    .sType                   = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
			    .pNext                   = nullptr, // optional
			    .flags                   = 0,       // optional
			    .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
			    .viewMask                = 0,
			    .inputAttachmentCount    = 0, // optional
			    .pInputAttachments       = nullptr,
			    .colorAttachmentCount    = uint32_t( colorAttachmentReferences.size() ), // optional
			    .pColorAttachments       = colorAttachmentReferences.data(),
			    .pResolveAttachments     = resolveAttachmentReferences.empty() ? nullptr : resolveAttachmentReferences.data(), // optional
			    .pDepthStencilAttachment = dsAttachmentReference,                                                              // optional
			    .preserveAttachmentCount = 0,                                                                                  // optional
			    .pPreserveAttachments    = nullptr,
			};

			subpasses.emplace_back( subpassDescription );
		}

		VkMemoryBarrier2     memoryBarriers[ 2 ];
		VkSubpassDependency2 dependencies[ 2 ];

		{
			if ( LE_PRINT_DEBUG_MESSAGES ) {

				logger.info( "Subpass Dependency: VK_SUBPASS_EXTERNAL to subpass `%s`", pass.debugName );
				logger.info( "\t srcStage: %-40s Anything in stage %1$s must happen-before", to_string_vk_pipeline_stage_flags2( srcStageFromExternalFlags ).c_str() );
				logger.info( "\t dstStage: %-40s anything in stage %1$s.", to_string_vk_pipeline_stage_flags2( dstStageFromExternalFlags ).c_str() );
				uint64_t( srcAccessFromExternalFlags )
				    ? logger.info( "\tsrcAccess: %-40s Memory from stage %s, accessing %1$s must be made available", to_string_vk_access_flags2( srcAccessFromExternalFlags ).c_str(), to_string_vk_pipeline_stage_flags2( srcStageFromExternalFlags ).c_str() )
				    : logger.info( "\tsrcAccess: %-40s No memory needs to be made available", to_string_vk_access_flags2( srcAccessFromExternalFlags ).c_str() );
				logger.info( "\tdstAccess: %-40s before memory is made visible to %1$s in stage %s", to_string_vk_access_flags2( dstAccessFromExternalFlags ).c_str(), to_string_vk_pipeline_stage_flags2( dstStageFromExternalFlags ).c_str() );

				logger.info( "Subpass Dependency: subpass `%s` to VK_SUBPASS_EXTERNAL:", pass.debugName );
				logger.info( "\t srcStage: %-40s Anything in stage %1$s must happen-before", to_string_vk_pipeline_stage_flags2( srcStageToExternalFlags ).c_str() );
				logger.info( "\t dstStage: %-40s anything in stage %1$s.", to_string_vk_pipeline_stage_flags2( dstStageToExternalFlags ).c_str() );
				uint64_t( srcAccessToExternalFlags )
				    ? logger.info( "\tsrcAccess: %-40s Memory from stage %s, accessing %1$s must be made available", to_string_vk_access_flags2( srcAccessToExternalFlags ).c_str(), to_string_vk_pipeline_stage_flags2( srcStageToExternalFlags ).c_str() )
				    : logger.info( "\tsrcAccess: %-40s No memory needs to be made available", to_string_vk_access_flags2( srcAccessToExternalFlags ).c_str() );
				logger.info( "\tdstAccess: %-40s before memory is made visible to %1$s in stage %s", to_string_vk_access_flags2( dstAccessToExternalFlags ).c_str(), to_string_vk_pipeline_stage_flags2( dstStageToExternalFlags ).c_str() );
				logger.info( "" );
			}

			memoryBarriers[ 0 ] = {
			    .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
			    .pNext         = nullptr,
			    .srcStageMask  = srcStageFromExternalFlags,
			    .srcAccessMask = srcAccessFromExternalFlags,
			    .dstStageMask  = dstStageFromExternalFlags,
			    .dstAccessMask = dstAccessFromExternalFlags,
			};
			memoryBarriers[ 1 ] = {
			    .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
			    .pNext         = nullptr,
			    .srcStageMask  = srcStageToExternalFlags,
			    .srcAccessMask = srcAccessToExternalFlags,
			    .dstStageMask  = dstStageToExternalFlags,
			    .dstAccessMask = dstAccessToExternalFlags,
			};

			dependencies[ 0 ] = {
			    // external to subpass
			    .sType           = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
			    .pNext           = memoryBarriers,              // optional
			    .srcSubpass      = VK_SUBPASS_EXTERNAL,         // outside of renderpass
			    .dstSubpass      = 0,                           // first subpass
			    .srcStageMask    = 0,                           // not used
			    .dstStageMask    = 0,                           // not used
			    .srcAccessMask   = 0,                           // not used
			    .dstAccessMask   = 0,                           // not used
			    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT, // optional
			    .viewOffset      = 0,
			};
			dependencies[ 1 ] = {
			    // subpass to external
			    .sType           = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
			    .pNext           = memoryBarriers + 1,          // optional
			    .srcSubpass      = 0,                           // last subpass
			    .dstSubpass      = VK_SUBPASS_EXTERNAL,         // outside of subpass
			    .srcStageMask    = 0,                           // not used
			    .dstStageMask    = 0,                           // not used
			    .srcAccessMask   = 0,                           // not used
			    .dstAccessMask   = 0,                           // not used
			    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT, // optional
			    .viewOffset      = 0,
			};
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
				for ( const auto& a : attachments ) {

					// We use offsetof so that we can get everything from flags to the start of the
					// attachmentdescription to (but not including) loadOp.
					static_assert(
					    offsetof( VkAttachmentDescription2, loadOp ) -
					            offsetof( VkAttachmentDescription2, flags ) ==
					        sizeof( VkAttachmentDescription2::flags ) +
					            sizeof( VkAttachmentDescription2::format ) +
					            sizeof( VkAttachmentDescription2::samples ),
					    "AttachmentDescription struct must be tightly packed for efficient hashing" );

					rp_hash = SpookyHash::Hash64(
					    &a.flags, offsetof( VkAttachmentDescription2, loadOp ) - offsetof( VkAttachmentDescription2, flags ),
					    rp_hash );
				}

				// -- Hash subpasses
				for ( const auto& s : subpasses ) {

					// Note: Attachment references are not that straightforward to hash either, as they contain a layout
					// field, which we want to ignore, since it makes no difference for render pass compatibility.

					rp_hash = SpookyHash::Hash64( &s.flags, sizeof( s.flags ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.pipelineBindPoint, sizeof( s.pipelineBindPoint ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.inputAttachmentCount, sizeof( s.inputAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.colorAttachmentCount, sizeof( s.colorAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.preserveAttachmentCount, sizeof( s.preserveAttachmentCount ), rp_hash );

					// We define this as a pure function lambda, and hope for it to be inlined
					auto calc_hash_for_attachment_references = []( VkAttachmentReference2 const* pAttachmentRefs, unsigned int count, uint64_t seed ) -> uint64_t {
						if ( pAttachmentRefs == nullptr ) {
							return seed;
						}
						// ----------| invariant: pAttachmentRefs is valid
						for ( auto const* pAr = pAttachmentRefs; pAr != pAttachmentRefs + count; pAr++ ) {
							// Note: for RenderPass compatibility, only the actual attachment
							// counts - Layout has no effect on compatibility.
							seed = SpookyHash::Hash64( &pAr->attachment, sizeof( VkAttachmentReference2::attachment ), seed );
						}
						return seed;
					};

					// -- For each element in attachment reference, add attachment reference index to the hash
					//
					rp_hash = calc_hash_for_attachment_references( s.pColorAttachments, s.colorAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pInputAttachments, s.inputAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pDepthStencilAttachment, 1, rp_hash );

					// Note that we did not calculate hashes for resolve attachments, as these do not contribute
					// to renderpass compatibility considerations. See: vkSpec:`7.2. Render Pass Compatibility`

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

			VkRenderPassCreateInfo2 renderpassCreateInfo{
			    .sType                   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
			    .pNext                   = nullptr, // optional
			    .flags                   = 0,       // optional
			    .attachmentCount         = uint32_t( attachments.size() ),
			    .pAttachments            = attachments.data(),
			    .subpassCount            = uint32_t( subpasses.size() ),
			    .pSubpasses              = subpasses.data(),
			    .dependencyCount         = uint32_t( sizeof( dependencies ) / sizeof( VkSubpassDependency2 ) ),
			    .pDependencies           = dependencies,
			    .correlatedViewMaskCount = 0, // optional
			    .pCorrelatedViewMasks    = 0,
			};

			// Create vulkan renderpass object

			vkCreateRenderPass2( device, &renderpassCreateInfo, nullptr, &pass.renderPass );

			delete dsAttachmentReference; // noo-op if nullptr; we clean up here in case we allocated a
			                              // depth stencil attachment reference above.
			                              // Once createRenderPass has consumed the data, we can safely delete.

			AbstractPhysicalResource rp;
			rp.type         = AbstractPhysicalResource::eRenderPass;
			rp.asRenderPass = pass.renderPass;

			// Add vulkan renderpass object to list of owned and life-time tracked resources, so that
			// it can be recycled when not needed anymore.
			frame.ownedResources.emplace_front( std::move( rp ) );
		}
	} // end for each pass
}

// ----------------------------------------------------------------------

/// \brief fetchVkBuffer from frame local storage based on resource handle flags
/// - allocatorBuffers[index] if transient,
/// - stagingAllocator.buffers[index] if staging,
/// otherwise, fetch from frame available resources based on an id lookup.
static inline VkBuffer frame_data_get_buffer_from_le_resource_id( const BackendFrameData& frame, const le_buf_resource_handle& buffer ) {

	if ( buffer->data->flags == uint8_t( le_buf_resource_usage_flags_t::eIsVirtual ) ) {
		return frame.allocatorBuffers[ buffer->data->index ];
	} else if ( buffer->data->flags == uint8_t( le_buf_resource_usage_flags_t::eIsStaging ) ) {
		return frame.stagingAllocator->buffers[ buffer->data->index ];
	} else {
		return frame.availableResources.at( buffer ).as.buffer;
	}
}

// ----------------------------------------------------------------------
static inline VkImage frame_data_get_image_from_le_resource_id( const BackendFrameData& frame, const le_img_resource_handle& img ) {
	return frame.availableResources.at( img ).as.image;
}

// ----------------------------------------------------------------------
static inline VkFormat frame_data_get_image_format_from_resource_id( BackendFrameData const& frame, const le_img_resource_handle& img ) {
	return frame.availableResources.at( img ).info.imageInfo.format;
}

// ----------------------------------------------------------------------

static inline AllocatedResourceVk const& frame_data_get_allocated_resource_from_resource_id( BackendFrameData& frame, const le_resource_handle& rsp ) {
	return frame.availableResources.at( rsp );
}

// ----------------------------------------------------------------------
// if specific format for texture was not specified, return format of referenced image
static inline VkFormat frame_data_get_image_format_from_texture_info( BackendFrameData const& frame, le_image_sampler_info_t const& texInfo ) {
	if ( texInfo.imageView.format == le::Format::eUndefined ) {
		return ( frame_data_get_image_format_from_resource_id( frame, texInfo.imageView.imageId ) );
	} else {
		return static_cast<VkFormat>( texInfo.imageView.format );
	}
}

// ----------------------------------------------------------------------

VkImageAspectFlags get_aspect_flags_from_format( le::Format const& format ) {
	VkImageAspectFlags aspectFlags{};

	bool isDepth   = false;
	bool isStencil = false;
	le_format_get_is_depth_stencil( format, isDepth, isStencil );

	if ( isDepth || isStencil ) {
		if ( isDepth ) {
			aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if ( isStencil ) {
			aspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	} else {
		aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
	}

	return aspectFlags;
}

// ----------------------------------------------------------------------
// input: Pass
// output: framebuffer, append newly created imageViews to retained resources list.
static void backend_create_frame_buffers( BackendFrameData& frame, VkDevice& device ) {

	for ( auto& pass : frame.passes ) {

		if ( pass.type != le::QueueFlagBits::eGraphics ) {
			continue;
		}

		uint32_t attachmentCount = pass.numColorAttachments +
		                           pass.numResolveAttachments +
		                           pass.numDepthStencilAttachments;

		std::vector<VkImageView> framebufferAttachments;
		framebufferAttachments.reserve( attachmentCount );

		auto const attachment_end = pass.attachments + attachmentCount;
		for ( AttachmentInfo const* attachment = pass.attachments; attachment != attachment_end; attachment++ ) {

			VkImageSubresourceRange subresourceRange{
			    .aspectMask     = get_aspect_flags_from_format( attachment->format ),
			    .baseMipLevel   = 0,
			    .levelCount     = 1,
			    .baseArrayLayer = 0,
			    .layerCount     = 1,
			};

			VkImageViewCreateInfo imageViewCreateInfo{
			    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			    .pNext            = nullptr, // optional
			    .flags            = 0,       // optional
			    .image            = frame_data_get_image_from_le_resource_id( frame, attachment->resource ),
			    .viewType         = VK_IMAGE_VIEW_TYPE_2D,
			    .format           = VkFormat( attachment->format ),
			    .components       = {},
			    .subresourceRange = subresourceRange,
			};

			VkImageView imageView = nullptr;
			vkCreateImageView( device, &imageViewCreateInfo, nullptr, &imageView );

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

		VkFramebufferCreateInfo framebufferCreateInfo{
		    .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		    .pNext           = nullptr, // optional
		    .flags           = 0,       // optional
		    .renderPass      = pass.renderPass,
		    .attachmentCount = attachmentCount, // optional
		    .pAttachments    = framebufferAttachments.data(),
		    .width           = pass.width,
		    .height          = pass.height,
		    .layers          = 1,
		};

		vkCreateFramebuffer( device, &framebufferCreateInfo, nullptr, &pass.framebuffer );
		{
			// Retain framebuffer

			AbstractPhysicalResource fb;
			fb.type          = AbstractPhysicalResource::eFramebuffer;
			fb.asFramebuffer = pass.framebuffer;

			frame.ownedResources.emplace_front( std::move( fb ) );
		}
	}
}

// ----------------------------------------------------------------------

static void backend_create_descriptor_pools( BackendFrameData& frame, VkDevice& device, size_t numRenderPasses ) {

	// Make sure that there is one descriptorpool for every renderpass.
	// descriptor pools which were created previously will be re-used,
	// if we're suddenly rendering more frames, we will add additional
	// descriptorPools.

	// At this point it would be nice to have an idea for each renderpass
	// on how many descriptors to expect, but we cannot know that realistically
	// without going through the command buffer... yuck.

	// This is why we're creating space for a generous amount of descriptors
	// hoping we're not running out when assembling the command buffer.

	constexpr VkDescriptorType DESCRIPTOR_TYPES[] = {
	    VK_DESCRIPTOR_TYPE_SAMPLER,
	    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
	    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
	    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
	    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
	    VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
	    VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT,
	    VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
	};

	constexpr size_t DESCRIPTOR_TYPE_COUNT = sizeof( DESCRIPTOR_TYPES ) / sizeof( VkDescriptorType );

	for ( ; frame.descriptorPools.size() < numRenderPasses; ) {

		std::vector<VkDescriptorPoolSize> descriptorPoolSizes;

		descriptorPoolSizes.reserve( DESCRIPTOR_TYPE_COUNT );

		for ( auto i : DESCRIPTOR_TYPES ) {
			descriptorPoolSizes.push_back( {
			    .type            = i,
			    .descriptorCount = 1000,
			} ); // 1000 descriptors of each type
		}

		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{
		    .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		    .pNext         = nullptr, // optional
		    .flags         = 0,       // optional
		    .maxSets       = 2000,
		    .poolSizeCount = uint32_t( descriptorPoolSizes.size() ),
		    .pPoolSizes    = descriptorPoolSizes.data(),
		};

		VkDescriptorPool descriptorPool = nullptr;
		vkCreateDescriptorPool( device, &descriptorPoolCreateInfo, nullptr, &descriptorPool );

		frame.descriptorPools.emplace_back( std::move( descriptorPool ) );
	}
}

// ----------------------------------------------------------------------
// Returns a VkFormat which will match a given set of LeImageUsageFlags.
// If a matching format cannot be inferred, this method
le::Format infer_image_format_from_le_image_usage_flags( le_backend_o* self, le::ImageUsageFlags const& flags ) {
	le::Format format{};
	if ( flags & le::ImageUsageFlags( le::ImageUsageFlagBits::eColorAttachment ) ) {
		// set to default color format
		format = self->defaultFormatColorAttachment;
	} else if ( flags & le::ImageUsageFlags( le::ImageUsageFlagBits::eDepthStencilAttachment ) ) {
		// set to default depth stencil format
		format = self->defaultFormatDepthStencilAttachment;
	} else if ( flags & le::ImageUsageFlags( le::ImageUsageFlagBits::eSampled ) ) {
		format = self->defaultFormatSampledImage;
	} else {
		// we don't know what to do because we can't infer the intended use of this resource.
		format = le::Format::eUndefined;
	}
	return format;
}
// ----------------------------------------------------------------------

static int32_t backend_allocate_image( le_backend_o*                  self,
                                       VkImageCreateInfo const*       pImageCreateInfo,
                                       VmaAllocationCreateInfo const* pAllocationCreateInfo,
                                       VkImage*                       pImage,
                                       VmaAllocation*                 pAllocation,
                                       VmaAllocationInfo*             pAllocationInfo ) {

	auto result = vmaCreateImage( self->mAllocator,
	                              pImageCreateInfo,
	                              pAllocationCreateInfo,
	                              pImage,
	                              pAllocation,
	                              pAllocationInfo );
	return result;
}

// ----------------------------------------------------------------------

static void backend_destroy_image( le_backend_o* self, VkImage image, VmaAllocation allocation ) {
	vmaDestroyImage( self->mAllocator, image, allocation );
}

// ----------------------------------------------------------------------

static int32_t backend_allocate_buffer( le_backend_o*                  self,
                                        VkBufferCreateInfo const*      pBufferCreateInfo,
                                        VmaAllocationCreateInfo const* pAllocationCreateInfo,
                                        VkBuffer*                      pBuffer,
                                        VmaAllocation*                 pAllocation,
                                        VmaAllocationInfo*             pAllocationInfo ) {
	auto result = vmaCreateBuffer( self->mAllocator, pBufferCreateInfo, pAllocationCreateInfo, pBuffer, pAllocation, pAllocationInfo );
	return result;
}

// ----------------------------------------------------------------------

static void backend_destroy_buffer( le_backend_o* self, VkBuffer buffer, VmaAllocation allocation ) {
	vmaDestroyBuffer( self->mAllocator, buffer, allocation );
}

// ----------------------------------------------------------------------
// Allocates and creates a physical vulkan resource using vmaAlloc given an allocator
// Returns an AllocatedResourceVk, currently does not do any error checking.
static inline AllocatedResourceVk allocate_resource_vk( const VmaAllocator& alloc, const ResourceCreateInfo& resourceInfo, VkDevice device = nullptr ) {
	static auto         logger = LeLog( LOGGER_LABEL );
	AllocatedResourceVk res{};
	res.info = resourceInfo;
	VmaAllocationCreateInfo allocationCreateInfo{};
	allocationCreateInfo.flags          = {}; // default flags
	allocationCreateInfo.usage          = VMA_MEMORY_USAGE_GPU_ONLY;
	allocationCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkResult result = VK_SUCCESS;

	if ( resourceInfo.isBuffer() ) {

		result = vmaCreateBuffer(
		    alloc,
		    &resourceInfo.bufferInfo,
		    &allocationCreateInfo,
		    &res.as.buffer,
		    &res.allocation,
		    &res.allocationInfo );
		assert( result == VK_SUCCESS );

	} else if ( resourceInfo.isImage() ) {

		result = vmaCreateImage(
		    alloc,
		    &resourceInfo.imageInfo,
		    &allocationCreateInfo,
		    &res.as.image,
		    &res.allocation,
		    &res.allocationInfo );
		assert( result == VK_SUCCESS );
	} else if ( resourceInfo.isBlas() ) {

		// Allocate bottom level ray tracing acceleration structure

		assert( device && "blas allocation needs device" );

		auto const blas = reinterpret_cast<le_rtx_blas_info_o*>( resourceInfo.blasInfo.handle );

		std::vector<VkAccelerationStructureGeometryKHR> geometries;
		std::vector<uint32_t>                           primitive_counts;

		geometries.reserve( blas->geometries.size() );
		primitive_counts.reserve( geometries.size() );

		for ( auto const& g : blas->geometries ) {

			VkAccelerationStructureGeometryKHR geom = {
			    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			    .pNext        = nullptr, // optional
			    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
			    .geometry     = {
			            .triangles = {
			                .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
			                .pNext         = nullptr, // optional
			                .vertexFormat  = VkFormat( g.vertex_format ),
			                .vertexData    = { 0 },
			                .vertexStride  = 0,
			                .maxVertex     = g.vertex_count - 1, // highest index of a vertex that will be accessed by build command
			                .indexType     = VkIndexType( g.index_type ),
			                .indexData     = { 0 },
			                .transformData = { 0 },
                    },
                },
			    .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, // optional
			};

			geometries.push_back( geom );

			primitive_counts.push_back( g.index_count ? g.index_count / 3 : g.vertex_count / 3 );
		}

		VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		    .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		    .pNext                    = nullptr, // optional
		    .type                     = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		    .flags                    = 0, // optional
		    .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		    .srcAccelerationStructure = 0,                             // optional
		    .dstAccelerationStructure = 0,                             // optional
		    .geometryCount            = uint32_t( geometries.size() ), // optional
		    .pGeometries              = geometries.data(),             // optional
		    .ppGeometries             = 0,
		    .scratchData              = { 0 },
		};

		VkAccelerationStructureBuildSizesInfoKHR build_sizes = {
		    .sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
		    .pNext                     = nullptr, // optional
		    .accelerationStructureSize = 0,
		    .updateScratchSize         = 0,
		    .buildScratchSize          = 0,
		};
		vkGetAccelerationStructureBuildSizesKHR(
		    device,
		    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		    &build_info,
		    primitive_counts.data(),
		    &build_sizes );

		VkBufferCreateInfo bufferInfo = {
		    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .pNext                 = nullptr,
		    .flags                 = 0,
		    .size                  = build_sizes.accelerationStructureSize,
		    .usage                 = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		    .queueFamilyIndexCount = 0,
		    .pQueueFamilyIndices   = 0,
		};

		{
			result =
			    vmaCreateBuffer(
			        alloc,
			        &static_cast<VkBufferCreateInfo&>( bufferInfo ),
			        &allocationCreateInfo,
			        &res.info.blasInfo.buffer,
			        &res.allocation,
			        &res.allocationInfo );

			assert( result == VkResult::VK_SUCCESS );
		}

		VkAccelerationStructureCreateInfoKHR create_info = {
		    .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		    .pNext         = nullptr, // optional
		    .createFlags   = 0,       // optional
		    .buffer        = res.info.blasInfo.buffer,
		    .offset        = 0, // must be a multiple of 256 : vkSpec
		    .size          = build_sizes.accelerationStructureSize,
		    .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		    .deviceAddress = 0, // if 0, this means no specific device address requested
		};

		vkCreateAccelerationStructureKHR( device, &create_info, nullptr, &res.as.blas );

		logger.info( "Created acceleration structure '%p' with size: %d, scratch size: %d", res.as.blas, build_sizes.accelerationStructureSize, build_sizes.buildScratchSize );

		res.info.blasInfo.buffer_size = build_sizes.accelerationStructureSize;

		// Store memory requirements for scratch buffer into allocation info for this blas element
		res.info.blasInfo.scratch_buffer_size = build_sizes.buildScratchSize;

		// Query, and store object integer handle, which is used to refer
		// to this bottom-level acceleration structure from a top-level
		// acceleration structure
		VkAccelerationStructureDeviceAddressInfoKHR device_address_info = {
		    .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		    .pNext                 = nullptr, // optional
		    .accelerationStructure = res.as.blas,
		};
		;

		res.info.blasInfo.device_address =
		    vkGetAccelerationStructureDeviceAddressKHR( device, &device_address_info );

	} else if ( resourceInfo.isTlas() ) {

		// Allocate top level ray tracing allocation structure

		assert( device && "tlas allocation needs device" );

		auto const tlas = reinterpret_cast<le_rtx_tlas_info_o*>( resourceInfo.tlasInfo.handle );

		assert( tlas && "tlas must be valid." );

		VkAccelerationStructureGeometryKHR geom = {
		    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		    .pNext        = nullptr, // optional
		    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		    .geometry     = {
		            .instances = {
		                .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
		                .pNext           = nullptr, // optional
		                .arrayOfPointers = 0,
		                .data            = { 0 },

                } },
		    .flags = 0, // optional
		};

		VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		    .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		    .pNext                    = nullptr,
		    .type                     = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		    .flags                    = 0,
		    .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		    .srcAccelerationStructure = 0,
		    .dstAccelerationStructure = 0,
		    .geometryCount            = 1,
		    .pGeometries              = &geom,
		    .ppGeometries             = 0,
		    .scratchData              = { 0 },
		};

		VkAccelerationStructureBuildSizesInfoKHR build_sizes = {
		    .sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
		    .pNext                     = nullptr, // optional
		    .accelerationStructureSize = 0,
		    .updateScratchSize         = 0,
		    .buildScratchSize          = 0,
		};
		;
		vkGetAccelerationStructureBuildSizesKHR( device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &tlas->instances_count, &build_sizes );

		VkBufferCreateInfo bufferInfo = {
		    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .pNext                 = nullptr, // optional
		    .flags                 = 0,       // optional
		    .size                  = build_sizes.accelerationStructureSize,
		    .usage                 = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
		    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		    .queueFamilyIndexCount = 0, // optional
		    .pQueueFamilyIndices   = 0,
		};

		{
			result =
			    vmaCreateBuffer(
			        alloc,
			        &bufferInfo,
			        &allocationCreateInfo,
			        &res.info.tlasInfo.buffer,
			        &res.allocation,
			        &res.allocationInfo );

			assert( result == VkResult::VK_SUCCESS );
		}

		VkAccelerationStructureCreateInfoKHR create_info = {
		    .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		    .pNext         = nullptr, // optional
		    .createFlags   = 0,       // optional
		    .buffer        = res.info.tlasInfo.buffer,
		    .offset        = 0,
		    .size          = build_sizes.accelerationStructureSize,
		    .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		    .deviceAddress = 0, // if 0, this means no specific device address requested
		};

		vkCreateAccelerationStructureKHR( device, &create_info, nullptr, &res.as.tlas );

		// Store memory requirements for scratch buffer into allocation info for this tlas element
		res.info.tlasInfo.scratch_buffer_size = build_sizes.buildScratchSize;
		res.info.tlasInfo.buffer_size         = build_sizes.accelerationStructureSize;

	} else {
		assert( false && "Cannot allocate unknown resource type." );
	}
	assert( result == VK_SUCCESS );
	return res;
};

// ----------------------------------------------------------------------

// Creates a new staging allocator
// Typically, there is one staging allocator associated to each frame.
static le_staging_allocator_o* staging_allocator_create( VmaAllocator const vmaAlloc, VkDevice const device ) {
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
static bool staging_allocator_map( le_staging_allocator_o* self, uint64_t numBytes, void** pData, le_buf_resource_handle* resource_handle ) {

	auto lock = std::scoped_lock( self->mtx );

	VmaAllocation     allocation; // handle to allocation
	VkBuffer          buffer;     // handle to buffer (returned from vmaMemAlloc)
	VmaAllocationInfo allocationInfo;

	VkBufferCreateInfo bufferCreateInfo{
	    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .pNext                 = nullptr, // optional
	    .flags                 = 0,       // optional
	    .size                  = numBytes,
	    .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0, // optional
	    .pQueueFamilyIndices   = 0,
	};

	VmaAllocationCreateInfo allocationCreateInfo{};
	allocationCreateInfo.flags          = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocationCreateInfo.usage          = VMA_MEMORY_USAGE_CPU_ONLY;
	allocationCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	auto result =
	    vmaCreateBuffer(
	        self->allocator,
	        &bufferCreateInfo,
	        &allocationCreateInfo,
	        &buffer,
	        &allocation,
	        &allocationInfo );

	assert( result == VK_SUCCESS );

	if ( result != VK_SUCCESS ) {
		return false;
	}

	//---------- | Invariant: create buffer was successful.

	{
		// -- Now store our allocation in the allocations vectors.
		size_t allocationIndex = self->allocations.size();

		self->allocations.push_back( allocation );
		self->allocationInfo.push_back( allocationInfo );
		self->buffers.emplace_back( buffer );

		// Staging resources share the same name, but their allocation index is different.
		//
		// The staging index makes sure the correct buffer for this handle can be retrieved later.

		static std::vector<le_buf_resource_handle> staging_buffers;

		// We locally cache the names of all the index-specialised
		// staging buffers on first use, so that we don't have to look them
		// up in the renderer's resource library on every frame.
		//
		if ( allocationIndex < staging_buffers.size() ) {
			staging_buffers.reserve( allocationIndex );
		}
		while ( staging_buffers.size() < self->allocations.size() ) {
			size_t index = staging_buffers.size();
			staging_buffers.emplace_back(
			    le_renderer::renderer_i.produce_buf_resource_handle(
			        "Le-Staging-Buffer",
			        le_buf_resource_usage_flags_t::eIsStaging, uint32_t( index ) ) );
		}

		*resource_handle = staging_buffers[ allocationIndex ];
	}

	// Map memory so that it may be written to
	vmaMapMemory( self->allocator, allocation, pData );

	return true;
};

// ----------------------------------------------------------------------

/// Frees all allocations held by the staging allocator given in `self`
static void staging_allocator_reset( le_staging_allocator_o* self ) {
	auto lock = std::scoped_lock( self->mtx );

	assert( self->buffers.size() == self->allocations.size() && self->buffers.size() == self->allocationInfo.size() &&
	        "buffers, allocations, and allocationInfos sizes must match." );

	// Since buffers were allocated using the VMA allocator,
	// we cannot delete them directly using the device. We must delete them using the allocator,
	// so that the allocator can track current allocations.

	auto allocation = self->allocations.begin();
	for ( auto b = self->buffers.begin(); b != self->buffers.end(); b++, allocation++ ) {
		vmaUnmapMemory( self->allocator, *allocation );
		vmaDestroyBuffer( self->allocator, *b, *allocation ); // implicitly calls vmaFreeMemory()
	}

	self->buffers.clear();
	self->allocations.clear();
	self->allocationInfo.clear();
}

// ----------------------------------------------------------------------

// Destroys a staging allocator (and implicitly all of its derived objects)
static void staging_allocator_destroy( le_staging_allocator_o* self ) {

	// Reset the object first so that dependent objects (vmaAllocations, vulkan objects) are cleaned up.
	staging_allocator_reset( self );

	delete self;
}

// ----------------------------------------------------------------------

// Frees any resources which are marked for being recycled in the current frame.
inline void frame_release_binned_resources( BackendFrameData& frame, VmaAllocator& allocator ) {
	for ( auto& a : frame.binnedResources ) {
		if ( a.second.info.isBuffer() ) {
			vmaDestroyBuffer( allocator, a.second.as.buffer, a.second.allocation );
		} else {
			vmaDestroyImage( allocator, a.second.as.image, a.second.allocation );
		}
	}
	frame.binnedResources.clear();
}

// ----------------------------------------------------------------------

static inline void consolidate_resource_info_into( le_resource_info_t& lhs, le_resource_info_t const& rhs ) {

	// return superset of lhs and rhs in lhs
	assert( lhs.type == rhs.type );

	switch ( lhs.type ) {
	case LeResourceType::eBuffer: {

		lhs.buffer.size = std::max( lhs.buffer.size, rhs.buffer.size );
		lhs.buffer.usage |= rhs.buffer.usage;

		return;
	}
	case LeResourceType::eImage: {
		// TODO (tim): check how we can enforce correct number of array layers and mip levels

		lhs.image.samplesFlags |= rhs.image.samplesFlags;
		assert( lhs.image.sample_count_log2 == 0 ); // NOTE: we expect sample_count_log2 not to be set at this point
		assert( rhs.image.sample_count_log2 == 0 ); // NOTE: we expect sample_count_log2 not to be set at this point

		if ( rhs.image.arrayLayers > lhs.image.arrayLayers ) {
			lhs.image.arrayLayers = rhs.image.arrayLayers;
		}

		if ( rhs.image.mipLevels > lhs.image.mipLevels ) {
			lhs.image.mipLevels = rhs.image.mipLevels;
		}

		if ( uint32_t( rhs.image.imageType ) > uint32_t( lhs.image.imageType ) ) {
			// this is a bit sketchy.
			lhs.image.imageType = rhs.image.imageType;
		}

		lhs.image.flags |= rhs.image.flags;
		lhs.image.usage |= rhs.image.usage;

		// If an image format was explictly set, this takes precedence over eUndefined.
		// Note that we skip this block if both infos have the same format, so if both
		// infos are eUndefined, format stays undefined.

		if ( rhs.image.format != le::Format::eUndefined && rhs.image.format != lhs.image.format ) {

			// ----------| invariant: both formats differ, and second format is not undefined

			if ( lhs.image.format == le::Format::eUndefined ) {
				lhs.image.format = rhs.image.format;
			} else {
				// Houston, we have a problem!
				// Two different formats were explicitly specified for this image.
				assert( false );
			}
		}

		// Make sure the image is as large as it needs to be

		lhs.image.extent.width  = std::max( lhs.image.extent.width, rhs.image.extent.width );
		lhs.image.extent.height = std::max( lhs.image.extent.height, rhs.image.extent.height );
		lhs.image.extent.depth  = std::max( lhs.image.extent.depth, rhs.image.extent.depth );

		lhs.image.extent_from_pass.width  = std::max( lhs.image.extent_from_pass.width, rhs.image.extent_from_pass.width );
		lhs.image.extent_from_pass.height = std::max( lhs.image.extent_from_pass.height, rhs.image.extent_from_pass.height );
		lhs.image.extent_from_pass.depth  = std::max( lhs.image.extent_from_pass.depth, rhs.image.extent_from_pass.depth );
		return;
	}
	case LeResourceType::eRtxBlas: {
		return;
	}
	case LeResourceType::eRtxTlas: {
		return;
	}

	case LeResourceType::eUndefined: {
		assert( false ); // unreachable
		return;
	}
	}
}

// ----------------------------------------------------------------------
// TODO: don't use vectors for resourceInfos, consolidate in-place.
//
// should return a map of all resources used in all passes, with consolidated infos per-resource.
static void collect_resource_infos_per_resource(
    le_renderpass_o const* const*                               passes,
    size_t                                                      numRenderPasses,
    std::vector<le_resource_handle> const&                      frame_declared_resources_id,   // | pre-declared resources (declared via module)
    std::vector<le_resource_info_t> const&                      frame_declared_resources_info, // | info for each pre-declared resource
    std::unordered_map<le_resource_handle, le_resource_info_t>& active_resources ) {

	using namespace le_renderer;

	for ( auto rp = passes; rp != passes + numRenderPasses; rp++ ) {

		uint32_t                pass_width        = 0;
		uint32_t                pass_height       = 0;
		le::SampleCountFlagBits pass_sample_count = {};

		renderpass_i.get_framebuffer_settings( *rp, &pass_width, &pass_height, &pass_sample_count );

		uint16_t pass_num_samples_log2 = get_sample_count_log_2( uint32_t( pass_sample_count ) );

		le_resource_handle const* p_resources              = nullptr;
		le::AccessFlags2 const*   p_resources_access_flags = nullptr;
		size_t                    resources_count          = 0;

		renderpass_i.get_used_resources( *rp, &p_resources, &p_resources_access_flags, &resources_count );

		for ( size_t i = 0; i != resources_count; ++i ) {

			le_resource_handle const& resource = p_resources[ i ]; // Resource handle

			// Test whether a resource with this id is already in usedResources -
			// if not, resource_index will be identical to usedResource vector size,
			// which is useful, because as soon as we add an element to the vector
			// resource_index will index the correct element.

			// We must ensure that images which are used as Color, or DepthStencil attachments
			// fit the extents of their renderpass - as this is a Vulkan requirement.
			//
			// We do this here, because we know the extents of the renderpass.
			//
			// We also need to ensure that the extent has 1 as depth value by default.

			le_resource_info_t resourceInfo = {
			    .type = resource->data->type, // empty resource info, but with type set according to resource type
			};

			if ( resourceInfo.type == LeResourceType::eImage ) {

				auto& imgInfo = resourceInfo.image;

				imgInfo.extent_from_pass = { pass_width, pass_height, 1 };

				if ( ( p_resources_access_flags[ i ] &
				       ( le::AccessFlagBits2::eColorAttachmentWrite |
				         le::AccessFlagBits2::eColorAttachmentRead |
				         le::AccessFlagBits2::eDepthStencilAttachmentWrite |
				         le::AccessFlagBits2::eDepthStencilAttachmentRead ) ) ) {

					// ---------- | resource is either used as a depth stencil attachment, or a color attachment

					imgInfo.mipLevels    = 1;
					imgInfo.imageType    = le::ImageType::e2D;
					imgInfo.tiling       = le::ImageTiling::eOptimal;
					imgInfo.arrayLayers  = 1;
					imgInfo.samplesFlags = uint32_t( 1 ) << pass_num_samples_log2; // note we set sample count as a flag so that it can be consolidated -
					// imgInfo.sample_count_log2 = 0; // note that we leave sample_count_log2 untouched.
					imgInfo.extent = { pass_width, pass_height, 1 };
				}

			} else if ( resourceInfo.type == LeResourceType::eBuffer ) {
			} else if ( resourceInfo.type == LeResourceType::eRtxBlas ) {
			} else if ( resourceInfo.type == LeResourceType::eRtxTlas ) {
			} else {
				assert( false ); // unreachable
			}

			{
				auto emplace_result = active_resources.try_emplace( resource, std::move( resourceInfo ) );
				if ( !emplace_result.second ) {
					// entry was not assigned, result.first will hold an iterator to the current element
					consolidate_resource_info_into( emplace_result.first->second, resourceInfo );
				}
			}
		} // end for all resources

	} // end for all passes

	// -- Consolidate resources with pre-declared resources
	//
	// If any of our active resources has been pre-declared explicitly via the rendergraph,
	// consolidate its info with the pre-declared resource's info.
	//
	// As a side-effect this will also consolidate any frame-declared resources which
	// are declared more than once and used in the current rendergraph.

	for ( size_t i = 0; i != frame_declared_resources_id.size(); i++ ) {
		auto const& resource     = frame_declared_resources_id[ i ];
		auto const& resourceInfo = frame_declared_resources_info[ i ];

		auto find_result = active_resources.find( resource );
		if ( find_result != active_resources.end() ) {
			// a declared resource was found for an used resource - we must consolidate the two.

			consolidate_resource_info_into( find_result->second, resourceInfo );
		}
	}
}

// ----------------------------------------------------------------------

static void insert_msaa_versions(
    std::unordered_map<le_resource_handle, le_resource_info_t>& active_resources ) {
	// For each image resource which is specified with versions of additional sample counts
	// we create additional resource_ids (by patching in the sample count), and add matching
	// resource info, so that multisample versions of image resources can be allocated dynamically.
	std::unordered_map<le_resource_handle, le_resource_info_t> extra_resources;

	for ( auto const& ar : active_resources ) {
		if ( ar.first->data->type != LeResourceType::eImage ) {
			continue;
		}

		// ----------| Invariant: resource is an image

		// For any samplesFlags, we must create a matching msaa version of the image.
		std::bitset<sizeof( ar.second.image.samplesFlags ) * 8> samples_flags( ar.second.image.samplesFlags );

		uint32_t sample_count_log_2 = 1;                  // 2^1 evaluates to 2
		samples_flags               = samples_flags >> 1; // we remove the single-sampled image, as we assume it already exists.

		while ( samples_flags.count() ) {

			if ( samples_flags.test( 0 ) ) {
				// we must create a resource copy with this sample count
				le_resource_handle resource_copy =
				    le_renderer::renderer_i.produce_img_resource_handle(
				        ar.first->data->debug_name, sample_count_log_2, static_cast<le_img_resource_handle>( ar.first ), 0 );
				le_resource_info_t resource_info_copy      = ar.second;
				resource_info_copy.image.sample_count_log2 = sample_count_log_2;

				// insert extra resource into extra msaa resources
				auto result = extra_resources.try_emplace( resource_copy, resource_info_copy );
				if ( result.second == false ) {
					// key already existed, must be consolidated
					consolidate_resource_info_into( result.first->second, resource_info_copy );
				}
			}

			samples_flags = samples_flags >> 1;
			sample_count_log_2++;
		}
	}

	// append extra resources to active resources
	for ( auto& er : extra_resources ) {
		auto result = active_resources.try_emplace( er.first, er.second );
		if ( result.second == false ) {
			// key already existed, must be consolidated
			consolidate_resource_info_into( result.first->second, er.second );
		}
	}
}

static void printResourceInfo( le_resource_handle const& handle, ResourceCreateInfo const& info, const char* prefix = "" ) {
	static auto logger = LeLog( LOGGER_LABEL );
	if ( info.isBuffer() ) {
		logger.info( "%-15s : %-32s : %11d : %30s : %-30s", prefix, handle->data->debug_name, info.bufferInfo.size, "-",
		             to_string_vk_buffer_usage_flags( info.bufferInfo.usage ).c_str() );
	} else if ( info.isImage() ) {
		logger.info( "%-15s : %-30s@%d : %dx%dx%d : %30s : %-30s",
		             prefix,
		             !( handle->data->debug_name[ 0 ] == '\0' )
		                 ? handle->data->debug_name
		             : handle->data->reference_handle
		                 ? handle->data->reference_handle->data->debug_name
		                 : "unnamed",
		             uint32( info.imageInfo.samples ),
		             info.imageInfo.extent.width,
		             info.imageInfo.extent.height,
		             info.imageInfo.extent.depth,
		             to_str_vk_format( info.imageInfo.format ),
		             to_string_vk_image_usage_flags( info.imageInfo.usage ).c_str() );
	} else if ( info.isBlas() ) {
		logger.info( "%-15s :%-32s : %11d : (%28d) : %-30s",
		             prefix,
		             handle->data->debug_name,
		             info.blasInfo.buffer_size,
		             info.blasInfo.scratch_buffer_size,
		             "-" );
	} else if ( info.isTlas() ) {
		logger.info( "%-15s :%-32s : %11d : (%28d) : %-30s",
		             prefix,
		             handle->data->debug_name,
		             info.tlasInfo.buffer_size,
		             info.tlasInfo.scratch_buffer_size,
		             "-" );
	}
}

// ----------------------------------------------------------------------

static void patch_renderpass_extents(
    le_renderpass_o** passes,
    size_t            numRenderPasses,
    uint32_t          swapchainWidth,
    uint32_t          swapchainHeight ) {
	using namespace le_renderer;

	auto passes_end = passes + numRenderPasses;

	for ( auto rp = passes; rp != passes_end; rp++ ) {
		uint32_t pass_width  = 0;
		uint32_t pass_height = 0;
		renderpass_i.get_framebuffer_settings( *rp, &pass_width, &pass_height, nullptr );
		if ( pass_width == 0 ) {
			// if zero was chosen this means to use the default extents values for a
			// renderpass, which is to use the frame's current swapchain extents.
			pass_width = swapchainWidth;
			renderpass_i.set_width( *rp, pass_width );
		}

		if ( pass_height == 0 ) {
			// if zero was chosen this means to use the default extents values for a
			// renderpass, which is to use the frame's current swapchain extents.
			pass_height = swapchainHeight;
			renderpass_i.set_height( *rp, pass_height );
		}
	}
}

// ----------------------------------------------------------------------

static bool inferImageFormat( le_backend_o* self, le_img_resource_handle const& resource, le::ImageUsageFlags const& usageFlags, ResourceCreateInfo* createInfo ) {

	static auto logger = LeLog( LOGGER_LABEL );

	// If image format was not specified, we must try to
	// infer the image format from usage flags.
	auto inferred_format = infer_image_format_from_le_image_usage_flags( self, usageFlags );

	if ( inferred_format == le::Format::eUndefined ) {
		logger.error( "Fatal: Cannot infer image format, resource underspecified: '%s'", resource->data->debug_name );
		logger.error( "Specify usage, or provide explicit format option for resource to fix this error. " );
		logger.error( "Consider using le::RenderModule::declareResource()" );

		assert( false ); // we don't have enough information to infer image format.
		return false;
	} else {
		createInfo->imageInfo.format = static_cast<VkFormat>( inferred_format );
	}

	return true;
}

// ----------------------------------------------------------------------
// If image has mip levels, we implicitly add usage: "transfer_src", so that mip maps may be created by blitting.
static void patchImageUsageForMipLevels( ResourceCreateInfo* createInfo ) {
	if ( createInfo->imageInfo.mipLevels > 1 ) {
		createInfo->imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
}

// ----------------------------------------------------------------------

static void frame_resources_set_debug_names( le_backend_vk_instance_o* instance, VkDevice device_, BackendFrameData::ResourceMap_T& resources ) {
	static auto logger = LeLog( LOGGER_LABEL );

	// We capture the check for extension as a static, as this is not expected to
	// change for the lifetime of the application, and checking for the extension
	// on each frame is wasteful.
	//
	static bool check_utils_extension_available = le_backend_vk::vk_instance_i.is_extension_available( instance, VK_EXT_DEBUG_UTILS_EXTENSION_NAME );

	if ( !check_utils_extension_available ) {
		return;
	}

	// --------| invariant utuls extension is available

	for ( auto const& r : resources ) {

		auto device = VkDevice( device_ );

		VkDebugUtilsObjectNameInfoEXT nameInfo{
		    .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		    .pNext        = nullptr, // optional
		    .objectType   = VK_OBJECT_TYPE_UNKNOWN,
		    .objectHandle = 0,
		    .pObjectName  = "", // optional
		};
		;

		nameInfo.pObjectName = r.first->data->debug_name;

		switch ( r.first->data->type ) {
		case LeResourceType::eImage:
			nameInfo.objectType   = VK_OBJECT_TYPE_IMAGE;
			nameInfo.objectHandle = reinterpret_cast<uint64_t>( r.second.as.image );
			break;
		case LeResourceType::eBuffer:
			nameInfo.objectType   = VK_OBJECT_TYPE_BUFFER;
			nameInfo.objectHandle = reinterpret_cast<uint64_t>( r.second.as.buffer );
			break;
		case LeResourceType::eRtxBlas:
			nameInfo.objectType   = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
			nameInfo.objectHandle = reinterpret_cast<uint64_t>( r.second.as.blas );
			break;
		case LeResourceType::eRtxTlas:
			nameInfo.objectType   = VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
			nameInfo.objectHandle = reinterpret_cast<uint64_t>( r.second.as.tlas );
			break;
		default:
			assert( false && "unknown resource type" );
		}

		auto result = vkSetDebugUtilsObjectNameEXT( device, &nameInfo );
		assert( result == VK_SUCCESS );
	}
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

static void backend_allocate_resources( le_backend_o* self, BackendFrameData& frame, le_renderpass_o** passes, size_t numRenderPasses ) {

	/*
	- Frame is only ever allowed to reference frame-local resources.
	- "Acquire" therefore means we create local copies of backend-wide resource handles.
	*/

	static auto logger = LeLog( LOGGER_LABEL );

	static le_resource_handle LE_RTX_SCRATCH_BUFFER_HANDLE = LE_BUF_RESOURCE( "le_rtx_scratch_buffer_handle" ); // opaque handle for rtx scratch buffer

	// -- first it is our holy duty to drop any binned resources which
	// were condemned the last time this frame was active.
	// It's possible that this was more than two frames ago,
	// depending on how many swapchain images there are.
	//
	frame_release_binned_resources( frame, self->mAllocator );

	// Iterate over all resource declarations in all passes so that we can collect all resources,
	// and their usage information. Later, we will consolidate their usages so that resources can
	// be re-used across passes.
	//
	// Note that we accumulate all resource infos first, and do consolidation
	// in a separate step. That way, we can first make sure all flags are combined,
	// before we make sure to we find a valid image format which matches all uses...
	//

	std::unordered_map<le_resource_handle, le_resource_info_t> active_resources;

	collect_resource_infos_per_resource(
	    passes, numRenderPasses,
	    frame.declared_resources_id, frame.declared_resources_info,
	    active_resources );

	if ( LE_PRINT_DEBUG_MESSAGES ) {
		for ( auto const& r : active_resources ) {
			logger.info( "resource [ %-30s ] : [ %-50s ]", r.first->data->debug_name,
			             r.second.type == LeResourceType::eImage
			                 ? to_string_vk_image_usage_flags( r.second.image.usage ).c_str()
			                 : to_string_vk_buffer_usage_flags( r.second.buffer.usage ).c_str() );
		}
	}

	// For each image resource which has versions of additional sample counts
	// we create additional resource_ids (by patching in the sample count), and add matching
	// resource info, so that multisample versions of image resources can be allocated dynamically.
	insert_msaa_versions( active_resources );

	// Check if all resources declared in this frame are already available in backend.
	// If a resource is not available yet, this resource must be allocated.

	auto& backendResources = self->only_backend_allocate_resources_may_access.allocatedResources;

	for ( auto const& ar : active_resources ) {

		le_resource_handle const& resource     = ar.first;
		le_resource_info_t const& resourceInfo = ar.second; ///< consolidated resource info for this resource over all passes

		// See if a resource with this id is already available to the frame
		// This may be the case with a swapchain image resource for example,
		// as it is allocated and managed from within the swapchain, not here.
		//
		if ( frame.availableResources.find( resource ) != frame.availableResources.end() ) {
			// Resource is already available to and present in the frame.
			continue;
		}

		// ---------| invariant: resource with this id is not yet available to frame.

		// first check if the resource is available to the frame,
		// if that is not the chase, check if the resource is available to the frame.

		auto       resourceCreateInfo = ResourceCreateInfo::from_le_resource_info( resourceInfo );
		auto       foundIt            = backendResources.find( resource );
		const bool resourceIdNotFound = ( foundIt == backendResources.end() );

		if ( resourceIdNotFound ) {

			// Resource does not yet exist, we must allocate this resource and add it to the backend.
			// Then add a reference to it to the current frame.

			if ( resourceCreateInfo.isImage() ) {

				patchImageUsageForMipLevels( &resourceCreateInfo );

				if ( resourceCreateInfo.imageInfo.format == VK_FORMAT_UNDEFINED ) {
					inferImageFormat( self, static_cast<le_img_resource_handle>( resource ), resourceInfo.image.usage, &resourceCreateInfo );
				}
			}

			auto allocatedResource = allocate_resource_vk( self->mAllocator, resourceCreateInfo, self->device->getVkDevice() );

			if ( LE_PRINT_DEBUG_MESSAGES || true ) {
				printResourceInfo( resource, allocatedResource.info, "ALLOC" );
			}

			// Add resource to map of available resources for this frame
			frame.availableResources.insert_or_assign( resource, allocatedResource );

			// Add this newly allocated resource to the backend so that the following frames
			// may use it, too
			backendResources.insert_or_assign( resource, allocatedResource );

		} else {

			// If an existing resource has been found, we must check that it
			// was allocated with the same properties as the resource we require

			auto& foundResourceCreateInfo = foundIt->second.info;

			// Note that we use the greater-than operator, which means
			// that if our foundResource is equal to *or a superset of*
			// resourceCreateInfo, we can re-use the found resource.
			//
			if ( foundResourceCreateInfo >= resourceCreateInfo ) {

				// -- found info is either equal or a superset

				// Add a copy of this resource allocation to the current frame.
				frame.availableResources.emplace( resource, foundIt->second );

			} else {

				// -- info does not match.

				// We must re-allocate this resource, and add the old version of the resource to the recycling bin.

				// -- allocate a new resource

				if ( resourceCreateInfo.isImage() ) {
					patchImageUsageForMipLevels( &resourceCreateInfo );
					if ( resourceCreateInfo.imageInfo.format == VK_FORMAT_UNDEFINED ) {
						inferImageFormat( self, static_cast<le_img_resource_handle>( resource ), resourceInfo.image.usage, &resourceCreateInfo );
					}
				}

				auto allocatedResource = allocate_resource_vk( self->mAllocator, resourceCreateInfo );

				if ( LE_PRINT_DEBUG_MESSAGES || true ) {
					printResourceInfo( resource, allocatedResource.info, "RE-ALLOC" );
				}

				// Add a copy of old resource to recycling bin for this frame, so that
				// these resources get freed when this frame comes round again.
				//
				// We don't immediately delete the resources, as in-flight (preceding) frames
				// might still be using them.
				frame.binnedResources.try_emplace( resource, foundIt->second );

				// add the new version of the resource to frame available resources
				frame.availableResources.insert_or_assign( resource, allocatedResource );

				// Remove old version of resource from backend, and
				// add new version of resource to backend
				backendResources.insert_or_assign( resource, allocatedResource );
			}
		}
	} // end for all used resources
	if ( LE_PRINT_DEBUG_MESSAGES ) {
		logger.info( "" );
	}

	// -- Create rtx acceleration structure scratch buffer
	{
		// In case there are acceleration structures with the `build` flag set, we must allocate
		// a scratch buffer which is large enough to hold the largest of the acceleration structures
		// with the build flag set

		// TODO: this should also apply for any acceleration structures which have the `update` flag
		// set, as updating requires a scratch buffer too.

		uint64_t scratchbuffer_max_size = 0;

		for ( auto const& ar : active_resources ) {

			le_resource_handle const& resourceId   = ar.first;
			le_resource_info_t const& resourceInfo = ar.second; ///< consolidated resource info for this resource over all passes

			if ( resourceInfo.type == LeResourceType::eRtxBlas &&
			     ( resourceInfo.blas.usage & LE_RTX_BLAS_BUILD_BIT ) ) {
				auto const& frame_resource = frame.availableResources.at( resourceId );
				scratchbuffer_max_size     = std::max<uint64_t>( scratchbuffer_max_size, frame_resource.info.blasInfo.scratch_buffer_size );
			} else if ( resourceInfo.type == LeResourceType::eRtxTlas &&
			            ( resourceInfo.tlas.usage & LE_RTX_TLAS_BUILD_BIT ) ) {
				auto const& frame_resource = frame.availableResources.at( resourceId );
				scratchbuffer_max_size     = std::max<uint64_t>( scratchbuffer_max_size, frame_resource.info.tlasInfo.scratch_buffer_size );
			}
		}

		if ( scratchbuffer_max_size != 0 ) {
			// We must allocate a scratch buffer, which needs to be available for exactly one frame.
			le_resource_info_t resourceInfo{};
			resourceInfo.buffer.size              = uint32_t( scratchbuffer_max_size );
			resourceInfo.buffer.usage             = le::BufferUsageFlags( le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eShaderDeviceAddress );
			resourceInfo.type                     = LeResourceType::eBuffer;
			ResourceCreateInfo resourceCreateInfo = ResourceCreateInfo::from_le_resource_info( resourceInfo );
			auto               resource_id        = LE_RTX_SCRATCH_BUFFER_HANDLE;
			auto               allocated_resource = allocate_resource_vk( self->mAllocator, resourceCreateInfo, self->device->getVkDevice() );
			frame.availableResources.insert_or_assign( resource_id, allocated_resource );

			// We immediately bin the buffer resource, so that its lifetime is tied to the current frame.
			frame.binnedResources.insert_or_assign( resource_id, allocated_resource );
		}
	}

	// If we locked backendResources with a mutex, this would be the right place to release it.

	if ( LE_PRINT_DEBUG_MESSAGES ) {
		logger.info( "Available Resources" );
		logger.info( "%10s : %38s : %30s", "Type", "debugName", "Vk Handle" );
		for ( auto const& r : frame.availableResources ) {
			switch ( r.second.info.type ) {
			case ( LeResourceType::eUndefined ):
				break;
			case ( LeResourceType::eBuffer ):
				logger.info( "%10s : %38s : %30p",
				             "Buffer",
				             r.first->data->debug_name,
				             r.second.as.buffer );
				break;
			case ( LeResourceType::eImage ):
				logger.info( "%10s : %36s@%d : %30p",
				             "Image",
				             r.first->data->debug_name,
				             1 << r.first->data->num_samples,
				             r.second.as.buffer );
				break;
			case ( LeResourceType::eRtxBlas ):
				logger.info( "%10s : %36s@%d",
				             "RtxBLAS",
				             r.first->data->debug_name );
				break;
			case ( LeResourceType::eRtxTlas ):
				logger.info( "%10s : %36s@%d",
				             "RtxTLAS",
				             r.first->data->debug_name );
				break;
			}
		}
	}

	if ( DEBUG_TAG_RESOURCES ) {
		frame_resources_set_debug_names( self->instance, self->device->getVkDevice(), frame.availableResources );
	}
}

// ----------------------------------------------------------------------

// Allocates ImageViews, Samplers and Textures requested by individual passes
// these are tied to the lifetime of the frame, and will be re-created
static void frame_allocate_transient_resources( BackendFrameData& frame, VkDevice const& device, le_renderpass_o** passes, size_t numRenderPasses ) {

	using namespace le_renderer;
	static auto       logger = LeLog( LOGGER_LABEL );
	le::QueueFlagBits pass_type{};

	// Only for compute passes: Create imageviews for all available
	// resources which are of type image and which have usage
	// sampled or storage.
	//
	for ( auto p = passes; p != passes + numRenderPasses; p++ ) {

		// fetch pass type from this passes' queue sumbission info
		renderpass_i.get_queue_sumbission_info( *p, &pass_type, nullptr );

		if ( pass_type != le::QueueFlagBits::eCompute ) {
			continue;
		}

		const le_resource_handle* resources        = nullptr;
		const le::AccessFlags2*   resources_access = nullptr;
		size_t                    resource_count   = 0;

		renderpass_i.get_used_resources( *p, &resources, &resources_access, &resource_count );

		for ( size_t i = 0; i != resource_count; ++i ) {
			auto const& r = static_cast<le_img_resource_handle>( resources[ i ] );

			if ( r->data->type == LeResourceType::eImage ) {

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

				AllocatedResourceVk const& vk_resource_info = frame_data_get_allocated_resource_from_resource_id( frame, r );

				auto const& imageFormat = le::Format( vk_resource_info.info.imageInfo.format );

				// If the format is still undefined at this point, we can only throw our hands up in the air...
				//
				if ( imageFormat == le::Format::eUndefined ) {
					logger.warn( "Cannot create default view for image: '%s', as format is unspecified", r->data->debug_name );
					continue;
				}

				VkImageSubresourceRange subresourceRange{
				    .aspectMask     = get_aspect_flags_from_format( imageFormat ),
				    .baseMipLevel   = 0,
				    .levelCount     = VK_REMAINING_MIP_LEVELS, // we set VK_REMAINING_MIP_LEVELS which activates all mip levels remaining.
				    .baseArrayLayer = 0,
				    .layerCount     = 1,
				};

				VkImageViewCreateInfo imageViewCreateInfo{
				    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				    .pNext            = nullptr, // optional
				    .flags            = 0,       // optional
				    .image            = vk_resource_info.as.image,
				    .viewType         = VK_IMAGE_VIEW_TYPE_2D,
				    .format           = VkFormat( imageFormat ),
				    .components       = {}, // default component mapping
				    .subresourceRange = subresourceRange,
				};

				VkImageView imageView = nullptr;
				vkCreateImageView( device, &imageViewCreateInfo, nullptr, &imageView );

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

	frame.textures_per_pass.resize( numRenderPasses );

	// Create Samplers for all images which are used as Textures
	//
	for ( size_t pass_idx = 0; pass_idx != numRenderPasses; ++pass_idx ) {

		auto& p = passes[ pass_idx ];

		// Get all texture names for this pass
		const le_texture_handle* textureIds     = nullptr;
		size_t                   textureIdCount = 0;
		renderpass_i.get_texture_ids( p, &textureIds, &textureIdCount );

		const le_image_sampler_info_t* textureInfos     = nullptr;
		size_t                         textureInfoCount = 0;
		renderpass_i.get_texture_infos( p, &textureInfos, &textureInfoCount );

		assert( textureIdCount == textureInfoCount ); // texture info and -id count must be identical, as there is a 1:1 relationship

		for ( size_t i = 0; i != textureIdCount; i++ ) {

			// -- find out if texture with this name has already been alloacted.
			// -- if not, allocate

			const le_texture_handle textureId = textureIds[ i ];

			if ( frame.textures_per_pass[ pass_idx ].find( textureId ) == frame.textures_per_pass[ pass_idx ].end() ) {
				// -- we need to allocate a new texture

				auto& texInfo = textureInfos[ i ];

				VkImageView imageView{};
				{
					// Set or create vkImageview

					auto const& imageFormat = le::Format( frame_data_get_image_format_from_texture_info( frame, texInfo ) );

					VkImageSubresourceRange subresourceRange{
					    .aspectMask     = get_aspect_flags_from_format( imageFormat ),
					    .baseMipLevel   = 0,
					    .levelCount     = VK_REMAINING_MIP_LEVELS, // we set VK_REMAINING_MIP_LEVELS which activates all mip levels remaining.
					    .baseArrayLayer = texInfo.imageView.base_array_layer,
					    .layerCount     = VK_REMAINING_ARRAY_LAYERS, // Fixme: texInfo.imageView.layer_count must be 6 if imageView.type is cubemap
					};

					// TODO: fill in additional image view create info based on info from pass...

					VkImageViewCreateInfo imageViewCreateInfo{
					    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					    .pNext            = nullptr, // optional
					    .flags            = 0,       // optional
					    .image            = frame_data_get_image_from_le_resource_id( frame, texInfo.imageView.imageId ),
					    .viewType         = VkImageViewType( texInfo.imageView.image_view_type ),
					    .format           = VkFormat( imageFormat ),
					    .components       = {}, // default component mapping
					    .subresourceRange = subresourceRange,
					};

					vkCreateImageView( device, &imageViewCreateInfo, nullptr, &imageView );

					// Store vk object references with frame-owned resources, so that
					// the vk objects can be destroyed when frame crosses the fence.

					AbstractPhysicalResource res;
					res.asImageView = imageView;
					res.type        = AbstractPhysicalResource::Type::eImageView;

					frame.ownedResources.emplace_front( std::move( res ) );
				}

				VkSampler sampler{};
				{
					// Create VkSampler object on device.

					VkSamplerCreateInfo samplerCreateInfo{
					    .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
					    .pNext                   = nullptr, // optional
					    .flags                   = 0,       // optional
					    .magFilter               = VkFilter( texInfo.sampler.magFilter ),
					    .minFilter               = VkFilter( texInfo.sampler.minFilter ),
					    .mipmapMode              = VkSamplerMipmapMode( texInfo.sampler.mipmapMode ),
					    .addressModeU            = VkSamplerAddressMode( texInfo.sampler.addressModeU ),
					    .addressModeV            = VkSamplerAddressMode( texInfo.sampler.addressModeV ),
					    .addressModeW            = VkSamplerAddressMode( texInfo.sampler.addressModeW ),
					    .mipLodBias              = texInfo.sampler.mipLodBias,
					    .anisotropyEnable        = texInfo.sampler.anisotropyEnable,
					    .maxAnisotropy           = texInfo.sampler.maxAnisotropy,
					    .compareEnable           = texInfo.sampler.compareEnable,
					    .compareOp               = VkCompareOp( texInfo.sampler.compareOp ),
					    .minLod                  = texInfo.sampler.minLod,
					    .maxLod                  = texInfo.sampler.maxLod,
					    .borderColor             = VkBorderColor( texInfo.sampler.borderColor ),
					    .unnormalizedCoordinates = texInfo.sampler.unnormalizedCoordinates,
					};

					vkCreateSampler( device, &samplerCreateInfo, nullptr, &sampler );
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

				frame.textures_per_pass[ pass_idx ][ textureId ] = tex;
			} else {
				// The frame already has an element with such a texture id.
				assert( false && "texture must have been defined multiple times using identical id within the same renderpass." );
			}
		} // end for all textureIds
	}     // end for all passes
}

// ----------------------------------------------------------------------
// This is one of the most important methods of backend -
// where we associate virtual with physical resources, allocate physical
// resources as needed, and keep track of sync state of physical resources.
static bool backend_acquire_physical_resources( le_backend_o*             self,
                                                size_t                    frameIndex,
                                                le_renderpass_o**         passes,
                                                size_t                    numRenderPasses,
                                                le_resource_handle const* declared_resources,
                                                le_resource_info_t const* declared_resources_infos,
                                                size_t const&             declared_resources_count ) {

	auto& frame = self->mFrames[ frameIndex ];

	// We try to acquire all images, even if one of the acquisitions fails.
	//
	// This is so that every semaphore for presentComplete is correctly
	// waited upon.

	bool acquire_success = true;

	using namespace le_swapchain_vk;

	for ( size_t i = 0; i != self->swapchains.size(); ++i ) {
		if ( !swapchain_i.acquire_next_image(
		         self->swapchains[ i ],
		         frame.swapchain_state[ i ].presentComplete,
		         &frame.swapchain_state[ i ].image_idx ) ) {
			acquire_success                               = false;
			frame.swapchain_state[ i ].acquire_successful = false;
		} else {
			frame.swapchain_state[ i ].acquire_successful = true;
		}
	}

	if ( !acquire_success ) {
		return false;
	}

	// ----------| invariant: swapchain image acquisition was successful.

#ifndef NDEBUG
	// If we're running in debug, there is a chance that we might want to print out
	// dot graph diagrams for queue sync - in which case we should fetch the global
	// setting telling us whether to do so or not.
	LE_SETTING( uint32_t, LE_SETTING_GENERATE_QUEUE_SYNC_DOT_FILES, 0 );
	// we fetch this variable to a local copy, and only at a single point, here,
	// so that there is no risk that the value of the variable is changed on
	// another thread while the dot graph is being generated
	frame.must_create_queues_dot_graph = ( *LE_SETTING_GENERATE_QUEUE_SYNC_DOT_FILES > 0 );
	// then, we decrement the number of requested queue sync dot files, so that we only generate
	// as many dot graph files as requested.
	if ( *LE_SETTING_GENERATE_QUEUE_SYNC_DOT_FILES > 0 ) {
		--( *LE_SETTING_GENERATE_QUEUE_SYNC_DOT_FILES );
	};
#endif

	// Setup declared resources per frame - These are resources declared using resource infos
	// which are explicitly declared by user via the rendergraph, but which may or may not be
	// actually used in the frame.
	//
	frame.declared_resources_id   = { declared_resources, declared_resources + declared_resources_count };
	frame.declared_resources_info = { declared_resources_infos, declared_resources_infos + declared_resources_count };

	for ( size_t i = 0; i != self->swapchains.size(); ++i ) {
		// Acquire swapchain image

		// ----------| invariant: swapchain acquisition successful.

		frame.swapchain_state[ i ].surface_width  = swapchain_i.get_image_width( self->swapchains[ i ] );
		frame.swapchain_state[ i ].surface_height = swapchain_i.get_image_height( self->swapchains[ i ] );

		auto const& img_resource_handle = self->swapchain_resources[ i ];

		frame.availableResources[ img_resource_handle ].as.image =
		    swapchain_i.get_image( self->swapchains[ i ], frame.swapchain_state[ i ].image_idx );

		{
			auto& backbufferInfo = frame.availableResources[ img_resource_handle ].info.imageInfo;
			backbufferInfo       = {
			          .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			          .pNext     = nullptr, // optional
			          .flags     = 0,       // optional
			          .imageType = VK_IMAGE_TYPE_2D,
			          .format    = VkFormat( self->swapchainImageFormat[ i ] ),
			          .extent    = {
			                 .width  = frame.swapchain_state[ i ].surface_width,
			                 .height = frame.swapchain_state[ i ].surface_height,
			                 .depth  = 1,
                },
			          .mipLevels             = 1,
			          .arrayLayers           = 1,
			          .samples               = VK_SAMPLE_COUNT_1_BIT,
			          .tiling                = VK_IMAGE_TILING_OPTIMAL,
			          .usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			          .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
			          .queueFamilyIndexCount = 0, // optional
			          .pQueueFamilyIndices   = nullptr,
			          .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
            };
		}

		// Add swapchain resource to automatically- declared resources
		// We add this so that usage type is automatically defined for this Resource as ColorAttachment.

		frame.declared_resources_id.push_back( img_resource_handle );
		frame.declared_resources_info.push_back(
		    le::ImageInfoBuilder()
		        .addUsageFlags( le::ImageUsageFlags( le::ImageUsageFlagBits::eColorAttachment ) )
		        .setExtent(
		            frame.swapchain_state[ i ].surface_width,
		            frame.swapchain_state[ i ].surface_height,
		            1 )
		        .build() );
	}

	if ( !frame.swapchain_state.empty() ) {

		// For all passes - set pass width/height to swapchain width/height if not known.
		// Only extents of swapchain[0] are used to infer extents for renderpasses which lack extents info
		patch_renderpass_extents(
		    passes,
		    numRenderPasses,
		    frame.swapchain_state[ 0 ].surface_width,
		    frame.swapchain_state[ 0 ].surface_height );
	}

	// Note: this consumes frame.declared_resources
	backend_allocate_resources( self, frame, passes, numRenderPasses );

	{
		// Initialise, then build sync chain table - each resource receives initial state
		// from current entry in frame.availableResources resource map -

		frame.syncChainTable.clear();
		for ( auto const& res : frame.availableResources ) {
			frame.syncChainTable.insert( { res.first, { res.second.state } } );
		}

		// -- build sync chain for each resource, create explicit sync barrier requests for resources
		// which cannot be impliciltly synced.
		frame_track_resource_state( frame, passes, numRenderPasses, self->swapchain_resources );

		// At this point we know the state for each resource at the end of the sync chain.
		// this state will be the initial state for the resource

		{
			// Update final sync state for each pre-existing backend resource.
			// fixme: this breaks the promise that no-one but allocate resources is writing to allocatedResources.

			static le_resource_handle LE_RTX_SCRATCH_BUFFER_HANDLE = LE_BUF_RESOURCE( "le_rtx_scratch_buffer_handle" ); // opaque handle for rtx scratch buffer

			auto& backendResources = self->only_backend_allocate_resources_may_access.allocatedResources;
			for ( auto const& tbl : frame.syncChainTable ) {
				auto const& resId       = tbl.first;
				auto const& resSyncList = tbl.second;

				assert( !resSyncList.empty() ); // sync list must have entries

				// find element with matching resource ID in list of backend resources

				auto res = backendResources.find( resId );
				if ( res != backendResources.end() ) {
					// Element found.
					// Set sync state for this resource to value of last elment in the sync chain.
					res->second.state = resSyncList.back();
				} else {

					assert( std::find( self->swapchain_resources.begin(), self->swapchain_resources.end(), resId ) != self->swapchain_resources.end() ||
					        resId == LE_RTX_SCRATCH_BUFFER_HANDLE );

					// Frame local resource must be available as a backend resource,
					// unless the resource is the swapchain image handle, which is owned and managed
					// by the swapchain.
					// Another exception is LE_RTX_SCRATCH_BUFFER, which is a transient resource,
					// and as such does not end up in backendResources, but starts out directly
					// as a binned resource.
					// Otherwise something fishy is going on.
				}
			}

			// If we use a mutex to protect backend-wide resources, we can release it now.
		}
	}

	VkDevice device = self->device->getVkDevice();

	// -- allocate any transient vk objects such as image samplers, and image views
	frame_allocate_transient_resources( frame, device, passes, numRenderPasses );

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
static le_allocator_o** backend_get_transient_allocators( le_backend_o* self, size_t frameIndex ) {
	return self->mFrames[ frameIndex ].allocators.data();
}

// ----------------------------------------------------------------------
static le_allocator_o** backend_create_transient_allocators( le_backend_o* self, size_t frameIndex, size_t numAllocators ) {

	using namespace le_backend_vk;

	auto&                           frame                         = self->mFrames[ frameIndex ];
	static const VkBufferUsageFlags LE_BUFFER_USAGE_FLAGS_SCRATCH = defaults_get_buffer_usage_scratch();

	for ( size_t i = frame.allocators.size(); i != numAllocators; ++i ) {

		assert( numAllocators < 256 ); // must not have more than 255 allocators, otherwise we cannot store index in LeResourceHandleMeta.

		VkBuffer          buffer = nullptr;
		VmaAllocation     allocation;
		VmaAllocationInfo allocationInfo;

		VmaAllocationCreateInfo createInfo{};
		createInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
		createInfo.pool  = frame.allocationPool; // Since we're allocating from a pool all fields but .flags will be taken from the pool

		le_buf_resource_handle res = declare_resource_virtual_buffer( uint8_t( i ) );

		createInfo.pUserData = res;

		VkBufferCreateInfo bufferCreateInfo{
		    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		    .pNext                 = nullptr, // optional
		    .flags                 = 0,       // optional
		    .size                  = LE_LINEAR_ALLOCATOR_SIZE,
		    .usage                 = LE_BUFFER_USAGE_FLAGS_SCRATCH,
		    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		    .queueFamilyIndexCount = 0,
		    .pQueueFamilyIndices   = nullptr,
		};

		auto result = vmaCreateBuffer( self->mAllocator, &bufferCreateInfo, &createInfo, &buffer, &allocation, &allocationInfo );

		assert( result == VK_SUCCESS ); // todo: deal with failed allocation

		// Create a new allocator - note that we assume an alignment of 256 bytes
		le_allocator_o* allocator = le_allocator_linear_i.create( &allocationInfo, 256 );

		frame.allocators.emplace_back( allocator );
		frame.allocatorBuffers.emplace_back( std::move( buffer ) );
		frame.allocations.emplace_back( std::move( allocation ) );
		frame.allocationInfos.emplace_back( std::move( allocationInfo ) );
	}

	return frame.allocators.data();
}

// ----------------------------------------------------------------------

static le_staging_allocator_o* backend_get_staging_allocator( le_backend_o* self, size_t frameIndex ) {
	return self->mFrames[ frameIndex ].stagingAllocator;
}

void debug_print_le_pipeline_layout_info( le_pipeline_layout_info* info ) {
	static auto logger = LeLog( LOGGER_LABEL );
	logger.debug( "pipeline layout: %x", info->pipeline_layout_key );
	for ( size_t i = 0; i != info->set_layout_count; i++ ) {
		logger.debug( "set layout key : %x", info->set_layout_keys[ i ] );
	}
}

static bool is_equal( le_pipeline_and_layout_info_t const& lhs, le_pipeline_and_layout_info_t const& rhs ) {
	return lhs.pipeline == rhs.pipeline &&
	       lhs.layout_info.set_layout_count == rhs.layout_info.set_layout_count &&
	       0 == memcmp( lhs.layout_info.set_layout_keys, rhs.layout_info.set_layout_keys, sizeof( uint64_t ) * lhs.layout_info.set_layout_count ) &&
	       lhs.layout_info.active_vk_shader_stages == rhs.layout_info.active_vk_shader_stages;
}

static bool updateArguments( const VkDevice&                    device,
                             const VkDescriptorPool&            descriptorPool_,
                             const ArgumentState&               argumentState,
                             std::array<DescriptorSetState, 8>& previousSetData,
                             VkDescriptorSet*                   descriptorSets ) {

	static auto logger = LeLog( LOGGER_LABEL );
	// -- allocate descriptors from descriptorpool based on set layout info

	if ( argumentState.setCount == 0 ) {
		return true;
	}

	// ----------| invariant: there are descriptorSets to allocate

	bool argumentsOk = true;

	auto get_argument_name = [ &argumentState ]( size_t set_id, uint32_t binding_number ) -> char const* {
		for ( auto const& b : argumentState.binding_infos ) {
			if ( b.binding == binding_number && b.setIndex == set_id ) {
				return le_get_argument_name_from_hash( b.name_hash );
			}
		}

		// ---------| invariant: not found

		return nullptr;
	};

	// -- write data from descriptorSetData into freshly allocated DescriptorSets
	for ( size_t setId = 0; setId != argumentState.setCount; ++setId ) {

		// If argumentState contains invalid information (for example if an uniform has not been set yet)
		// this will lead to SEGFAULT. You must ensure that argumentState contains valid information.
		//
		// The most common case for this bug is not providing any data for a uniform used in the shader,
		// we check for this and skip any argumentStates which have invalid data...

		static constexpr auto NULL_VK_BUFFER                     = VkBuffer( nullptr );
		static constexpr auto NULL_VK_IMAGE_VIEW                 = VkImageView( nullptr );
		static constexpr auto NULL_VK_ACCELERATION_STRUCTURE_KHR = VkAccelerationStructureKHR( nullptr );

		for ( auto& a : argumentState.setData[ setId ] ) {

			switch ( a.type ) {
			case le::DescriptorType::eStorageBufferDynamic: //
			case le::DescriptorType::eUniformBuffer:        //
			case le::DescriptorType::eUniformBufferDynamic: //
			case le::DescriptorType::eStorageBuffer:        // fall-through
				if ( NULL_VK_BUFFER == a.bufferInfo.buffer ) {
					// if buffer must have valid buffer bound
					logger.error( "Buffer argument '%s', at set=%d, binding=%d, array_index=%d not set, not valid, or missing.",
					              get_argument_name( setId, a.bindingNumber ),
					              setId,
					              a.bindingNumber,
					              a.arrayIndex );
					argumentsOk = false;
				}
				break;
			case le::DescriptorType::eCombinedImageSampler:
			case le::DescriptorType::eSampledImage:
			case le::DescriptorType::eStorageImage:
				argumentsOk &= ( NULL_VK_IMAGE_VIEW != a.imageInfo.imageView ); // if sampler, must have valid image view
				if ( NULL_VK_IMAGE_VIEW == a.imageInfo.imageView ) {
					// if image - must have valid imageview bound
					logger.error( "Image argument '%s', at set=%d, binding=%d, array_index=%d not set, not valid, or missing.",
					              get_argument_name( setId, a.bindingNumber ),
					              setId,
					              a.bindingNumber,
					              a.arrayIndex );
					argumentsOk = false;
				}
				break;
			case le::DescriptorType::eAccelerationStructureKhr:
				argumentsOk &= ( NULL_VK_ACCELERATION_STRUCTURE_KHR != a.accelerationStructureInfo.accelerationStructure );
				if ( NULL_VK_ACCELERATION_STRUCTURE_KHR == a.accelerationStructureInfo.accelerationStructure ) {
					// if image - must have valid acceleration structure bound
					logger.error( "Acceleration Structure argument '%s', at set=%d, binding=%d, array_index=%d not set, not valid, or missing.",
					              get_argument_name( setId, a.bindingNumber ),
					              setId,
					              a.bindingNumber,
					              a.arrayIndex );
					argumentsOk = false;
				}

				break;
			default:
				argumentsOk &= false;
				// TODO: check arguments for other types of descriptors
				assert( false && "unhandled descriptor type" );
				break;
			}

			if ( false == argumentsOk ) {
				assert( false && "Argument state did not fit template" );
				break;
			}
		}

		if ( argumentsOk ) {

			// We test the current argument state of descriptors against the currently bound
			// descriptors - we only (re-)allocate descriptorsets for when we detect a change
			// within one of these sets.

			// FIXME: there is a subtle bug here - if setData is the same between arguments we
			// should theoretically be able to recycle the descriptorset - but beware! If the
			// decriptorset requires a different layout, then you must re-allocate. This can
			// happen when descriptors differ in usage flags, for example (vertex|fragment vs. vertex)
			// -- in such a case the parameters are the same between two descriptorSets,
			// but the descriptorSetLayouts will be different, and you must allcate the matching
			// descriptorSet.

			if ( previousSetData[ setId ].setData.empty() ||
			     previousSetData[ setId ].setData != argumentState.setData[ setId ] ||
			     previousSetData[ setId ].setLayout != argumentState.layouts[ setId ] ) {

				VkDescriptorSetAllocateInfo allocateInfo{
				    .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				    .pNext              = nullptr, // optional
				    .descriptorPool     = descriptorPool_,
				    .descriptorSetCount = 1,
				    .pSetLayouts        = &argumentState.layouts[ setId ],
				};

				// -- allocate descriptorSets based on current layout
				// and place them in the correct position

				auto result = vkAllocateDescriptorSets( device, &allocateInfo, &descriptorSets[ setId ] );

				assert( result == VK_SUCCESS && "failed to allocate descriptor set" );

				if ( /* DISABLES CODE */ ( false ) ) {
					// I wish that this would work - but it appears that accelerator decriptors cannot be updated using templates.
					vkUpdateDescriptorSetWithTemplate( device, descriptorSets[ setId ], argumentState.updateTemplates[ setId ], argumentState.setData[ setId ].data() );

				} else {

					std::vector<VkWriteDescriptorSet> write_descriptor_sets;

					// We deliberately allocate write descriptor set acceleration structure objects on the heap,
					// so that the pointer to the object will not change if and when the vector grows.
					//
					// This means that we can hand out copies of pointers from this vector without fear from
					// within the current scope, but also that we must clean up the contents of the vector
					// manually before leaving the current scope or else we will leak these objects.
					std::vector<VkWriteDescriptorSetAccelerationStructureKHR*> write_acceleration_structures;

					write_descriptor_sets.reserve( argumentState.setData[ setId ].size() );

					for ( auto& a : argumentState.setData[ setId ] ) {
						VkWriteDescriptorSet w{
						    .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						    .pNext            = nullptr, // optional
						    .dstSet           = descriptorSets[ setId ],
						    .dstBinding       = a.bindingNumber,
						    .dstArrayElement  = a.arrayIndex,
						    .descriptorCount  = 1,
						    .descriptorType   = VkDescriptorType( a.type ),
						    .pImageInfo       = 0,
						    .pBufferInfo      = 0,
						    .pTexelBufferView = 0,
						};
						;

						switch ( a.type ) {
						case le::DescriptorType::eSampler:
						case le::DescriptorType::eCombinedImageSampler:
						case le::DescriptorType::eSampledImage:
						case le::DescriptorType::eStorageImage:
						case le::DescriptorType::eInputAttachment:
							w.pImageInfo = reinterpret_cast<VkDescriptorImageInfo const*>( &a.imageInfo );
							break;
						case le::DescriptorType::eUniformTexelBuffer:
						case le::DescriptorType::eStorageTexelBuffer:
							w.pTexelBufferView = reinterpret_cast<VkBufferView const*>( &a.texelBufferInfo );
							break;
						case le::DescriptorType::eUniformBuffer:
						case le::DescriptorType::eStorageBuffer:
						case le::DescriptorType::eUniformBufferDynamic:
						case le::DescriptorType::eStorageBufferDynamic:
							w.pBufferInfo = reinterpret_cast<VkDescriptorBufferInfo const*>( &a.bufferInfo );
							break;
						case le::DescriptorType::eInlineUniformBlockExt:
							assert( false && "inline uniform blocks are not yet supported" );
							break;
						case le::DescriptorType::eAccelerationStructureNv:
							assert( false && "NV acceleration structures are not supported anymore. Use KHR acceleration structures." );
							break;
						case le::DescriptorType::eAccelerationStructureKhr: {
							// FIXME: use an arena for that - we don't want to allocate on the free store
							auto wd = new VkWriteDescriptorSetAccelerationStructureKHR{
							    .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
							    .pNext                      = nullptr, // optional
							    .accelerationStructureCount = 1,
							    .pAccelerationStructures    = &reinterpret_cast<VkAccelerationStructureKHR const&>( a.accelerationStructureInfo.accelerationStructure ),
							};
							w.pNext = wd;
							write_acceleration_structures.push_back( wd );

						} break;
						default:
							assert( false && "Unhandled descriptor Type" );
						}

						write_descriptor_sets.emplace_back( w );
					}
					vkUpdateDescriptorSets( device, uint32_t( write_descriptor_sets.size() ), write_descriptor_sets.data(), 0, nullptr );

					// We must manually delete any WriteDescriptorSetAccelerationStructureKHR objects
					for ( auto& w : write_acceleration_structures ) {
						delete ( w );
					}
				}
				previousSetData[ setId ].setData   = argumentState.setData[ setId ];
				previousSetData[ setId ].setLayout = argumentState.layouts[ setId ];
			}

		} else {
			return false;
		}
	}

	return argumentsOk;
};

// ----------------------------------------------------------------------

static void debug_print_command( void*& cmd ) {
	static auto        logger = LeLog( LOGGER_LABEL );
	std::ostringstream os;
	os << "cmd: ";

	auto cmd_header = static_cast<le::CommandHeader*>( cmd );

	// clang-format off
			switch (cmd_header->info.type){
                case (le::CommandType::eDrawIndexed): os << "eDrawIndexed"; break;
                case (le::CommandType::eDraw): os << "eDraw"; break;
                case (le::CommandType::eDispatch): os << "eDispatch"; break;
                case (le::CommandType::eBufferMemoryBarrier): os << "eBufferMemoryBarrier"; break;
                case (le::CommandType::eSetLineWidth): os << "eSetLineWidth"; break;
                case (le::CommandType::eSetViewport): os << "eSetViewport"; break;
                case (le::CommandType::eSetScissor): os << "eSetScissor"; break;
                case (le::CommandType::eSetPushConstantData): os << "eSetPushConstantData"; break;
                case (le::CommandType::eBindArgumentBuffer): os << "eBindArgumentBuffer"; break;
                case (le::CommandType::eSetArgumentTexture): os << "eSetArgumentTexture"; break;
                case (le::CommandType::eSetArgumentImage): os << "eSetArgumentImage"; break;
                case (le::CommandType::eBindIndexBuffer): os << "eBindIndexBuffer"; break;
                case (le::CommandType::eBindVertexBuffers): os << "eBindVertexBuffers"; break;
                case (le::CommandType::eBindGraphicsPipeline): os << "eBindGraphicsPipeline"; break;
                case (le::CommandType::eBindComputePipeline): os << "eBindComputePipeline"; break;
                case (le::CommandType::eWriteToBuffer): os << "eWriteToBuffer"; break;
                case (le::CommandType::eBindRtxPipeline): os << "eBindRtxPipeline" ; break;
                case (le::CommandType::eBuildRtxTlas): os << "eBuildRtxTlas"; break;
                case (le::CommandType::eBuildRtxBlas): os << "eBuildRtxBlas"; break;
                case (le::CommandType::eWriteToImage): os << "eWriteToImage"; break;
                case (le::CommandType::eDrawMeshTasks): os << "eDrawMeshTasks"; break;
                case (le::CommandType::eTraceRays): os << "eTraceRays"; break;
                case (le::CommandType::eSetArgumentTlas): os << "eSetArgumentTlas"; break;
			}
	// clang-format on

	if ( cmd_header->info.type == le::CommandType::eBindGraphicsPipeline ) {
		auto le_cmd = static_cast<le::CommandBindGraphicsPipeline*>( cmd );
		os << " [" << std::hex << le_cmd->info.gpsoHandle << "]";
	}

	logger.info( "%s", os.str().c_str() );
};

// convert 0xrrggbbaa color to float color
constexpr static std::array<float, 4> hex_rgba_to_float_colour( uint32_t const& hex ) {
	std::array<float, 4> result{
	    ( hex >> 24 ) / 255.f,
	    ( ( hex >> 16 ) & 0xff ) / 255.f,
	    ( ( hex >> 8 ) & 0xff ) / 255.f,
	    ( ( hex )&0xff ) / 255.f,
	};
	return result;
}

static BackendFrameData::CommandPool* backend_frame_data_produce_command_pool(
    BackendFrameData& frame,
    uint32_t          queue_family_index,
    VkDevice          device ) {

	BackendFrameData::CommandPool* pool = nullptr;

	// search through available command pools to see if we could recycle one with a matching
	// queue family index.

	for ( auto& existing_pool : frame.available_command_pools ) {
		if ( false == existing_pool->is_used && existing_pool->vk_queue_family_idx == queue_family_index ) {
			pool          = existing_pool;
			pool->is_used = true; // mark this command pool as used
			break;
		}
	}

	if ( pool == nullptr ) {
		// we couldn't find a command pool, we must allocate a new one.

		pool = new BackendFrameData::CommandPool{};

		VkCommandPoolCreateInfo create_info = {
		    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		    .pNext            = nullptr,                              // optional
		    .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, // optional
		    .queueFamilyIndex = queue_family_index,
		};

		vkCreateCommandPool( device, &create_info, nullptr, &pool->pool );
		le::Log( LOGGER_LABEL ).info( "Created CommandPool %p", pool->pool );

		frame.available_command_pools.push_back( pool );
		pool->vk_queue_family_idx = queue_family_index;
		pool->is_used             = true; // mark this command pool as used
	}

	assert( pool && "must either find or create valid command pool" );
	return pool;
}

// predictable, repeatable response given queue flags
static uint32_t backend_find_queue_family_index_from_requirements( le_backend_o* self, VkQueueFlags flags ) {

	uint32_t lowest_num_extra_bits = ~uint32_t( 0 );
	uint32_t found_family          = ~uint32_t( 0 );

	static std::unordered_map<VkQueueFlags, uint32_t> cache;

	auto cache_entry = cache.emplace( flags, 0 );

	if ( cache_entry.second == true ) {
		// element was inserted, this was not a cache hit
		for ( BackendQueueInfo const* info : self->queues ) {
			VkQueueFlags available_flags = info->queue_flags;
			VkQueueFlags requested_flags = flags;

			if ( ( available_flags & requested_flags ) == requested_flags ) {
				// requested_flags are contained in available flags
				VkQueueFlags leftover_flags = ( available_flags & ( ~requested_flags ) ); // flags which only appear in available_flags
				size_t       num_extra_bits = std::bitset<sizeof( VkQueueFlags ) * 8>( leftover_flags ).count();
				if ( num_extra_bits < lowest_num_extra_bits ) {
					found_family          = info->queue_family_index;
					lowest_num_extra_bits = num_extra_bits;
				}
			}
		}
		cache_entry.first->second = found_family;
	}

	return cache_entry.first->second;
}

// ----------------------------------------------------------------------
// Decode commandStream for each pass (may happen in parallel)
// translate into vk specific commands.
static void backend_process_frame( le_backend_o* self, size_t frameIndex ) {

	static auto logger = LeLog( LOGGER_LABEL );

	if ( LE_PRINT_DEBUG_MESSAGES ) {
		logger.debug( "** Process Frame #%8d **", frameIndex );
	}

	using namespace le_renderer;   // for encoder
	using namespace le_backend_vk; // for device

	auto& frame = self->mFrames[ frameIndex ];

	// Only insert debug labels iff validation layers are active - otherwise we will get errors.
	// Debug labels are useful for RenderDoc, for example.
	bool const SHOULD_INSERT_DEBUG_LABELS = self->instance->is_using_validation_layers;

	VkDevice device = self->device->getVkDevice();

	static_assert( sizeof( VkViewport ) == sizeof( le::Viewport ), "Viewport data size must be same in vk and le" );
	static_assert( sizeof( VkRect2D ) == sizeof( le::Rect2D ), "Rect2D data size must be same in vk and le" );

	static auto maxVertexInputBindings = vk_device_i.get_vk_physical_device_properties( *self->device )->limits.maxVertexInputBindings;

	bool needs_to_collect_root_pass_names = frame.must_create_queues_dot_graph; // only collect root pass names when these are needed, for example in order to create dot graphs or debug printouts

	{

		// -- Collect command buffers for each queue submission by testing against queue submission key.
		//    if a pass's affinity matches the submission key, it belongs to that particular queue submission.
		// -- And collect pass indices per queue submission

		size_t num_invocation_keys = frame.queue_submission_keys.size();

		for ( size_t i = 0; i != num_invocation_keys; i++ ) {

			auto const& key = frame.queue_submission_keys[ i ];

			BackendFrameData::PerQueueSubmissionData submission_data{};

			for ( size_t pi = 0; pi != frame.passes.size(); pi++ ) {

				auto const& pass = frame.passes[ pi ];

				if ( key & pass.root_passes_affinity ) {
					submission_data.queue_flags |= VkQueueFlags( pass.type ); // Accumulate queue flags over submission - queue capabilities must be superset
					submission_data.pass_indices.push_back( uint32_t( pi ) );
				}
			}

			if ( needs_to_collect_root_pass_names ) {
				for ( size_t j = 0; j != frame.debug_root_passes_names.size(); j++ ) {
					if ( key & ( uint64_t( 1 ) << j ) ) {
						if ( !submission_data.debug_root_passes_names.empty() ) {
							submission_data.debug_root_passes_names.append( " | " );
						}
						submission_data.debug_root_passes_names.append( frame.debug_root_passes_names[ j ] );
					}
				}
			}

			if ( !submission_data.pass_indices.empty() ) {
				frame.queue_submission_data.push_back( submission_data );
			}
		}

		assert( num_invocation_keys == frame.queue_submission_data.size() && "must have one submission data element per invocaton key" );

		// -- Control that resources may only be used by the same queue family per-frame.
		// we adjust for this by making the queue requirements a superset of all resource queue usages,
		// and accumulating all queue usages per-resource first.

		if ( self->must_track_resources_queue_family_ownership ) {
			// We only must do this if we have multiple queue families active
			// as this can get pretty expensive if there are many resources flying around.

			// We do this to prevent a situation where a resource is claimed by two or more queue families
			// Ideally, this

			// for each resource, accumulate all queue type flags that it gets used with over all submissions

			std::unordered_map<le_resource_handle, VkQueueFlags> resource_queue_flags;

			for ( auto const& qs : frame.queue_submission_data ) {
				for ( auto const& pi : qs.pass_indices ) {
					for ( auto const& r : frame.passes[ pi ].resources ) {
						resource_queue_flags[ r ] |= qs.queue_flags;
					}
				}
			}

			// now accumulate submission's queue flags based on the queue flags that all its resources have

			for ( auto& qs : frame.queue_submission_data ) {
				for ( auto const& pi : qs.pass_indices ) {
					VkQueueFlags flags = qs.queue_flags;
					for ( auto const& r : frame.passes[ pi ].resources ) {
						flags |= resource_queue_flags.at( r );
					}
					qs.queue_flags = flags;
				}
			}
		}

		{
			/// Assign queues to each submission:
			///
			/// we must have a unique mapping from queue_flags to queue family.
			///
			/// from this, we can then go through all queues of the queue family
			/// and pick the queue with the least submissions.
			///
			std::vector<uint32_t> num_submissions_per_queue( num_invocation_keys, 0 );
			for ( size_t i = 0; i != num_invocation_keys; i++ ) {

				auto const& queues = self->queues;
				auto const& flags  = frame.queue_submission_data[ i ].queue_flags;

				int      matching_queue          = -1;
				uint32_t lowest_submission_count = uint32_t( ~0 );

				uint32_t matching_queue_family_index = backend_find_queue_family_index_from_requirements( self, flags );

				// try to find an exact match with the lowest submissions to it
				for ( uint32_t j = 0; j != queues.size(); j++ ) {
					if ( queues[ j ]->queue_family_index == matching_queue_family_index ) {
						// all flags are contained in q.queue_flags
						if ( num_submissions_per_queue[ j ] < lowest_submission_count ) {
							matching_queue          = j;
							lowest_submission_count = num_submissions_per_queue[ j ];
						}
					}
				}

				if ( matching_queue == -1 ) {
					le::Log( LOGGER_LABEL ).error( "Could not find matching queue with capability: %s\n"
					                               "This could be caused by one or more queue families claiming ownership of the same resource.",
					                               to_string_vk_queue_flags( flags ).c_str() );
				}

				assert( matching_queue != -1 && "must have found matching queue" );

				num_submissions_per_queue[ matching_queue ]++;

				frame.queue_submission_data[ i ].queue_idx = matching_queue;
			}
		}

		// for each submission data, allocate pool from the correct queue

		for ( auto& data : frame.queue_submission_data ) {
			// this submission has passes
			// find available command pool which has the correct queue family.

			data.command_pool = backend_frame_data_produce_command_pool( frame, self->queues[ data.queue_idx ]->queue_family_index, self->device->getVkDevice() );
			{
				VkCommandBufferAllocateInfo info = {
				    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				    .pNext              = nullptr, // optional
				    .commandPool        = data.command_pool->pool,
				    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				    .commandBufferCount = uint32_t( data.pass_indices.size() ),
				};

				data.command_pool->buffers.resize( info.commandBufferCount );
				vkAllocateCommandBuffers( device, &info, data.command_pool->buffers.data() );
			}
		}

		if ( LE_PRINT_DEBUG_MESSAGES ) {
			logger.info( "Listing queue batches and their queue affinity:" );
			int i = 0;
			for ( auto const& qf : frame.queue_submission_data ) {
				logger.info( "#%i, [%-50s]", i, to_string_vk_queue_flags( qf.queue_flags ).c_str() );
				i++;
			}
			logger.info( "" );
		}
	}

	for ( auto const& submission : frame.queue_submission_data ) {
		std::array<VkClearValue, 16> clearValues{};
		// split graph into separate submissions by filtering by submission key
		// accumulate pass types to find out the needed queue flags for the
		//
		// create vectors of pass indices per submission
		// then assign each submission based on best-match to queues
		// submit submissions to queues in-order.

		// TODO: (parallel for)
		// note that access to any caches when creating pipelines and layouts and descriptorsets must be
		// mutex-controlled when processing happens concurrently.
		uint32_t buffer_index = 0;
		for ( auto const& passIndex : submission.pass_indices ) {

			auto& pass           = frame.passes[ passIndex ];
			auto& cmd            = submission.command_pool->buffers[ buffer_index++ ]; // note that we post-increment, so that next iteration will get next command buffer.
			auto& descriptorPool = frame.descriptorPools[ passIndex ];

			// create frame buffer, based on swapchain and renderpass

			{
				VkCommandBufferBeginInfo info = {
				    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				    .pNext            = nullptr,                                     // optional
				    .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // optional
				    .pInheritanceInfo = 0,                                           // optional
				};

				vkBeginCommandBuffer( cmd, &info );
			}

			if ( SHOULD_INSERT_DEBUG_LABELS ) {
				VkDebugUtilsLabelEXT labelInfo{
				    .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
				    .pNext      = nullptr, // optional
				    .pLabelName = pass.debugName,
				    .color      = {},
				};

				static constexpr auto LE_COLOUR_LIGHTBLUE    = hex_rgba_to_float_colour( 0x61BBEFFF );
				static constexpr auto LE_COLOUR_GREENY_BLUE  = hex_rgba_to_float_colour( 0x4EC9B0FF );
				static constexpr auto LE_COLOUR_BRICK_ORANGE = hex_rgba_to_float_colour( 0xCE4B0EFF );
				static constexpr auto LE_COLOUR_PALE_PEACH   = hex_rgba_to_float_colour( 0xFFDBA3FF );

				switch ( pass.type ) {
				case le::QueueFlagBits::eCompute:
					memcpy( labelInfo.color, LE_COLOUR_LIGHTBLUE.data(), sizeof( float ) * 4 );
					break;
				case le::QueueFlagBits::eGraphics:
					memcpy( labelInfo.color, LE_COLOUR_GREENY_BLUE.data(), sizeof( float ) * 4 );
					break;
				case le::QueueFlagBits::eTransfer:
					memcpy( labelInfo.color, LE_COLOUR_BRICK_ORANGE.data(), sizeof( float ) * 4 );
					break;
				default:
					break;
				}

				vkCmdBeginDebugUtilsLabelEXT( cmd, &labelInfo );
			}

			{

				if ( LE_PRINT_DEBUG_MESSAGES ) {
					logger.info( "*** Frame %d *** Queue %d *** Renderpass '%s'", frame.frameNumber, submission.queue_idx, pass.debugName );
				}

				// -- Issue sync barriers for all resources which require explicit sync.
				//
				// We must to this here, as the spec requires barriers to happen
				// before renderpass begin.
				//
				for ( auto const& op : pass.explicit_sync_ops ) {
					// fill in sync op

					if ( op.active == false ) {
						continue;
					}

					// ---------| invariant: barrier is active.

					auto const& syncChain = frame.syncChainTable[ op.resource ];

					auto const& stateInitial = syncChain[ op.sync_chain_offset_initial ];
					auto const& stateFinal   = syncChain[ op.sync_chain_offset_final ];

					if ( stateInitial != stateFinal ) {
						// we must issue an image barrier

						if ( LE_PRINT_DEBUG_MESSAGES ) {

							// --------| invariant: barrier is active.

							// print out sync chain for sampled image
							logger.info( "\t Explicit Barrier for: %s (s: %d)", op.resource->data->debug_name, 1 << op.resource->data->num_samples );
							logger.info( "\t % 3s : % 30s : % 30s : % 10s", "#", "visible_access", "write_stage", "layout" );

							auto const& syncChain = frame.syncChainTable.at( op.resource );

							for ( size_t i = op.sync_chain_offset_initial; i <= op.sync_chain_offset_final; i++ ) {
								auto const& s = syncChain[ i ];
								logger.info( "\t % 3d : % 30s : % 30s : % 10s", i,
								             to_string_vk_access_flags2( s.visible_access ).c_str(),
								             to_string_vk_pipeline_stage_flags2( s.stage ).c_str(),
								             to_str_vk_image_layout( s.layout ) );
							}
						}

						auto dstImage = frame_data_get_image_from_le_resource_id( frame, static_cast<le_img_resource_handle>( op.resource ) );

						VkImageMemoryBarrier2 imageLayoutTransfer{
						    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
						    .pNext               = nullptr,
						    .srcStageMask        = uint64_t( stateInitial.stage ) == 0 ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : stateInitial.stage, // happens-before
						    .srcAccessMask       = ( stateInitial.visible_access & ANY_WRITE_VK_ACCESS_2_FLAGS ),                                  // make available memory update from operation (in case it was a write operation, otherwise don't wait)
						    .dstStageMask        = stateFinal.stage,                                                                               // happens-after
						    .dstAccessMask       = stateFinal.visible_access,                                                                      // make visible
						    .oldLayout           = stateInitial.layout,
						    .newLayout           = stateFinal.layout,
						    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						    .image               = dstImage,
						    .subresourceRange    = LE_IMAGE_SUBRESOURCE_RANGE_ALL_MIPLEVELS,
						};

						VkDependencyInfo dependencyInfo = {
						    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
						    .pNext                    = nullptr, // optional
						    .dependencyFlags          = 0,       // optional
						    .memoryBarrierCount       = 0,       // optional
						    .pMemoryBarriers          = 0,
						    .bufferMemoryBarrierCount = 0, // optional
						    .pBufferMemoryBarriers    = 0,
						    .imageMemoryBarrierCount  = 1, // optional
						    .pImageMemoryBarriers     = &imageLayoutTransfer,
						};

						vkCmdPipelineBarrier2( cmd, &dependencyInfo );
					}
				} // end for all explicit sync ops.
			}

			// Draw passes must begin by opening a Renderpass context.
			if ( pass.type == le::QueueFlagBits::eGraphics && pass.renderPass ) {

				for ( uint32_t i = 0; i != ( pass.numColorAttachments + pass.numDepthStencilAttachments ); ++i ) {
					clearValues[ i ] = reinterpret_cast<VkClearValue&>( pass.attachments[ i ].clearValue );
				}

				VkRenderPassBeginInfo renderPassBeginInfo{
				    .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				    .pNext       = nullptr, // optional
				    .renderPass  = pass.renderPass,
				    .framebuffer = pass.framebuffer,
				    .renderArea  = {
				         .offset = { 0, 0 },
				         .extent = { pass.width, pass.height },
                    },
				    .clearValueCount = uint32_t( pass.numColorAttachments + pass.numDepthStencilAttachments ), // optional
				    .pClearValues    = clearValues.data(),
				};

				vkCmdBeginRenderPass( cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );
			}

			// -- Translate intermediary command stream data to api-native instructions

			void*    commandStream = nullptr;
			size_t   dataSize      = 0;
			size_t   numCommands   = 0;
			size_t   commandIndex  = 0;
			uint32_t subpassIndex  = 0;

			VkPipelineLayout currentPipelineLayout                          = nullptr;
			VkDescriptorSet  descriptorSets[ LE_MAX_BOUND_DESCRIPTOR_SETS ] = {}; // currently bound descriptorSets (allocated from pool, therefore we must not worry about freeing, and may re-use freely)

			// We store currently bound descriptors so that we only allocate new DescriptorSets
			// if the descriptors really change. With dynamic descriptors, it is very likely
			// that we don't need to allocate new descriptors, as the same descriptors are used
			// for different accessors, only with different dynamic binding offsets.
			//
			//
			std::array<DescriptorSetState, 8> previousSetState; // currently bound descriptorSetLayout+Data for each set
			ArgumentState                     argumentState{};  //
			RtxState                          rtx_state{};      // used to keep track of shader binding tables bound with rtx pipelines.

			static le_buf_resource_handle LE_RTX_SCRATCH_BUFFER_HANDLE = LE_BUF_RESOURCE( "le_rtx_scratch_buffer_handle" ); // opaque handle for rtx scratch buffer

			if ( pass.encoder ) {
				encoder_i.get_encoded_data( pass.encoder, &commandStream, &dataSize, &numCommands );
			} else {
				// This is legit behaviour for draw passes which are used only to clear attachments,
				// in which case they don't need to include any draw commands.
			}

			if ( commandStream != nullptr && numCommands > 0 ) {

				le_pipeline_manager_o* pipelineManager = encoder_i.get_pipeline_manager( pass.encoder );

				std::vector<VkBuffer>         vertexInputBindings( maxVertexInputBindings, nullptr );
				void*                         dataIt = commandStream;
				le_pipeline_and_layout_info_t currentPipeline{};

				while ( commandIndex != numCommands ) {

					auto header = static_cast<le::CommandHeader*>( dataIt );

					if ( /* DISABLES CODE */ ( false ) ) {
						// Print the command stream to stdout.
						debug_print_command( dataIt );
					}

					switch ( header->info.type ) {

					case le::CommandType::eBindGraphicsPipeline: {
						auto* le_cmd = static_cast<le::CommandBindGraphicsPipeline*>( dataIt );

						if ( pass.type == le::QueueFlagBits::eGraphics ) {
							// at this point, a valid renderpass must be bound

							using namespace le_backend_vk;
							// -- potentially compile and create pipeline here, based on current pass and subpass
							auto requestedPipeline = le_pipeline_manager_i.produce_graphics_pipeline( pipelineManager, le_cmd->info.gpsoHandle, pass, subpassIndex );

							if ( /* DISABLES CODE */ ( false ) ) {

								// Print pipeline debug info when a new pipeline gets bound.

								logger.debug( "Requested pipeline: %x ", le_cmd->info.gpsoHandle );
								debug_print_le_pipeline_layout_info( &requestedPipeline.layout_info );
							}

							if ( !is_equal( currentPipeline, requestedPipeline ) ) {
								// update current pipeline
								currentPipeline = requestedPipeline;
								// -- grab current pipeline layout from cache
								currentPipelineLayout = le_pipeline_manager_i.get_pipeline_layout( pipelineManager, currentPipeline.layout_info.pipeline_layout_key );
								// -- update pipelineData - that's the data values for all descriptors which are currently bound

								argumentState.setCount = uint32_t( currentPipeline.layout_info.set_layout_count );
								argumentState.binding_infos.clear();

								// -- reset dynamic offset count
								argumentState.dynamicOffsetCount = 0;

								// let's create descriptorData vector based on current bindings-
								for ( size_t setId = 0; setId != argumentState.setCount; ++setId ) {

									// look up set layout info via set layout key
									auto const& set_layout_key = currentPipeline.layout_info.set_layout_keys[ setId ];

									auto const setLayoutInfo = le_pipeline_manager_i.get_descriptor_set_layout( pipelineManager, set_layout_key );

									auto& setData = argumentState.setData[ setId ];

									argumentState.layouts[ setId ]         = setLayoutInfo->vk_descriptor_set_layout;
									argumentState.updateTemplates[ setId ] = setLayoutInfo->vk_descriptor_update_template;

									setData.clear();
									setData.reserve( setLayoutInfo->binding_info.size() );

									for ( auto b : setLayoutInfo->binding_info ) {

										// add an entry for each array element with this binding to setData
										for ( size_t arrayIndex = 0; arrayIndex != b.count; arrayIndex++ ) {
											DescriptorData descriptorData{};

											descriptorData.type          = b.type;
											descriptorData.bindingNumber = uint32_t( b.binding );
											descriptorData.arrayIndex    = uint32_t( arrayIndex );

											if ( b.type == le::DescriptorType::eStorageBuffer ||
											     b.type == le::DescriptorType::eUniformBuffer ||
											     b.type == le::DescriptorType::eStorageBufferDynamic ||
											     b.type == le::DescriptorType::eUniformBufferDynamic ) {

												descriptorData.bufferInfo.range = b.range;
											}

											setData.emplace_back( descriptorData );
										}

										if ( b.type == le::DescriptorType::eStorageBufferDynamic ||
										     b.type == le::DescriptorType::eUniformBufferDynamic ) {
											assert( b.count != 0 ); // count cannot be 0

											// store dynamic offset index for this element
											b.dynamic_offset_idx = argumentState.dynamicOffsetCount;

											// increase dynamic offset count by number of elements in this binding
											argumentState.dynamicOffsetCount += b.count;
										}

										// add this binding to list of current bindings
										argumentState.binding_infos.push_back( b );
									}
								}

								vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline.pipeline );
							} else {
								// Re-using previously bound pipeline. We may keep argumentState state as it is.
							}

							// -- Reset dynamic offsets in argumentState:
							// we do this regardless of whether pipeline was already bound,
							// because binding a pipeline should always reset parameters associated
							// with the pipeline.

							memset( argumentState.dynamicOffsets.data(), 0, sizeof( uint32_t ) * argumentState.dynamicOffsetCount );

						} else {
							// -- TODO: warn that graphics pipelines may only be bound within
							// draw passes.
						}
					} break;

					case le::CommandType::eBindComputePipeline: {
						auto* le_cmd = static_cast<le::CommandBindComputePipeline*>( dataIt );
						if ( pass.type == le::QueueFlagBits::eCompute ) {
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
									auto const& set_layout_key = currentPipeline.layout_info.set_layout_keys[ setId ];

									auto const setLayoutInfo = le_pipeline_manager_i.get_descriptor_set_layout( pipelineManager, set_layout_key );

									auto& setData = argumentState.setData[ setId ];

									argumentState.layouts[ setId ]         = setLayoutInfo->vk_descriptor_set_layout;
									argumentState.updateTemplates[ setId ] = setLayoutInfo->vk_descriptor_update_template;

									setData.clear();
									setData.reserve( setLayoutInfo->binding_info.size() );

									for ( auto b : setLayoutInfo->binding_info ) {

										// add an entry for each array element with this binding to setData
										for ( size_t arrayIndex = 0; arrayIndex != b.count; arrayIndex++ ) {
											DescriptorData descriptorData{};

											descriptorData.type          = b.type;
											descriptorData.bindingNumber = uint32_t( b.binding );
											descriptorData.arrayIndex    = uint32_t( arrayIndex );

											descriptorData.bufferInfo.range = b.range;

											setData.emplace_back( std::move( descriptorData ) );
										}

										if ( b.type == le::DescriptorType::eStorageBufferDynamic ||
										     b.type == le::DescriptorType::eUniformBufferDynamic ) {
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
							vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, currentPipeline.pipeline );

						} else {
							// -- TODO: warn that compute pipelines may only be bound within
							// compute passes.
						}

					} break;

					case le::CommandType::eBindRtxPipeline: {
						auto* le_cmd = static_cast<le::CommandBindRtxPipeline*>( dataIt );
						if ( pass.type == le::QueueFlagBits::eCompute ) {
							// at this point, a valid renderpass must be bound

							using namespace le_backend_vk;

							// -- fetch pipeline from pipeline cache, also fetch shader group data, so that
							// we can verify that the current pipeline state matches the pipeline state which
							// was used to create the pipeline. The pipeline state may change if pipeline gets recompiled.

							{
								currentPipeline.pipeline                        = static_cast<VkPipeline>( le_cmd->info.pipeline_native_handle );
								currentPipeline.layout_info.pipeline_layout_key = le_cmd->info.pipeline_layout_key;

								memcpy( currentPipeline.layout_info.set_layout_keys, le_cmd->info.descriptor_set_layout_keys, sizeof( currentPipeline.layout_info.set_layout_keys ) );

								currentPipeline.layout_info.set_layout_count = le_cmd->info.descriptor_set_layout_count;
							}

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
									auto const& set_layout_key = currentPipeline.layout_info.set_layout_keys[ setId ];

									auto const setLayoutInfo = le_pipeline_manager_i.get_descriptor_set_layout( pipelineManager, set_layout_key );

									auto& setData = argumentState.setData[ setId ];

									argumentState.layouts[ setId ]         = setLayoutInfo->vk_descriptor_set_layout;
									argumentState.updateTemplates[ setId ] = setLayoutInfo->vk_descriptor_update_template;

									setData.clear();
									setData.reserve( setLayoutInfo->binding_info.size() );

									for ( auto b : setLayoutInfo->binding_info ) {

										// add an entry for each array element with this binding to setData
										for ( size_t arrayIndex = 0; arrayIndex != b.count; arrayIndex++ ) {
											DescriptorData descriptorData{};

											descriptorData.type          = le::DescriptorType( b.type );
											descriptorData.bindingNumber = uint32_t( b.binding );
											descriptorData.arrayIndex    = uint32_t( arrayIndex );

											if ( b.type == le::DescriptorType::eStorageBuffer ||
											     b.type == le::DescriptorType::eUniformBuffer ||
											     b.type == le::DescriptorType::eStorageBufferDynamic ||
											     b.type == le::DescriptorType::eUniformBufferDynamic ) {

												descriptorData.bufferInfo.range = b.range;
											}

											setData.emplace_back( std::move( descriptorData ) );
										}

										if ( b.type == le::DescriptorType::eStorageBufferDynamic ||
										     b.type == le::DescriptorType::eUniformBufferDynamic ) {
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
							}

							vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, currentPipeline.pipeline );
							// -- "bind" shader binding table state

							{
								rtx_state.sbt_buffer = le_cmd->info.sbt_buffer;

								VkBuffer vk_buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.sbt_buffer );

								VkBufferDeviceAddressInfo info = {
								    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
								    .pNext  = nullptr, // optional
								    .buffer = vk_buffer,
								};
								uint64_t offset               = vkGetBufferDeviceAddress( device, &info );
								rtx_state.ray_gen_sbt_offset  = offset + le_cmd->info.ray_gen_sbt_offset;
								rtx_state.ray_gen_sbt_size    = le_cmd->info.ray_gen_sbt_size;
								rtx_state.miss_sbt_offset     = offset + le_cmd->info.miss_sbt_offset;
								rtx_state.miss_sbt_stride     = le_cmd->info.miss_sbt_stride;
								rtx_state.miss_sbt_size       = le_cmd->info.miss_sbt_size;
								rtx_state.hit_sbt_offset      = offset + le_cmd->info.hit_sbt_offset;
								rtx_state.hit_sbt_stride      = le_cmd->info.hit_sbt_stride;
								rtx_state.hit_sbt_size        = le_cmd->info.hit_sbt_size;
								rtx_state.callable_sbt_offset = offset + le_cmd->info.callable_sbt_offset;
								rtx_state.callable_sbt_stride = le_cmd->info.callable_sbt_stride;
								rtx_state.callable_sbt_size   = le_cmd->info.callable_sbt_size;
								rtx_state.is_set              = true;
							}

						} else {
							// -- TODO: warn that rtx pipelines may only be bound within
							// compute passes.
						}

					} break;
					case le::CommandType::eTraceRays: {
						auto* le_cmd = static_cast<le::CommandTraceRays*>( dataIt );

						// -- update descriptorsets via template if tainted
						bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

						if ( false == argumentsOk ) {
							break;
						}

						// --------| invariant: arguments were updated successfully

						if ( argumentState.setCount > 0 ) {

							vkCmdBindDescriptorSets(
							    cmd,
							    VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
							    currentPipelineLayout,
							    0,
							    argumentState.setCount,
							    descriptorSets,
							    argumentState.dynamicOffsetCount,
							    argumentState.dynamicOffsets.data() );
						}

						assert( rtx_state.is_set && "sbt state must have been set before calling traceRays" );

						// VkBuffer sbt_vk_buffer = frame_data_get_buffer_from_le_resource_id( frame, rtx_state.sbt_buffer );

						//					std::cout << "sbt buffer: " << std::hex << sbt_vk_buffer << std::endl
						//					          << std::flush;
						//					std::cout << "sbt buffer raygen offset: " << std::dec << rtx_state.ray_gen_sbt_offset << std::endl
						//					          << std::flush;

						// buffer, offset, stride, size
						VkStridedDeviceAddressRegionKHR sbt_ray_gen{ rtx_state.ray_gen_sbt_offset, rtx_state.ray_gen_sbt_size, rtx_state.ray_gen_sbt_size };
						VkStridedDeviceAddressRegionKHR sbt_miss{ rtx_state.miss_sbt_offset, rtx_state.miss_sbt_stride, rtx_state.miss_sbt_size };
						VkStridedDeviceAddressRegionKHR sbt_hit{ rtx_state.hit_sbt_offset, rtx_state.hit_sbt_stride, rtx_state.hit_sbt_size };
						VkStridedDeviceAddressRegionKHR sbt_callable{ rtx_state.callable_sbt_offset, rtx_state.callable_sbt_stride, rtx_state.callable_sbt_size };

						vkCmdTraceRaysKHR(
						    cmd,
						    &sbt_ray_gen,
						    &sbt_miss,
						    &sbt_hit,
						    &sbt_callable,
						    le_cmd->info.width,
						    le_cmd->info.height,
						    le_cmd->info.depth //
						);

					} break;
					case le::CommandType::eDispatch: {
						auto* le_cmd = static_cast<le::CommandDispatch*>( dataIt );

						// -- update descriptorsets via template if tainted
						bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

						if ( false == argumentsOk ) {
							break;
						}

						// --------| invariant: arguments were updated successfully

						if ( argumentState.setCount > 0 ) {

							vkCmdBindDescriptorSets( cmd,
							                         VK_PIPELINE_BIND_POINT_COMPUTE,
							                         currentPipelineLayout,
							                         0,
							                         argumentState.setCount,
							                         descriptorSets,
							                         argumentState.dynamicOffsetCount,
							                         argumentState.dynamicOffsets.data() );
						}

						vkCmdDispatch( cmd, le_cmd->info.groupCountX, le_cmd->info.groupCountY, le_cmd->info.groupCountZ );

					} break;
					case le::CommandType::eBufferMemoryBarrier: {
						auto*                  le_cmd = static_cast<le::CommandBufferMemoryBarrier*>( dataIt );
						VkBufferMemoryBarrier2 bufferMemoryBarrier{
						    .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
						    .pNext               = nullptr,
						    .srcStageMask        = static_cast<VkPipelineStageFlags2>( le_cmd->info.srcStageMask ), // happens-before
						    .srcAccessMask       = 0,                                                               // FIXME: no memory is made available from src stage ?!
						    .dstStageMask        = static_cast<VkPipelineStageFlags2>( le_cmd->info.dstStageMask ), // before continuing with dst stage
						    .dstAccessMask       = static_cast<VkAccessFlagBits2>( le_cmd->info.dstAccessMask ),    // and making memory visible to dst stage
						    .srcQueueFamilyIndex = 0,
						    .dstQueueFamilyIndex = 0,
						    .buffer              = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer ),
						    .offset              = le_cmd->info.offset,
						    .size                = le_cmd->info.range,
						};

						VkDependencyInfo dependency_info{
						    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
						    .pNext                    = nullptr, // optional
						    .dependencyFlags          = 0,       // optional
						    .memoryBarrierCount       = 0,       // optional
						    .pMemoryBarriers          = 0,
						    .bufferMemoryBarrierCount = 1, // optional
						    .pBufferMemoryBarriers    = &bufferMemoryBarrier,
						    .imageMemoryBarrierCount  = 0, // optional
						    .pImageMemoryBarriers     = 0,
						};

						vkCmdPipelineBarrier2( cmd, &dependency_info );

					} break;
					case le::CommandType::eDraw: {
						auto* le_cmd = static_cast<le::CommandDraw*>( dataIt );

						// -- update descriptorsets via template if tainted
						bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

						if ( false == argumentsOk ) {
							break;
						}

						// --------| invariant: arguments were updated successfully

						if ( argumentState.setCount > 0 ) {

							vkCmdBindDescriptorSets(
							    cmd,
							    VK_PIPELINE_BIND_POINT_GRAPHICS,
							    currentPipelineLayout,
							    0,
							    argumentState.setCount,
							    descriptorSets,
							    argumentState.dynamicOffsetCount,
							    argumentState.dynamicOffsets.data() );
						}

						vkCmdDraw( cmd, le_cmd->info.vertexCount, le_cmd->info.instanceCount, le_cmd->info.firstVertex, le_cmd->info.firstInstance );
					} break;

					case le::CommandType::eDrawIndexed: {
						auto* le_cmd = static_cast<le::CommandDrawIndexed*>( dataIt );

						// -- update descriptorsets via template if tainted
						bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

						if ( false == argumentsOk ) {
							break;
						}

						// --------| invariant: arguments were updated successfully

						if ( argumentState.setCount > 0 ) {

							vkCmdBindDescriptorSets(
							    cmd,
							    VK_PIPELINE_BIND_POINT_GRAPHICS,
							    currentPipelineLayout,
							    0,
							    argumentState.setCount,
							    descriptorSets,
							    argumentState.dynamicOffsetCount,
							    argumentState.dynamicOffsets.data() );
						}

						vkCmdDrawIndexed(
						    cmd,
						    le_cmd->info.indexCount,
						    le_cmd->info.instanceCount,
						    le_cmd->info.firstIndex,
						    le_cmd->info.vertexOffset,
						    le_cmd->info.firstInstance );
					} break;

					case le::CommandType::eDrawMeshTasks: {
						auto* le_cmd = static_cast<le::CommandDrawMeshTasks*>( dataIt );

						// -- update descriptorsets via template if tainted
						bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

						if ( false == argumentsOk ) {
							break;
						}

						// --------| invariant: arguments were updated successfully

						if ( argumentState.setCount > 0 ) {

							vkCmdBindDescriptorSets( cmd,
							                         VK_PIPELINE_BIND_POINT_GRAPHICS,
							                         currentPipelineLayout,
							                         0,
							                         argumentState.setCount,
							                         descriptorSets,
							                         argumentState.dynamicOffsetCount,
							                         argumentState.dynamicOffsets.data() );
						}

						vkCmdDrawMeshTasksNV( cmd, le_cmd->info.taskCount, le_cmd->info.firstTask );
					} break;

					case le::CommandType::eSetLineWidth: {
						auto* le_cmd = static_cast<le::CommandSetLineWidth*>( dataIt );
						vkCmdSetLineWidth( cmd, le_cmd->info.width );
					} break;

					case le::CommandType::eSetViewport: {
						auto* le_cmd = static_cast<le::CommandSetViewport*>( dataIt );
						// Since data for viewports *is stored inline*, we increment the typed pointer
						// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
						vkCmdSetViewport( cmd, le_cmd->info.firstViewport, le_cmd->info.viewportCount, reinterpret_cast<VkViewport*>( le_cmd + 1 ) );
					} break;

					case le::CommandType::eSetScissor: {
						auto* le_cmd = static_cast<le::CommandSetScissor*>( dataIt );
						// Since data for scissors *is stored inline*, we increment the typed pointer
						// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
						vkCmdSetScissor( cmd, le_cmd->info.firstScissor, le_cmd->info.scissorCount, reinterpret_cast<VkRect2D*>( le_cmd + 1 ) );
					} break;

					case le::CommandType::eSetPushConstantData: {
						if ( currentPipelineLayout ) {
							auto*              le_cmd               = static_cast<le::CommandSetPushConstantData*>( dataIt );
							VkShaderStageFlags active_shader_stages = VkShaderStageFlags( currentPipeline.layout_info.active_vk_shader_stages );
							vkCmdPushConstants( cmd, currentPipelineLayout, active_shader_stages, 0, uint32_t( le_cmd->info.num_bytes ), ( le_cmd + 1 ) ); // Note that we fetch inline data at (le_cmd + 1)
						}
						break;
					}

					case le::CommandType::eBindArgumentBuffer: {
						// we need to store the data for the dynamic binding which was set as an argument to the ubo
						// this alters our internal state
						auto* le_cmd = static_cast<le::CommandBindArgumentBuffer*>( dataIt );

						uint64_t argument_name_id = le_cmd->info.argument_name_id;

						// find binding info with name referenced in command

						auto b = std::find_if( argumentState.binding_infos.begin(), argumentState.binding_infos.end(),
						                       [ &argument_name_id ]( const le_shader_binding_info& e ) -> bool {
							                       return e.name_hash == argument_name_id;
						                       } );

						if ( b == argumentState.binding_infos.end() ) {
							static uint64_t wrong_argument = argument_name_id;
							[]( uint64_t argument ) {
								static uint64_t argument_id_local = 0;
								if ( argument_id_local == wrong_argument )
									return;
								logger.warn( "process_frame: \x1b[38;5;209mInvalid argument name: '%s'\x1b[0m id: %x", le_get_argument_name_from_hash( argument ), argument );
								argument_id_local = argument;
							}( argument_name_id );
							break;
						}

						// ---------| invariant: we found an argument name that matches
						auto setIndex = b->setIndex;
						auto binding  = b->binding;

						auto& bindingData = argumentState.setData[ setIndex ][ binding ].bufferInfo;

						bindingData.buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer_id );
						bindingData.range  = le_cmd->info.range;

						if ( bindingData.range == 0 ) {

							// If no range was specified, we must default to VK_WHOLE_SIZE,
							// as a range setting of 0 is not allowed in Vulkan.

							bindingData.range = VK_WHOLE_SIZE;
						}

						// If binding is in fact a dynamic binding, set the corresponding dynamic offset
						// and set the buffer offset to 0.
						if ( b->type == le::DescriptorType::eStorageBufferDynamic ||
						     b->type == le::DescriptorType::eUniformBufferDynamic ) {
							auto dynamicOffset                            = b->dynamic_offset_idx;
							bindingData.offset                            = 0;
							argumentState.dynamicOffsets[ dynamicOffset ] = uint32_t( le_cmd->info.offset );
						} else {
							bindingData.offset = le_cmd->info.offset;
						}

					} break;

					case le::CommandType::eSetArgumentTexture: {
						auto*    le_cmd           = static_cast<le::CommandSetArgumentTexture*>( dataIt );
						uint64_t argument_name_id = le_cmd->info.argument_name_id;

						// Find binding info with name referenced in command
						auto b = std::find_if( argumentState.binding_infos.begin(), argumentState.binding_infos.end(), [ &argument_name_id ]( const le_shader_binding_info& e ) -> bool {
							return e.name_hash == argument_name_id;
						} );

						if ( b == argumentState.binding_infos.end() ) {
							logger.warn( "Invalid texture argument name id: %x", argument_name_id );
							break;
						}

						// ---------| invariant: we found an argument name that matches

						auto setIndex      = b->setIndex;
						auto bindingNumber = b->binding;
						auto arrayIndex    = uint32_t( le_cmd->info.array_index );

						auto       bindingData      = argumentState.setData[ setIndex ].data();
						auto const binding_data_end = bindingData + argumentState.setData[ setIndex ].size();

						// Descriptors are stored as flat arrays; we cannot assume that binding number matches
						// index of descriptor in set, because some types of uniforms may be arrays, and these
						// arrays will be stored flat in the vector of per-set descriptors.
						//
						// Imagine these were bindings for a set: a b c0 c1 c2 c3 c4 d
						// a(0), b(1), would have their own binding number, but c0(2), c1(2), c2(2), c3(2), c4(2)
						// would share a single binding number, 2, until d(3), which would have binding number 3.
						//
						// To find the correct descriptor, we must therefore iterate over descriptors in-set
						// until we find one that matches the correct array index.
						//
						for ( ; bindingData != binding_data_end; bindingData++ ) {
							if ( bindingData->bindingNumber == bindingNumber &&
							     bindingData->arrayIndex == arrayIndex ) {
								break;
							}
						}

						assert( bindingData != binding_data_end && "could not find specified binding." );

						// fetch texture information based on texture id from command

						auto foundTex = frame.textures_per_pass[ passIndex ].find( le_cmd->info.texture_id );
						if ( foundTex == frame.textures_per_pass[ passIndex ].end() ) {
							using namespace le_renderer;
							logger.error( "Could not find requested texture: '%s', ignoring texture binding command",
							              renderer_i.texture_handle_get_name( le_cmd->info.texture_id ) );
							break;
						}

						// ----------| invariant: texture has been found

						bindingData->imageInfo.imageLayout = le::ImageLayout::eShaderReadOnlyOptimal;
						bindingData->imageInfo.sampler     = foundTex->second.sampler;
						bindingData->imageInfo.imageView   = foundTex->second.imageView;
						bindingData->type                  = le::DescriptorType::eCombinedImageSampler;

					} break;

					case le::CommandType::eSetArgumentImage: {
						auto*    le_cmd           = static_cast<le::CommandSetArgumentImage*>( dataIt );
						uint64_t argument_name_id = le_cmd->info.argument_name_id;

						// Find binding info with name referenced in command
						auto b = std::find_if( argumentState.binding_infos.begin(), argumentState.binding_infos.end(), [ &argument_name_id ]( const le_shader_binding_info& e ) -> bool {
							return e.name_hash == argument_name_id;
						} );

						if ( b == argumentState.binding_infos.end() ) {
							logger.warn( "Warning: Invalid image argument name id: %x", argument_name_id );
							break;
						}

						// ---------| invariant: we found an argument name that matches
						auto setIndex = b->setIndex;
						auto binding  = b->binding;

						auto& bindingData = argumentState.setData[ setIndex ][ binding ];

						// fetch texture information based on texture id from command

						auto foundImgView = frame.imageViews.find( le_cmd->info.image_id );
						if ( foundImgView == frame.imageViews.end() ) {
							logger.error( "Could not find image view for image: '%s', ignoring image binding command.",
							              le_cmd->info.image_id->data->debug_name );
							break;
						}

						// ----------| invariant: image view has been found

						// FIXME: (sync) image layout at this point *must* be general, if we wanted to write to this image.
						bindingData.imageInfo.imageLayout = le::ImageLayout::eGeneral;
						bindingData.imageInfo.imageView   = foundImgView->second;

						bindingData.type       = le::DescriptorType::eStorageImage;
						bindingData.arrayIndex = uint32_t( le_cmd->info.array_index );

					} break;
					case le::CommandType::eSetArgumentTlas: {
						auto*    le_cmd           = static_cast<le::CommandSetArgumentTlas*>( dataIt );
						uint64_t argument_name_id = le_cmd->info.argument_name_id;

						// Find binding info with name referenced in command
						auto b = std::find_if( argumentState.binding_infos.begin(), argumentState.binding_infos.end(), [ &argument_name_id ]( const le_shader_binding_info& e ) -> bool {
							return e.name_hash == argument_name_id;
						} );

						if ( b == argumentState.binding_infos.end() ) {
							logger.warn( "Invalid tlas argument name id: %x", argument_name_id );
							break;
						}

						// ---------| invariant: we found an argument name that matches
						auto setIndex = b->setIndex;
						auto binding  = b->binding;

						auto& bindingData = argumentState.setData[ setIndex ][ binding ];

						auto found_resource = frame.availableResources.find( le_cmd->info.tlas_id );
						if ( found_resource == frame.availableResources.end() ) {
							logger.error( "Could not find acceleration structure: '%s'. Ignoring top level acceleration structure binding command.", le_cmd->info.tlas_id->data->debug_name );
							break;
						}

						// ----------| invariant: acceleration structure has been found

						bindingData.accelerationStructureInfo.accelerationStructure = found_resource->second.as.tlas;
						bindingData.type                                            = le::DescriptorType::eAccelerationStructureKhr;
						bindingData.arrayIndex                                      = uint32_t( le_cmd->info.array_index );

					} break;
					case le::CommandType::eBindIndexBuffer: {
						auto* le_cmd = static_cast<le::CommandBindIndexBuffer*>( dataIt );
						auto  buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer );
						vkCmdBindIndexBuffer( cmd, buffer, le_cmd->info.offset, static_cast<VkIndexType>( le_cmd->info.indexType ) );
					} break;

					case le::CommandType::eBindVertexBuffers: {
						auto* le_cmd = static_cast<le::CommandBindVertexBuffers*>( dataIt );

						uint32_t firstBinding = le_cmd->info.firstBinding;
						uint32_t numBuffers   = le_cmd->info.bindingCount;

						assert( numBuffers && "must at least have one buffer to bind." );

						// Bind vertex buffers by looking up le resources and matching them with their corresponding
						// vk resources.
						// We optimise for the likely case that the same resource is given a number of times:
						// we cache the last lookup of a vk_resource, and if the same le_resource is requested again,
						// we can use the cached value instead of having to do a lookup.

						le_buf_resource_handle le_buffer    = le_cmd->info.pBuffers[ 0 ];
						VkBuffer               vk_buffer    = frame_data_get_buffer_from_le_resource_id( frame, le_buffer );
						vertexInputBindings[ firstBinding ] = vk_buffer;

						for ( uint32_t b = 1; b != numBuffers; ++b ) {
							le_buf_resource_handle next_buffer = le_cmd->info.pBuffers[ b ];
							if ( next_buffer != le_buffer ) {
								le_buffer = next_buffer;
								vk_buffer = frame_data_get_buffer_from_le_resource_id( frame, le_buffer );
							}
							vertexInputBindings[ b + firstBinding ] = vk_buffer;
						}

						vkCmdBindVertexBuffers( cmd, le_cmd->info.firstBinding, le_cmd->info.bindingCount, &vertexInputBindings[ firstBinding ], le_cmd->info.pOffsets );
					} break;

					case le::CommandType::eWriteToBuffer: {

						// Enqueue copy buffer command
						// TODO: we must sync this before the next read.
						auto* le_cmd = static_cast<le::CommandWriteToBuffer*>( dataIt );

						VkBufferCopy region{
						    .srcOffset = le_cmd->info.src_offset,
						    .dstOffset = le_cmd->info.dst_offset,
						    .size      = le_cmd->info.numBytes,
						};

						auto srcBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.src_buffer_id );
						auto dstBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.dst_buffer_id );

						vkCmdCopyBuffer( cmd, srcBuffer, dstBuffer, 1, &region );

						break;
					}

					case le::CommandType::eWriteToImage: {

						auto* le_cmd = static_cast<le::CommandWriteToImage*>( dataIt );

						auto srcBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.src_buffer_id );
						auto dstImage  = frame_data_get_image_from_le_resource_id( frame, le_cmd->info.dst_image_id );

						// We define a range that covers all miplevels. this is useful as it allows us to transform
						// Image layouts in bulk, covering the full mip chain.
						VkImageSubresourceRange rangeAllRemainingMiplevels{
						    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
						    .baseMipLevel   = le_cmd->info.dst_miplevel,
						    .levelCount     = VK_REMAINING_MIP_LEVELS, // we want all miplevels to be in transferDstOptimal.
						    .baseArrayLayer = le_cmd->info.dst_array_layer,
						    .layerCount     = VK_REMAINING_ARRAY_LAYERS, // we want the range to encompass all layers
						};

						{

							// Note: this barrier prepares the buffer resource for transfer read
							VkBufferMemoryBarrier2 bufferTransferBarrier{
							    .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
							    .pNext               = nullptr,                          // optional
							    .srcStageMask        = VK_PIPELINE_STAGE_2_HOST_BIT,     // any host operation
							    .srcAccessMask       = VK_ACCESS_2_HOST_WRITE_BIT,       // make HostWrite memory available (flush host-write)
							    .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT, // must complete before transfer operation
							    .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,    // and it must be visible for transferRead - so that we might read
							    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							    .buffer              = srcBuffer,
							    .offset              = 0, // we assume a fresh buffer was allocated, so offset must be 0
							    .size                = le_cmd->info.numBytes,
							};

							// Note: this barrier is to prepare the image resource for receiving data
							VkImageMemoryBarrier2 imageLayoutToTransferDstOptimal{
							    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
							    .pNext               = nullptr,                             // optional
							    .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, // wait for nothing as no memory must be made available
							    .srcAccessMask       = {},                                  // no memory must be made available - our image is garbage data at first
							    .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,    // layout transiton must complete before transfer operation
							    .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,      // make memory visible to transferWrite - so that we may write
							    .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
							    .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							    .image               = dstImage,
							    .subresourceRange    = rangeAllRemainingMiplevels,
							};
							VkDependencyInfo dependency_info{
							    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
							    .pNext                    = nullptr,
							    .dependencyFlags          = 0,
							    .memoryBarrierCount       = 0,
							    .pMemoryBarriers          = 0,
							    .bufferMemoryBarrierCount = 1,
							    .pBufferMemoryBarriers    = &bufferTransferBarrier,
							    .imageMemoryBarrierCount  = 1,
							    .pImageMemoryBarriers     = &imageLayoutToTransferDstOptimal,
							};

							vkCmdPipelineBarrier2( cmd, &dependency_info );
						}

						{
							// Copy data for first mip level from buffer to image.
							//
							// Then use the first mip level as a source for subsequent mip levels.
							// When copying from a lower mip level to a higher mip level, we must make
							// sure to add barriers, as these blit operations are transfers.
							//

							VkImageSubresourceLayers imageSubresourceLayers{
							    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
							    .mipLevel       = 0,
							    .baseArrayLayer = le_cmd->info.dst_array_layer,
							    .layerCount     = 1,
							};

							VkBufferImageCopy region{
							    .bufferOffset      = 0,                                   // buffer offset is 0, since staging buffer is a fresh, specially allocated buffer
							    .bufferRowLength   = 0,                                   // 0 means tightly packed
							    .bufferImageHeight = 0,                                   // 0 means tightly packed
							    .imageSubresource  = std::move( imageSubresourceLayers ), // stored inline
							    .imageOffset =
							        { .x = le_cmd->info.offset_x,
							          .y = le_cmd->info.offset_y,
							          .z = le_cmd->info.offset_z },
							    .imageExtent =
							        { .width  = le_cmd->info.image_w,
							          .height = le_cmd->info.image_h,
							          .depth  = le_cmd->info.image_d } };
							;

							vkCmdCopyBufferToImage( cmd, srcBuffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
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

							const uint32_t base_miplevel = le_cmd->info.dst_miplevel;
							{
								VkImageMemoryBarrier2 prepareBlit{
								    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
								    .pNext               = nullptr,                              //
								    .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,     //
								    .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,       // make transfer write memory available (flush) to layout transition
								    .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,     //
								    .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,        // make cache (after layout transition) visible to transferRead op
								    .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // layout transition from transfer dst optimal,
								    .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // to shader readonly optimal - note: implicitly makes memory available
								    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
								    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
								    .image               = dstImage,
								    .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, base_miplevel, 1, 0, 1 },
								};

								VkDependencyInfo dependency_info{
								    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
								    .pNext                    = nullptr,
								    .dependencyFlags          = 0,
								    .memoryBarrierCount       = 0,
								    .pMemoryBarriers          = 0,
								    .bufferMemoryBarrierCount = 0,
								    .pBufferMemoryBarriers    = 0,
								    .imageMemoryBarrierCount  = 1,
								    .pImageMemoryBarriers     = &prepareBlit,
								};

								vkCmdPipelineBarrier2( cmd, &dependency_info );
							}
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

								VkOffset3D  offsetZero = { .x = 0, .y = 0, .z = 0 };
								VkOffset3D  offsetSrc  = { .x = srcImgWidth, .y = srcImgHeight, .z = 1 };
								VkOffset3D  offsetDst  = { .x = dstImgWidth, .y = dstImgHeight, .z = 1 };
								VkImageBlit region{
								    .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, srcMipLevel, 0, 1 },
								    .srcOffsets     = { offsetZero, offsetSrc },
								    .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, dstMipLevel, 0, 1 },
								    .dstOffsets     = { offsetZero, offsetDst },
								};

								vkCmdBlitImage(
								    cmd,
								    dstImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
								    dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
								    1, &region, VK_FILTER_LINEAR );

								// Now we barrier Read after Write, and transition our freshly blitted subresource to transferSrc,
								// so that the next iteration may read from it.

								{
									VkImageMemoryBarrier2 finishBlit{
									    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
									    .pNext               = nullptr,                              // optional
									    .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,     // wait on transfer op
									    .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,       // flush transfer writes so that memory becomes available to layout transition
									    .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,     // before next transfer op
									    .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,        // make transitioned image visible to subsequent transfer read ops
									    .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // transition from transfer dst optimal
									    .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // to shader readonly optimal
									    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
									    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
									    .image               = dstImage,
									    .subresourceRange    = {
									           .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
									           .baseMipLevel   = dstMipLevel,
									           .levelCount     = 1,
									           .baseArrayLayer = 0,
									           .layerCount     = 1,
                                        },
									};

									VkDependencyInfo dependency_info{
									    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
									    .pNext                    = nullptr,
									    .dependencyFlags          = 0,
									    .memoryBarrierCount       = 0,
									    .pMemoryBarriers          = 0,
									    .bufferMemoryBarrierCount = 0,
									    .pBufferMemoryBarriers    = 0,
									    .imageMemoryBarrierCount  = 1,
									    .pImageMemoryBarriers     = &finishBlit,
									};

									vkCmdPipelineBarrier2( cmd, &dependency_info );
								}

								// Store this miplevel image's dimensions for next iteration
								srcImgHeight = dstImgHeight;
								srcImgWidth  = dstImgWidth;
							}

						} // end if mipLevelCount > 1

						// Transition image from transfer src optimal to shader read only optimal layout

						{
							VkImageMemoryBarrier2 imageLayoutToShaderReadOptimal{
							    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
							    .pNext               = nullptr,
							    .srcStageMask        = 0,
							    .srcAccessMask       = 0,
							    .dstStageMask        = 0,
							    .dstAccessMask       = 0,
							    .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
							    .newLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
							    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							    .image               = dstImage,
							    .subresourceRange    = rangeAllRemainingMiplevels,
							};
							;

							if ( le_cmd->info.num_miplevels > 1 ) {

								// If there were additional miplevels, the miplevel generation logic ensures that all subresources
								// are left in transfer_src layout.

								imageLayoutToShaderReadOptimal.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;         // anything in transfer must happen-before
								imageLayoutToShaderReadOptimal.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;  // anything that fragment shader does
								imageLayoutToShaderReadOptimal.srcAccessMask = {};                                       // no memory needs to be made available - nothing to flush, as previous barriers ensure flush
								imageLayoutToShaderReadOptimal.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;              // make layout transitioned image visible to shader read in FragmentShader stage
								imageLayoutToShaderReadOptimal.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;     // transition from transfer src optimal
								imageLayoutToShaderReadOptimal.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // ...to shader readonly optimal -and make transitioned image available;
							} else {

								// If there are no additional miplevels, the single subresource will still be in
								// transfer_dst layout after pixel data was uploaded to it.

								imageLayoutToShaderReadOptimal.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;         // anything in transfer must happen-before
								imageLayoutToShaderReadOptimal.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;  // anything in fragment shader
								imageLayoutToShaderReadOptimal.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;           // make available what is in transferwrite - image layout transition will need it
								imageLayoutToShaderReadOptimal.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;              // make visible the result of the image layout transition to shader read in FragmentShader stage
								imageLayoutToShaderReadOptimal.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;     // transition the single one subresource , which is in transfer dst optimal...
								imageLayoutToShaderReadOptimal.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // ... to shader readonly optimal -and make the transitioned image available;
								;
							}

							VkDependencyInfo dependency_info{
							    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
							    .pNext                    = nullptr,
							    .dependencyFlags          = 0,
							    .memoryBarrierCount       = 0,
							    .pMemoryBarriers          = 0,
							    .bufferMemoryBarrierCount = 0,
							    .pBufferMemoryBarriers    = 0,
							    .imageMemoryBarrierCount  = 1,
							    .pImageMemoryBarriers     = &imageLayoutToShaderReadOptimal,
							};

							vkCmdPipelineBarrier2( cmd, &dependency_info ); // images: prepare for shader read
						}

						break;
					}
					case le::CommandType::eBuildRtxBlas: {
						auto* le_cmd = static_cast<le::CommandBuildRtxBlas*>( dataIt );

						size_t     num_blas_handles  = le_cmd->info.blas_handles_count;
						auto const blas_handle_begin = reinterpret_cast<le_resource_handle*>( le_cmd + 1 );

						auto const blas_end = blas_handle_begin + num_blas_handles;

						VkBuffer scratchBuffer = frame_data_get_buffer_from_le_resource_id( frame, LE_RTX_SCRATCH_BUFFER_HANDLE );

						for ( auto blas_handle = blas_handle_begin; blas_handle != blas_end; blas_handle++ ) {

							auto const&                allocated_resource        = frame.availableResources.at( *blas_handle );
							VkAccelerationStructureKHR vk_acceleration_structure = allocated_resource.as.blas;
							auto                       blas_info                 = reinterpret_cast<le_rtx_blas_info_o*>( allocated_resource.info.blasInfo.handle );

							// Translate geometry info from internal format toVkgeometryKHR format.
							// We do this for each blas, which in turn may have an array of geometries.

							std::vector<VkAccelerationStructureGeometryKHR> geometries;
							geometries.reserve( blas_info->geometries.size() );

							std::vector<VkAccelerationStructureBuildRangeInfoKHR> build_ranges;
							build_ranges.reserve( blas_info->geometries.size() );

							for ( auto const& g : blas_info->geometries ) {

								// TODO: we may want to cache this - so that we don't have to lookup addresses more than once

								VkBuffer vertex_buffer = frame_data_get_buffer_from_le_resource_id( frame, g.vertex_buffer );
								VkBuffer index_buffer  = frame_data_get_buffer_from_le_resource_id( frame, g.index_buffer );

								VkDeviceOrHostAddressConstKHR vertex_addr = { .deviceAddress = 0 };
								VkDeviceOrHostAddressConstKHR index_addr  = { .deviceAddress = 0 };

								{
									VkBufferDeviceAddressInfo info = {
									    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
									    .pNext  = nullptr, // optional
									    .buffer = vertex_buffer,
									};

									vertex_addr.deviceAddress = g.vertex_offset + vkGetBufferDeviceAddress( device, &info );
								}

								{
									VkBufferDeviceAddressInfo info = {
									    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
									    .pNext  = nullptr, // optional
									    .buffer = index_buffer,
									};
									index_addr.deviceAddress =
									    g.index_count
									        ? g.index_offset + vkGetBufferDeviceAddress( device, &info )
									        : 0;
								}

								VkAccelerationStructureGeometryKHR geometry = {
								    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
								    .pNext        = nullptr, // optional
								    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
								    .geometry     = {
								            .triangles = {
								                .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
								                .pNext         = nullptr, // optional
								                .vertexFormat  = VkFormat( g.vertex_format ),
								                .vertexData    = vertex_addr,
								                .vertexStride  = g.vertex_stride,
								                .maxVertex     = g.vertex_count - 1, // highest index of a vertex that will be accessed via build command
								                .indexType     = VkIndexType( g.index_type ),
								                .indexData     = index_addr,
								                .transformData = {}, // no transform data
                                        } },
								    .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, // optional
								};

								geometries.emplace_back( geometry );

								VkAccelerationStructureBuildRangeInfoKHR build_range = {
								    .primitiveCount  = 0,
								    .primitiveOffset = 0,
								    .firstVertex     = 0,
								    .transformOffset = 0,
								};

								if ( g.index_count ) {
									// indexed geometry
									build_range.primitiveCount = g.index_count / 3;
								} else {
									// non-indexed geometry
									build_range.primitiveCount = g.vertex_count / 3;
								}

								build_ranges.emplace_back( build_range );
							}

							VkAccelerationStructureBuildRangeInfoKHR const* pBuildRangeInfos = build_ranges.data();

							VkDeviceOrHostAddressKHR scratchDataAddr = {};
							//  We get the device address by querying from the buffer.
							{
								VkBufferDeviceAddressInfo info = {
								    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
								    .pNext  = nullptr, // optional
								    .buffer = scratchBuffer,
								};

								scratchDataAddr.deviceAddress = vkGetBufferDeviceAddress( device, &info );
							}

							VkAccelerationStructureBuildGeometryInfoKHR info = {
							    .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
							    .pNext                    = nullptr, // optional
							    .type                     = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
							    .flags                    = blas_info->flags, // optional
							    .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
							    .srcAccelerationStructure = nullptr,                       // optional
							    .dstAccelerationStructure = vk_acceleration_structure,     // optional
							    .geometryCount            = uint32_t( geometries.size() ), // optional
							    .pGeometries              = geometries.data(),             // optional
							    .ppGeometries             = 0,
							    .scratchData              = scratchDataAddr,
							};

							vkCmdBuildAccelerationStructuresKHR( cmd, 1, &info, &pBuildRangeInfos );

							// Since the scratch buffer is reused across builds, we need a barrier to ensure one build
							// is finished before starting the next one - theoretically we could limit this to the scratch
							// buffer by issueing a buffer memory barrier, but since no one else will probably use the
							// acceleration structure memory caches, we should be fine with this more general barrier.

							{
								VkMemoryBarrier2 barrier = {
								    .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
								    .pNext         = nullptr,                                                  // optional
								    .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, //
								    .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,         // make anything written in previous acceleration structure build stage available
								    .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, // before the next acceleration build stage
								    .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,          // memory which has been previously written (and made available) must be visible after the barrier
								};
								VkDependencyInfo dependency_info = {
								    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
								    .pNext                    = nullptr, // optional
								    .dependencyFlags          = 0,       // optional
								    .memoryBarrierCount       = 1,       // optional
								    .pMemoryBarriers          = &barrier,
								    .bufferMemoryBarrierCount = 0, // optional
								    .pBufferMemoryBarriers    = 0,
								    .imageMemoryBarrierCount  = 0,
								    .pImageMemoryBarriers     = 0,
								};

								vkCmdPipelineBarrier2( cmd, &dependency_info );
							}

						} // end for each blas element in array

						break;
					}
					case le::CommandType::eBuildRtxTlas: {
						auto*                       le_cmd              = static_cast<le::CommandBuildRtxTlas*>( dataIt );
						void*                       payload_addr        = le_cmd + 1;
						le_resource_handle const*   resources           = static_cast<le_resource_handle*>( payload_addr );
						void*                       scratch_memory_addr = le_cmd->info.staging_buffer_mapped_memory;
						le_rtx_geometry_instance_t* instances           = static_cast<le_rtx_geometry_instance_t*>( scratch_memory_addr );

						// Foreach resource, we must patch the corresponding instance

						const size_t instances_count = le_cmd->info.geometry_instances_count;

						// TODO: Error checking: we should skip this command and issue a
						// warning if any blas resource could not be found.

						for ( size_t i = 0; i != instances_count; i++ ) {
							// Update blas handles in-place on GPU mapped, coherent memory.
							//
							// The 64bit integer handles for bottom level acceleration structures were queried from the GPU when
							// building bottom level acceleration structures.
							instances[ i ].blas_handle = frame.availableResources.at( resources[ i ] ).info.blasInfo.device_address;
						}

						// Invariant: all instances should be patched right now, we can use the buffer at offset as
						// instance data to build tlas.
						auto const&                allocated_resource        = frame.availableResources.at( le_cmd->info.tlas_handle );
						VkAccelerationStructureKHR vk_acceleration_structure = allocated_resource.as.tlas;
						auto                       tlas_info                 = reinterpret_cast<le_rtx_tlas_info_o*>( allocated_resource.info.tlasInfo.handle );

						// Issue barrier to make sure that transfer to instances buffer is complete
						// before building top-level acceleration structure

						{
							VkMemoryBarrier2 barrier = {
							    .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
							    .pNext         = nullptr,                                                  //
							    .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,                         // transfer must happen before barrier
							    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,                           // anything written in transfer must have been made available
							    .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, // acceleration structure build must happen-after barrier
							    .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,         // and memory must have been made visible to acceleration structure write
							};
							VkDependencyInfo dependency_info = {
							    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
							    .pNext                    = nullptr, // optional
							    .dependencyFlags          = 0,       // optional
							    .memoryBarrierCount       = 1,       // optional
							    .pMemoryBarriers          = &barrier,
							    .bufferMemoryBarrierCount = 0, // optional
							    .pBufferMemoryBarriers    = 0,
							    .imageMemoryBarrierCount  = 0, // optional
							    .pImageMemoryBarriers     = 0,
							};
							vkCmdPipelineBarrier2( cmd, &dependency_info );
						}

						// instances information is encoded via buffer, but that buffer is also available as host memory,
						// because it is held in staging_buffer_mapped_memory...
						VkBuffer instanceBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.staging_buffer_id );
						VkBuffer scratchBuffer  = frame_data_get_buffer_from_le_resource_id( frame, LE_RTX_SCRATCH_BUFFER_HANDLE );

						VkDeviceOrHostAddressConstKHR instanceBufferDeviceAddress = {};

						{
							VkBufferDeviceAddressInfo info = {
							    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
							    .pNext  = nullptr, // optional
							    .buffer = instanceBuffer,
							};

							instanceBufferDeviceAddress.deviceAddress =
							    le_cmd->info.staging_buffer_offset +
							    vkGetBufferDeviceAddress( device, &info );
						}

						VkAccelerationStructureGeometryKHR khr_instances_data = {
						    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
						    .pNext        = nullptr, // optional
						    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
						    .geometry     = {
						            .instances = {
						                .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
						                .pNext           = nullptr, // optional
						                .arrayOfPointers = false,
						                .data            = instanceBufferDeviceAddress,
                                } },
						    .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, // optional
						};
						// Take pointer to array of khr_instances - we will need one further indirection because reasons.

						//  we get the device address by querying from the buffer.
						VkDeviceOrHostAddressKHR scratch_data_addr = {};
						{
							VkBufferDeviceAddressInfo info = {
							    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
							    .pNext  = nullptr, // optional
							    .buffer = scratchBuffer,
							};
							scratch_data_addr.deviceAddress = vkGetBufferDeviceAddress( device, &info );
						}
						VkAccelerationStructureBuildGeometryInfoKHR info = {
						    .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
						    .pNext                    = nullptr, // optional
						    .type                     = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
						    .flags                    = tlas_info->flags, // optional
						    .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
						    .srcAccelerationStructure = 0,                         // optional
						    .dstAccelerationStructure = vk_acceleration_structure, // optional
						    .geometryCount            = 1,                         // optional
						    .pGeometries              = &khr_instances_data,       // optional
						    .ppGeometries             = 0,
						    .scratchData              = scratch_data_addr,
						};

						VkAccelerationStructureBuildRangeInfoKHR build_ranges = {
						    .primitiveCount  = tlas_info->instances_count, // This is where we set the number of instances.
						    .primitiveOffset = 0,                          // spec states: must be a multiple of 16?!!
						    .firstVertex     = 0,
						    .transformOffset = 0,
						};

						VkAccelerationStructureBuildRangeInfoKHR* p_build_ranges = &build_ranges;

						vkCmdBuildAccelerationStructuresKHR( cmd, 1, &info, &p_build_ranges );

						break;
					}
					default: {
						assert( false && "command not handled" );
					}
					} // end switch header.info.type

					// Move iterator by size of current le_command so that it points
					// to the next command in the list.
					dataIt = static_cast<char*>( dataIt ) + header->info.size;

					++commandIndex;
				}
			}

			// non-draw passes don't need renderpasses.
			if ( pass.type == le::QueueFlagBits::eGraphics && pass.renderPass ) {
				vkCmdEndRenderPass( cmd );
			}

			if ( SHOULD_INSERT_DEBUG_LABELS ) {
				vkCmdEndDebugUtilsLabelEXT( cmd );
			}

			vkEndCommandBuffer( cmd );
		}
	}
}

// ----------------------------------------------------------------------
// This method gets called once per frame (via renderer.update()) in order
// to poll shader modules for updates
static void backend_update_shader_modules( le_backend_o* self ) {
	using namespace le_backend_vk;
	le_pipeline_manager_i.update_shader_modules( self->pipelineCache );
}

// ----------------------------------------------------------------------
static le_shader_module_handle backend_create_shader_module(
    le_backend_o*                     self,
    char const*                       path,
    const LeShaderSourceLanguageEnum& shader_source_language,
    const le::ShaderStageFlagBits&    moduleType,
    char const*                       macro_definitions,
    le_shader_module_handle           handle,
    VkSpecializationMapEntry const*   specialization_map_entries,
    uint32_t                          specialization_map_entries_count,
    void*                             specialization_map_data,
    uint32_t                          specialization_map_data_num_bytes ) {

	using namespace le_backend_vk;
	return le_pipeline_manager_i.create_shader_module(
	    self->pipelineCache,
	    path,
	    shader_source_language,
	    moduleType,
	    macro_definitions,
	    handle,
	    specialization_map_entries,
	    specialization_map_entries_count,
	    specialization_map_data,
	    specialization_map_data_num_bytes );
}

// ----------------------------------------------------------------------

static le_pipeline_manager_o* backend_get_pipeline_cache( le_backend_o* self ) {
	return self->pipelineCache;
}

// ----------------------------------------------------------------------
// Return a pointer to a queue info structure holding the queue
// which we use for default graphics operations. This is also
// the queue which is used for swapchain present.
static BackendQueueInfo* backend_get_default_graphics_queue_info( le_backend_o* self ) {

	assert( !self->queues.empty() && "No queues available; Cannot retrieve default graphics queue" );
	// -----------| Invariant: queue available

	static auto result = self->queues[ self->queue_default_graphics_idx ];

	// It's safe to return a pointer to a vector element, and to cache it here,
	// as `self->queues` is constant once a device has been created.
	// There is no way to add or remove queues at a later stage because queues
	// are owned by device.

	return result;
}

struct QueueSubmissionLoggerData {

	struct info_t {
		uint32_t value;
		uint32_t semaphore_id;
	};

	struct submission_t {
		BackendQueueInfo*   queue;
		std::string         label;
		std::vector<info_t> wait_semaphores;
		std::vector<info_t> signal_semaphores;
	};

	std::vector<submission_t> submissions;
};

std::unordered_map<VkSemaphore, uint32_t>* get_semaphore_indices() {
	static std::unordered_map<VkSemaphore, uint32_t> semaphore_index;
	return &semaphore_index;
}
std::vector<std::string>* get_semaphore_names() {
	static std::vector<std::string> semaphore_names;
	return &semaphore_names;
}
QueueSubmissionLoggerData* get_queue_submission_logger_data() {
	static QueueSubmissionLoggerData data;
	return &data;
}

std::vector<std::string>* backend_initialise_semaphore_names( le_backend_o const* backend ) {

	auto semaphore_names   = get_semaphore_names();
	auto semaphore_indices = get_semaphore_indices();

	uint32_t f_idx = 0;
	for ( auto const& f : backend->mFrames ) {
		for ( size_t i = 0; i != f.swapchain_state.size(); i++ ) {
			{
				auto it = semaphore_indices->emplace( f.swapchain_state[ i ].presentComplete, uint32_t( semaphore_indices->size() ) );
				if ( it.second ) {
					// if an element was inserted
					semaphore_names->push_back( "PRESENT_COMPLETE" );
					assert( semaphore_names->size() == semaphore_indices->size() );
				} else {
					( *semaphore_names )[ it.first->second ] = "PRESENT_COMPLETE";
					assert( semaphore_names->size() == semaphore_indices->size() );
				}
			}
			{
				auto it = semaphore_indices->emplace( f.swapchain_state[ i ].renderComplete, uint32_t( semaphore_indices->size() ) );
				if ( it.second ) {
					// if an element was inserted
					semaphore_names->push_back( "RENDER_COMPLETE" );
					assert( semaphore_names->size() == semaphore_indices->size() );
				} else {
					( *semaphore_names )[ it.first->second ] = "RENDER_COMPLETE";
					assert( semaphore_names->size() == semaphore_indices->size() );
				}
			}
		}
		f_idx++;
	}

	return get_semaphore_names();
};

// ----------------------------------------------------------------------

static void backend_emit_queue_sync_dot_file( le_backend_o const* backend, uint64_t frame_number ) {

	static std::filesystem::path exe_path = []() {
		char result[ 1024 ] = { 0 };

#ifdef _MSC_VER

		// When NULL is passed to GetModuleHandle, the handle of the exe itself is returned
		HMODULE hModule = GetModuleHandle( NULL );
		if ( hModule != NULL ) {
			// Use GetModuleFileName() with module handle to get the path
			GetModuleFileName( hModule, result, ( sizeof( result ) ) );
		}
		size_t count = strnlen_s( result, sizeof( result ) );
#else
		ssize_t count = readlink( "/proc/self/exe", result, 1024 );
#endif

		return std::string( result, ( count > 0 ) ? size_t( count ) : 0 );
	}();

	static QueueSubmissionLoggerData*               data = get_queue_submission_logger_data();
	static std::unordered_map<VkQueue, std::string> queue_name;
	static std::vector<std::string>*                semaphore_names = backend_initialise_semaphore_names( backend );

	std::ostringstream                     os;
	std::unordered_map<uint64_t, uint32_t> wait_info_to_submission; // wait info to submission id
	std::unordered_map<uint64_t, uint32_t> sign_info_to_submission; // signal info to submission id
	uint32_t                               submission_id = 0;

	os << "digraph g {\n"
	      "rankdir = LR;\n"
	      "node [shape = plaintext; margin=0; height = 1; fontname = \"IBM Plex Sans\";];\n"
	      "graph [label = <<table border='0' cellborder='0' cellspacing='0' cellpadding='3'>";
	for ( size_t i = 0; i != backend->queues.size(); i++ ) {

		os << "<tr><td align='left'>queue_" << i << " : {" << to_string_vk_queue_flags( backend->queues[ i ]->queue_flags ) << "}</td></tr>";
	}
	os << "<tr><td align='left'>\"" << exe_path.string() << "\"</td></tr>"
	   << "<tr><td align='left'>Island Queue Sync @ Frame \xe2\x84\x96" << frame_number << "</td></tr>"
	   << "</table>>; splines = true; nodesep = 0.7; fontname = \"IBM Plex Sans\"; fontsize = 10; labeljust = \"l\";];\n";
	;

	// -- Go through each submission in sequence
	for ( auto const& submission : data->submissions ) {
		// each submission represents one queue submission

		os << "struct_" << submission_id << " [\n";

		auto queue_name_it = queue_name.emplace( submission.queue->queue, "" );
		if ( queue_name_it.second == true ) {
			// there was no entry there, yet

			std::ostringstream oname;
			{
				size_t i = 0;
				for ( ; i != backend->queues.size(); i++ ) {
					if ( backend->queues[ i ] == submission.queue ) {
						break;
					}
				}
				if ( i != backend->queues.size() ) {
				}
				oname << "queue_" << i << " : ";
			}

			queue_name_it.first->second = oname.str();
		}

		os << "\tlabel = <\n";
		os << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n\t<tr>";
		os << "<td colspan=\"2\" port=\"port_struct_" << submission_id << "\">" << queue_name_it.first->second << "</td></tr>";
		os << "\n\t<tr><td colspan=\"2\">" << submission.label << "</td></tr>";

		{
			auto w_it = submission.wait_semaphores.begin();
			auto s_it = submission.signal_semaphores.begin();
			do {

				os << "\n\t<tr>";
				if ( w_it != submission.wait_semaphores.end() ) {
					os << "<td port=\"port_" << w_it->semaphore_id << "_w_" << w_it->value << "\">";
					os << "S<sub><font point-size='9'>" << ( *semaphore_names )[ w_it->semaphore_id ] << "</font></sub> \xe2\x8c\x9b";
					if ( ( *semaphore_names )[ w_it->semaphore_id ][ 0 ] <= '9' ) {
						// only show value if we have a non-binary semaphore (that's a semaphore with an alphanumeric name)
						os << w_it->value;
					}
					os << " ";
					wait_info_to_submission.emplace( uint64( w_it->value ) << 32 | w_it->semaphore_id, submission_id );
					w_it++;
				} else {
					os << "<td>";
				}
				os << "</td>";
				if ( s_it != submission.signal_semaphores.end() ) {
					os << "<td port=\"port_" << s_it->semaphore_id << "_s_" << s_it->value << "\">"
					   << "S<sub><font point-size='9'>" << ( *semaphore_names )[ s_it->semaphore_id ] << "</font></sub> \xf0\x9f\x8f\x81 ";
					if ( ( *semaphore_names )[ s_it->semaphore_id ][ 0 ] <= '9' ) {
						// only show value if we have a non-binary semaphore (that's a semaphore with an alphanumeric name)
						os << s_it->value << "";
					}
					sign_info_to_submission.emplace( uint64( s_it->value ) << 32 | s_it->semaphore_id, submission_id );
					s_it++;
				} else {
					os << "<td>";
				}
				os << "</td></tr>";
			} while ( w_it != submission.wait_semaphores.end() || s_it != submission.signal_semaphores.end() );
		}
		os << "\n</table>>];\n";
		submission_id++;
	}

	// for all the waits, find a matching signal
	os << "\n";

	for ( auto const& w : wait_info_to_submission ) {
		auto it = sign_info_to_submission.find( w.first );
		if ( it != sign_info_to_submission.end() ) {

			os << "\tstruct_" << it->second << ":port_" << ( it->first & ~uint32_t( 0 ) ) << "_s_" << ( it->first >> 32 ) << " -> "
			   << "struct_" << w.second << ":port_" << ( w.first & ~uint32_t( 0 ) ) << "_w_" << ( w.first >> 32 ) << "\n";
		}
	}

	// we want to link back to the last submission on the same queue
	{
		os << "edge [style=dashed;];\n";
		std::unordered_map<VkQueue, uint32_t> queue_to_last_submission;
		uint32_t                              i = 0;
		for ( auto const& s : data->submissions ) {

			auto it = queue_to_last_submission.emplace( s.queue->queue, i );

			if ( it.second == false ) {
				// there was already a previous entry for this queue

				os << "\tstruct_" << it.first->second << ":port_struct_" << it.first->second << " -> "
				   << "\tstruct_" << i << ":port_struct_" << i << "\n";

				it.first->second = i;
			}

			i++;
		}
	}

	os << "}\n";

	auto write_to_file = []( char const* filename, std::ostringstream const& os ) {
		FILE* out_file = fopen( filename, "wb" );
		fprintf( out_file, "%s\n", os.str().c_str() );
		fclose( out_file );

		le::Log( LOGGER_LABEL ).info( "Generated .dot file: '%s'", filename );
	};

	char filename[ 32 ] = "";
	snprintf( filename, sizeof( filename ), "queues_%08zu.dot", frame_number );

	std::filesystem::path full_path = exe_path.parent_path() / filename;
	write_to_file( full_path.string().c_str(), os );

	std::error_code ec;

	std::filesystem::path link_path = exe_path.parent_path() / "queues.dot";
	if ( std::filesystem::exists( link_path ) ) {
		std::filesystem::remove( link_path );
	}
	std::filesystem::create_symlink( full_path, link_path, ec );

	auto error_str = ec.message();

	data->submissions.clear();
}

// ----------------------------------------------------------------------
// we wrap queue submissions so that we can log all parameters for a queue submission.
static void backend_queue_submit( BackendQueueInfo* queue, uint32_t submission_count, VkSubmitInfo2 const* submitInfo, VkFence fence, bool should_generate_dot_files, std::string const& debug_info ) {

	if ( should_generate_dot_files ) {

		static QueueSubmissionLoggerData* data = get_queue_submission_logger_data();

		VkSemaphoreSubmitInfo const*       wait_info      = submitInfo->pWaitSemaphoreInfos;
		VkSemaphoreSubmitInfo const* const wait_infos_end = wait_info + submitInfo->waitSemaphoreInfoCount;
		VkSemaphoreSubmitInfo const*       sign_info      = submitInfo->pSignalSemaphoreInfos;
		VkSemaphoreSubmitInfo const* const sign_infos_end = sign_info + submitInfo->signalSemaphoreInfoCount;

		static auto semaphore_index = get_semaphore_indices();
		static auto semaphore_names = get_semaphore_names();

		QueueSubmissionLoggerData::submission_t submission;
		submission.queue = queue;
		do {
			if ( wait_info != wait_infos_end ) {
				auto it = semaphore_index->emplace( wait_info->semaphore, uint32_t( semaphore_index->size() ) );
				if ( it.second ) {
					// if an element was inserted
					semaphore_names->push_back( std::to_string( semaphore_names->size() ) );
					assert( semaphore_names->size() == semaphore_index->size() );
				}
				uint32_t                          wait_semaphore_id = it.first->second;
				QueueSubmissionLoggerData::info_t info;
				info.semaphore_id = wait_semaphore_id;
				info.value        = uint32_t( wait_info->value & ~uint32_t( 0 ) ); // truncate to 32 bit value
				submission.wait_semaphores.emplace_back( std::move( info ) );
				wait_info++;
			}
			if ( sign_info != sign_infos_end ) {
				auto it = semaphore_index->emplace( sign_info->semaphore, uint32_t( semaphore_index->size() ) );
				if ( it.second ) {
					// if an element was inserted
					semaphore_names->push_back( std::to_string( semaphore_names->size() ) );
					assert( semaphore_names->size() == semaphore_index->size() );
				}
				uint32_t                          sign_semaphore_id = it.first->second;
				QueueSubmissionLoggerData::info_t info;
				info.semaphore_id = sign_semaphore_id;
				info.value        = uint32_t( sign_info->value & ~uint32_t( 0 ) ); // truncate to 32bit value
				submission.signal_semaphores.emplace_back( std::move( info ) );
				sign_info++;
			}

		} while ( sign_info != sign_infos_end || wait_info != wait_infos_end );
		submission.label = debug_info;
		data->submissions.emplace_back( std::move( submission ) );
	}

	// --------- the actual queue submission happens here

	vkQueueSubmit2( queue->queue, submission_count, submitInfo, fence );
};

// ----------------------------------------------------------------------
static void backend_submit_queue_transfer_ops( le_backend_o* self, size_t frameIndex, bool should_generate_queue_sync_dot_files ) {

	static auto logger = LeLog( LOGGER_LABEL );
	auto&       frame  = self->mFrames[ frameIndex ];

	struct ownership_transfer_t {
		le_resource_handle    resource;
		uint32_t              src_queue_family_index;
		uint32_t              dst_queue_family_index;
		std::vector<uint32_t> dst_queue_index;
	};

	std::unordered_map<le_resource_handle, ownership_transfer_t> queue_ownership_transfers;     // note that the transfer must happen on both queues - first release, then acquire.
	bool                                                         must_wait_for_acquire = false; /// signals whether multiple queues of the same family await a resoruce to become acquired

	// For all resources test if family ownership matches
	// since we last used this resource - if not, change it, and note the change.
	for ( auto const& submission_data : frame.queue_submission_data ) {
		uint32_t submission_queue_family_idx = self->queues[ submission_data.queue_idx ]->queue_family_index;
		for ( auto const& i : submission_data.pass_indices ) {
			for ( auto const& r : frame.passes[ i ].resources ) {

				// Tentatively write to the front buffer
				auto found_queue_family_ownership = self->resource_queue_family_ownership[ 0 ].find( r );
				// definitely write to the back buffer
				self->resource_queue_family_ownership[ 1 ][ r ] = submission_queue_family_idx;

				// There was already an element in there - we must compare
				if ( found_queue_family_ownership != self->resource_queue_family_ownership[ 0 ].end() &&
				     submission_queue_family_idx != found_queue_family_ownership->second ) {

					ownership_transfer_t change;
					change.src_queue_family_index = uint32_t( found_queue_family_ownership->second );
					change.dst_queue_family_index = uint32_t( submission_queue_family_idx );
					change.resource               = r;
					change.dst_queue_index        = { submission_data.queue_idx }; // We keep track of the actual queue (additionally to its queue family)
					                                                               // that will acquire the resource, as it is this one specifically that
					                                                               // will need to wait for the release semaphore to signal

					auto transfer_inserted_it = queue_ownership_transfers.emplace( change.resource, change );

					if ( transfer_inserted_it.second == false ) {

						// There was already an ownership change for this resource - make sure that it only differs
						// in the dst queue, but not in dst queue family.

						if ( change.dst_queue_family_index == transfer_inserted_it.first->second.dst_queue_family_index ) {

							// Add a queue index to the list of queues which wait for this transfer,
							// the first destination queue will do the acquire, all other destination
							// queues will have to wait for this acquire to complete in an extra step.
							transfer_inserted_it.first->second.dst_queue_index.push_back( submission_data.queue_idx );
							if ( submission_data.queue_idx != transfer_inserted_it.first->second.dst_queue_index[ 0 ] ) {
								// only wait for acquire if it's a different queue than the first queue
								must_wait_for_acquire = true;
							}

						} else {
							logger.error( "resource `%s` cannot be owned by two differing queue families: %d != %d",
							              r->data->debug_name,
							              change.dst_queue_family_index,
							              transfer_inserted_it.first->second.dst_queue_family_index );
							assert( change.dst_queue_family_index == transfer_inserted_it.first->second.dst_queue_family_index && "resource cannot be owned by more than one queue family" );
						}
					}

					if ( LE_PRINT_DEBUG_MESSAGES ) {
						// update entry, so that next frame can compare against the current frame.
						logger.info( "*[%4d]* Resource ownership change: [%s] qf%d -> qf%d (used with queue index:%d)",
						             frameIndex, r->data->debug_name, found_queue_family_ownership->second, submission_queue_family_idx, submission_data.queue_idx );
					}
				}
			}
		}
	}

	// Swap front and back buffer for resource family tracking - we do this because it allows
	// us to update the new state for resource ownership in one atomic operation - by not
	// changing it when we detect a change, we allow ourselves to detect further queues which
	// might need the resource in the changed state.
	//
	std::swap( self->resource_queue_family_ownership[ 0 ], self->resource_queue_family_ownership[ 1 ] );

	if ( queue_ownership_transfers.empty() ) {
		// early-out if there are no queue ownership transfers here.
		return;
	}

	// ---------| invariant: there are queue ownership transfer operations which need to be processed.

	// Record into command buffers -
	// first we release - for which we need to know the queue index of the command buffer.
	//
	// Q: Do we want to release/acquire in bulk - that is per queue family - or do we want to
	//    do it specifically per-queue?
	//
	// A: we want to release in bulk, per-queue family, and then acquire specifically per-
	//    queue. this means that all individual queues must wait for all resources per-queue to
	//    be released. I think that is a good balance.
	//
	//    we also need these acquire semaphores to protect the queues from starting too early,
	//    when their resources are not yet ready.
	//
	// If we had a finer release model, we would use more semaphores, but we would be able to
	// interleave more, potentially, but I feel that semaphores are quite heavy, and that fewer
	// of them is better.
	//

	/* Note that we split out the counting of required command buffers by
	 * release command buffers and acquire command buffers. This is because
	 * we accumulate *release* command buffers by common queue family index
	 * (all release operations for the same queue family index happen in one batch)
	 * and we accumulate *acquire* command buffers by queue index
	 * (all resources that are acquired by the same queue get acquired in the same batch)
	 */

	constexpr uint32_t    MAX_NUM_QUEUE_FAMILY_INDICES = 32;
	uint32_t              cmd_per_family_counter[ MAX_NUM_QUEUE_FAMILY_INDICES ]{}; // zero-initialised
	std::vector<uint32_t> cmd_acquire_per_queue_counter( self->queues.size(), 0 );

	{ /*

		 for each queue family, we need one command buffer, which we use to release all
		 resources that are owned by this queue family.

		 for each queue that acquires resources, we need one command buffer to acquire
		 all resources that this queue acquires.

	 */
		for ( auto const& t : queue_ownership_transfers ) {
			cmd_per_family_counter[ t.second.src_queue_family_index ] |= 1;      // accumulate release command buffer count per family here - if we already have one for this family then we're good.
			cmd_acquire_per_queue_counter[ t.second.dst_queue_index[ 0 ] ] |= 1; // accumulate command buffer count per queue here, we will need a separate one for this transaction
		}

		// For each acquire that requested a specific queue, we must
		// allocate a command buffer with the correct queue family:
		//
		for ( uint32_t i = 0; i != cmd_acquire_per_queue_counter.size(); i++ ) {
			cmd_per_family_counter[ self->queues[ i ]->queue_family_index ] += cmd_acquire_per_queue_counter[ i ];
		}
	}

	BackendFrameData::CommandPool* ownership_transfer_cmd_pools[ MAX_NUM_QUEUE_FAMILY_INDICES ]{};           // zero-initialised
	uint32_t                       ownership_transfer_cmd_bufs_used_count[ MAX_NUM_QUEUE_FAMILY_INDICES ]{}; // zero-initialised. counts used command buffers.

	VkCommandBufferBeginInfo cmd_begin_info = {
	    .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .pNext            = nullptr,                                     // optional
	    .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // optional
	    .pInheritanceInfo = 0,                                           // optional
	};

	for ( uint32_t i = 0; i != MAX_NUM_QUEUE_FAMILY_INDICES; i++ ) {
		if ( cmd_per_family_counter[ i ] != 0 && ownership_transfer_cmd_pools[ i ] == nullptr ) {
			BackendFrameData::CommandPool* pool = backend_frame_data_produce_command_pool( frame, i, self->device->getVkDevice() );
			VkCommandBufferAllocateInfo    info = {
			       .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			       .pNext              = nullptr,
			       .commandPool        = pool->pool,
			       .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			       .commandBufferCount = cmd_per_family_counter[ i ],
            };
			pool->buffers.resize( info.commandBufferCount );
			vkAllocateCommandBuffers( self->device->getVkDevice(), &info, pool->buffers.data() );
			// store the pool into our array of pools, so that we can look it up via the family index.
			ownership_transfer_cmd_pools[ i ] = pool;
			if ( LE_PRINT_DEBUG_MESSAGES ) {
				for ( auto const& b : pool->buffers ) {
					logger.info( "[%3d] allocated command buffer: %p", frame.frameNumber, b );
				}
			}
		}
	}

	struct Barriers {
		std::unordered_map<le_resource_handle, VkImageMemoryBarrier2>  img_barriers;       // we use a map here because there an only be one barrier per-resource
		std::unordered_map<le_resource_handle, VkBufferMemoryBarrier2> buf_barriers;       // --"--
		std::set<uint32_t>                                             src_family_indices; // which release queues this queue must wait for - only used for acquire barriers
		std::set<uint32_t>                                             wait_queue_indices; // which acquire queues this queue must wait for - only used for acquire barriers
	};

	std::unordered_map<uint32_t, Barriers> release_barriers; // map from queue family index to release barrier
	std::unordered_map<uint32_t, Barriers> acquire_barriers; // map from queue to acquire barrier

	// Group all release barriers together by queue family,
	// and group all acquire barriers together by queue
	for ( auto& transfer_it : queue_ownership_transfers ) {
		auto& transfer = transfer_it.second;

		if ( transfer.resource->data->type == LeResourceType::eImage ) {
			auto                  image = frame.availableResources.at( transfer.resource );
			VkImageMemoryBarrier2 imageMemoryBarrier{
			    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			    .pNext               = nullptr,
			    .srcStageMask        = image.state.stage == 0 ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : image.state.stage, // happens-before
			    .srcAccessMask       = ( image.state.visible_access & ANY_WRITE_VK_ACCESS_2_FLAGS ),                     // make available
			    .dstStageMask        = 0,                                                                                // happens-after
			    .dstAccessMask       = 0,                                                                                // make visible
			    .oldLayout           = image.state.layout,
			    .newLayout           = image.state.layout,
			    .srcQueueFamilyIndex = transfer.src_queue_family_index,
			    .dstQueueFamilyIndex = transfer.dst_queue_family_index,
			    .image               = image.as.image,
			    .subresourceRange    = LE_IMAGE_SUBRESOURCE_RANGE_ALL_MIPLEVELS,
			};

			release_barriers[ transfer.src_queue_family_index ].img_barriers.emplace( transfer.resource, imageMemoryBarrier );
			imageMemoryBarrier.srcAccessMask = 0;
			imageMemoryBarrier.srcStageMask  = 0;
			// imageMemoryBarrier.dstAccessMask = 0; // TODO
			// imageMemoryBarrier.dstStageMask = 0; // TODO
			acquire_barriers[ transfer.dst_queue_index[ 0 ] ].img_barriers.emplace( transfer.resource, imageMemoryBarrier );
		} else if ( transfer.resource->data->type == LeResourceType::eBuffer ) {
			auto                   buffer = frame.availableResources.at( transfer.resource );
			VkBufferMemoryBarrier2 bufferMemoryBarrier{
			    .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
			    .pNext               = nullptr,
			    .srcStageMask        = buffer.state.stage == 0 ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : buffer.state.stage, // happens-before
			    .srcAccessMask       = buffer.state.visible_access & ANY_WRITE_VK_ACCESS_2_FLAGS,                          // make available
			    .dstStageMask        = 0,                                                                                  // before continuing with dst stage
			    .dstAccessMask       = 0,                                                                                  // and making memory visible to dst stage
			    .srcQueueFamilyIndex = transfer.src_queue_family_index,
			    .dstQueueFamilyIndex = transfer.dst_queue_family_index,
			    .buffer              = buffer.as.buffer,
			    .offset              = 0,
			    .size                = buffer.info.bufferInfo.size,
			};
			release_barriers[ transfer.src_queue_family_index ].buf_barriers.emplace( transfer.resource, bufferMemoryBarrier );
			bufferMemoryBarrier.srcAccessMask = 0;
			bufferMemoryBarrier.srcStageMask  = 0;
			// bufferMemoryBarrier.dstAccessMask = 0; // TODO
			// bufferMemoryBarrier.dstStageMask = 0; // TODO

			acquire_barriers[ transfer.dst_queue_index[ 0 ] ].buf_barriers.emplace( transfer.resource, bufferMemoryBarrier );
		} else {
			assert( false && "unexpected resource type" );
		}
		// store src family index with acquire barrier - so that these barriers know which timeline semaphores to wait for.
		acquire_barriers[ transfer.dst_queue_index[ 0 ] ].src_family_indices.emplace( transfer.src_queue_family_index );
	}

	// ----------------------------------------------------------------------
	// Record and submit all release actions into command buffers which match queue family
	//
	for ( auto& rb : release_barriers ) {

		std::vector<VkBufferMemoryBarrier2> buffer_barriers;
		buffer_barriers.reserve( rb.second.buf_barriers.size() );
		for ( auto const& b : rb.second.buf_barriers ) {
			buffer_barriers.push_back( b.second );
		}
		std::vector<VkImageMemoryBarrier2> image_barriers;
		image_barriers.reserve( rb.second.img_barriers.size() );
		for ( auto const& b : rb.second.img_barriers ) {
			image_barriers.push_back( b.second );
		}

		VkDependencyInfo dependencyInfo = {
		    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		    .pNext                    = nullptr, // optional
		    .dependencyFlags          = 0,       // optional
		    .memoryBarrierCount       = 0,       // optional
		    .pMemoryBarriers          = 0,
		    .bufferMemoryBarrierCount = uint32_t( buffer_barriers.size() ), // optional
		    .pBufferMemoryBarriers    = buffer_barriers.data(),
		    .imageMemoryBarrierCount  = uint32_t( image_barriers.size() ), // optional
		    .pImageMemoryBarriers     = image_barriers.data(),
		};

		uint32_t queue_family_index = rb.first;
		uint32_t queue_index        = self->default_queue_for_family_index.at( queue_family_index );
		uint32_t buffer_idx         = ownership_transfer_cmd_bufs_used_count[ queue_family_index ]++; // note post-increment
		auto     cmd                = ownership_transfer_cmd_pools[ rb.first ]->buffers[ buffer_idx ];

		vkBeginCommandBuffer( cmd, &cmd_begin_info );
		// record consolidated barrier into release command buffer for this queue family.
		vkCmdPipelineBarrier2( cmd, &dependencyInfo );
		vkEndCommandBuffer( cmd );

		auto& queue = self->queues[ queue_index ];

		VkCommandBufferSubmitInfo cmdSubmitInfo{
		    .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		    .pNext         = nullptr,
		    .commandBuffer = cmd,
		    .deviceMask    = 0, // replaces vkDeviceGroupSubmitInfo
		};

		VkSemaphoreSubmitInfo signal_timeline_semaphore_release_complete = {
		    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		    .pNext       = nullptr,
		    .semaphore   = queue->semaphore,
		    .value       = queue->semaphore_get_next_signal_value(),
		    .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, // signal semaphore once all commands have been processed
		    .deviceIndex = 0,
		};

		{
			VkSubmitInfo2 submitInfo{
			    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			    .pNext                    = nullptr,
			    .flags                    = 0,
			    .waitSemaphoreInfoCount   = 0,
			    .pWaitSemaphoreInfos      = nullptr,
			    .commandBufferInfoCount   = 1,
			    .pCommandBufferInfos      = &cmdSubmitInfo,
			    .signalSemaphoreInfoCount = 1,
			    .pSignalSemaphoreInfos    = &signal_timeline_semaphore_release_complete,
			};

			// submit on the default queue for this queue family
			backend_queue_submit( queue, 1, &submitInfo, nullptr, should_generate_queue_sync_dot_files, "release" );
		}
	}

	// ----------------------------------------------------------------------
	// Record and submit all acquire actions grouped by queue -
	// each acquire submission waits for any release actions that it depends on.

	for ( auto& ab : acquire_barriers ) {
		std::vector<VkBufferMemoryBarrier2> buffer_barriers;
		buffer_barriers.reserve( ab.second.buf_barriers.size() );
		for ( auto const& b : ab.second.buf_barriers ) {
			buffer_barriers.push_back( b.second );
		}
		std::vector<VkImageMemoryBarrier2> image_barriers;
		image_barriers.reserve( ab.second.img_barriers.size() );
		for ( auto const& b : ab.second.img_barriers ) {
			image_barriers.push_back( b.second );
		}

		VkDependencyInfo dependencyInfo = {
		    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		    .pNext                    = nullptr, // optional
		    .dependencyFlags          = 0,       // optional
		    .memoryBarrierCount       = 0,       // optional
		    .pMemoryBarriers          = 0,
		    .bufferMemoryBarrierCount = uint32_t( buffer_barriers.size() ), // optional
		    .pBufferMemoryBarriers    = buffer_barriers.data(),
		    .imageMemoryBarrierCount  = uint32_t( image_barriers.size() ), // optional
		    .pImageMemoryBarriers     = image_barriers.data(),
		};
		;

		uint32_t queue_index      = ab.first;
		uint32_t queue_family_idx = self->queues[ queue_index ]->queue_family_index;
		uint32_t buffer_idx       = ownership_transfer_cmd_bufs_used_count[ queue_family_idx ]++; // note post-increment

		auto cmd = ownership_transfer_cmd_pools[ queue_family_idx ]->buffers[ buffer_idx ];

		vkBeginCommandBuffer( cmd, &cmd_begin_info );
		vkCmdPipelineBarrier2( cmd, &dependencyInfo );
		vkEndCommandBuffer( cmd );

		auto& queue = self->queues[ queue_index ];

		VkCommandBufferSubmitInfo cmdSubmitInfo{
		    .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		    .pNext         = nullptr,
		    .commandBuffer = cmd,
		    .deviceMask    = 0, // replaces vkDeviceGroupSubmitInfo
		};

		// We want to signal a timeline semaphore for each queue submission so that any batch submitted to a queue can be waited upon
		std::vector<VkSemaphoreSubmitInfo> wait_semaphore_timeline_complete;

		for ( auto& wait : ab.second.src_family_indices ) {

			uint32_t    src_queue_family_idx = wait;
			uint32_t    src_queue_idx        = self->default_queue_for_family_index[ src_queue_family_idx ];
			auto const& queue                = self->queues[ src_queue_idx ];

			VkSemaphoreSubmitInfo info{
			    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			    .pNext       = nullptr,
			    .semaphore   = queue->semaphore,
			    .value       = queue->semaphore_wait_value,
			    .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			    .deviceIndex = 0,
			};
			wait_semaphore_timeline_complete.emplace_back( info );
		};

		VkSemaphoreSubmitInfo signal_timeline_semaphore_acquire_complete = {
		    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		    .pNext       = nullptr,
		    .semaphore   = queue->semaphore,
		    .value       = queue->semaphore_wait_value + 1,    // note that we don't immediately update the wait value for this queue itself.
		    .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, // signal semaphore once all commands have been processed
		    .deviceIndex = 0,
		};

		{
			VkSubmitInfo2 submitInfo{
			    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			    .pNext                    = nullptr,
			    .flags                    = 0,
			    .waitSemaphoreInfoCount   = uint32_t( wait_semaphore_timeline_complete.size() ),
			    .pWaitSemaphoreInfos      = wait_semaphore_timeline_complete.data(),
			    .commandBufferInfoCount   = 1,
			    .pCommandBufferInfos      = &cmdSubmitInfo,
			    .signalSemaphoreInfoCount = 1,
			    .pSignalSemaphoreInfos    = &signal_timeline_semaphore_acquire_complete,
			};

			// submit on the queue chosen for this acquire barrier
			backend_queue_submit( queue, 1, &submitInfo, nullptr, should_generate_queue_sync_dot_files, "acquire" );
		}
	}

	// We increase the semaphore wait values for all queues that did have an acquire step,
	// so that acquire operations don't wait for each other - we know that acquire steps
	// don't depend on each other because all that acquires have to wait for is any release.
	// step that they depend on.
	for ( auto& ab : acquire_barriers ) {
		uint32_t          queue_index = ab.first;
		BackendQueueInfo* queue       = self->queues[ queue_index ];
		queue->semaphore_wait_value++;
	}
	// ---------------------------------------------------------------------
	// submit semaphore waits for queues which wait for acquire -
	//
	// If more than one queue of the same family want to acquire the same resource, then all but the first queue
	// must wait for the first queue to complete acquire before they can read from the resource.
	// Otherwise we would end up with a race condition wherein the non-acquiring queue might access
	// the resource before it has even been acquired, or even released.
	//
	// This is not an issue for write resources, as the rendergraph will not allow these to be shared between
	// distinct queues.
	//
	if ( must_wait_for_acquire ) {

		std::unordered_map<uint32_t, std::set<uint32_t>> per_queue_wait_for_queues;

		// First, we group wait operations by queue, so that we only need to issue one sync op per
		// queue.
		for ( auto const& transfer_it : queue_ownership_transfers ) {
			auto const& transfer = transfer_it.second;
			// If any transfer has multiple destination queues, all but the first must wait for the first.
			if ( transfer.dst_queue_index.size() > 1 ) {
				uint32_t wait_for_queue_idx = transfer.dst_queue_index[ 0 ];
				for ( uint32_t i = 1; i != transfer.dst_queue_index.size(); i++ ) {
					// each of the dependent queues must wait for the primary queue
					if ( wait_for_queue_idx != transfer.dst_queue_index[ i ] ) {
						per_queue_wait_for_queues[ transfer.dst_queue_index[ i ] ].emplace( wait_for_queue_idx );
					}
				}
			}
		}

		for ( auto const& per_queue_wait : per_queue_wait_for_queues ) {
			uint32_t                  queue_idx        = per_queue_wait.first;
			std::set<uint32_t> const& waits_for_queues = per_queue_wait.second;

			std::vector<VkSemaphoreSubmitInfo> wait_semaphore_acquire_complete;

			for ( auto& wait_for_queue : waits_for_queues ) {

				auto const& queue = self->queues[ wait_for_queue ];

				VkSemaphoreSubmitInfo info{
				    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				    .pNext       = nullptr,
				    .semaphore   = queue->semaphore,
				    .value       = queue->semaphore_wait_value,
				    .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				    .deviceIndex = 0,
				};
				wait_semaphore_acquire_complete.emplace_back( info );
			};

			VkSubmitInfo2 submitInfo{
			    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			    .pNext                    = nullptr,
			    .flags                    = 0,
			    .waitSemaphoreInfoCount   = uint32_t( wait_semaphore_acquire_complete.size() ),
			    .pWaitSemaphoreInfos      = wait_semaphore_acquire_complete.data(),
			    .commandBufferInfoCount   = 0,
			    .pCommandBufferInfos      = nullptr, // note: we don't submit a command buffer with this submission - it is just there to add synchronisation
			    .signalSemaphoreInfoCount = 0,
			    .pSignalSemaphoreInfos    = nullptr,
			};

			auto const& queue = self->queues[ queue_idx ];
			// submit on the nd queue chosen for this acquire barrier
			backend_queue_submit( queue, 1, &submitInfo, nullptr, should_generate_queue_sync_dot_files, "must_wait_acquire" );
		}
	}
}

// ----------------------------------------------------------------------
static bool backend_dispatch_frame( le_backend_o* self, size_t frameIndex ) {

	static auto logger         = LeLog( LOGGER_LABEL );
	auto&       frame          = self->mFrames[ frameIndex ];
	static auto graphics_queue = self->queues[ self->queue_default_graphics_idx ]->queue; // will not change for the duration of the program.

	if ( self->must_track_resources_queue_family_ownership ) {
		// add queue ownership transfer operations for resources which are shared across queue families.
		backend_submit_queue_transfer_ops( self, frameIndex, frame.must_create_queues_dot_graph );
	}

	std::vector<VkSemaphoreSubmitInfo> wait_present_complete_semaphore_submit_infos;
	std::vector<VkSemaphoreSubmitInfo> render_complete_semaphore_submit_infos;

	wait_present_complete_semaphore_submit_infos.reserve( frame.swapchain_state.size() );
	render_complete_semaphore_submit_infos.reserve( frame.swapchain_state.size() );

	{
		for ( auto const& swp : frame.swapchain_state ) {
			wait_present_complete_semaphore_submit_infos.push_back(
			    {
			        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			        .pNext       = nullptr,
			        .semaphore   = swp.presentComplete,
			        .value       = 0,                                               // ignored, as this semaphore is not a timeline semaphore
			        .stageMask   = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, // signal semaphore once ColorAttachmentOutput has completed
			        .deviceIndex = 0,                                               // replaces vkDeviceGroupSubmitInfo
			    } );
			render_complete_semaphore_submit_infos.push_back(
			    {
			        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			        .pNext       = nullptr,
			        .semaphore   = swp.renderComplete,
			        .value       = 0,                                  // ignored, as this semaphore is not a timeline semaphore
			        .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, // signal semaphore once all commands have been processed
			        .deviceIndex = 0,                                  // replaces vkDeviceGroupSubmitInfo
			    } );
		}

		// On default draw queue, wait for all timeline semaphores before signalling render complete.

		VkSubmitInfo2 submitInfo{
		    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		    .pNext                    = nullptr,
		    .flags                    = 0,
		    .waitSemaphoreInfoCount   = uint32_t( wait_present_complete_semaphore_submit_infos.size() ), //
		    .pWaitSemaphoreInfos      = wait_present_complete_semaphore_submit_infos.data(),             // wait for the present semaphores from earlier frames
		    .commandBufferInfoCount   = 0,                                                               // No commands submitted, this submission is purely for synchronisation
		    .pCommandBufferInfos      = nullptr,
		    .signalSemaphoreInfoCount = 0,
		    .pSignalSemaphoreInfos    = nullptr,

		};

		backend_queue_submit( self->queues[ self->queue_default_graphics_idx ], 1, &submitInfo, nullptr, frame.must_create_queues_dot_graph, "wait_present_complete" );
	}


	for ( auto const& current_submission : frame.queue_submission_data ) {

		// Prepare command buffers for submission
		std::vector<VkCommandBufferSubmitInfo> command_buffer_submit_infos;
		command_buffer_submit_infos.reserve( current_submission.command_pool->buffers.size() ); // one command buffer per pass

		for ( auto const& c : current_submission.command_pool->buffers ) {
			command_buffer_submit_infos.push_back(
			    {
			        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			        .pNext         = nullptr,
			        .commandBuffer = c,
			        .deviceMask    = 0, // replaces vkDeviceGroupSubmitInfo
			    } );
		}

		// We want to signal a timeline semaphore for each queue submission so that any batch submitted to a queue can be waited upon
		VkSemaphoreSubmitInfo signal_semaphore_timeline_complete = {
		    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		    .pNext       = nullptr,
		    .semaphore   = self->queues[ current_submission.queue_idx ]->semaphore,
		    .value       = self->queues[ current_submission.queue_idx ]->semaphore_get_next_signal_value(),
		    .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, // signal semaphore once all commands have been processed
		    .deviceIndex = 0,
		};

		VkSubmitInfo2 submitInfo{
		    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		    .pNext                    = nullptr,
		    .flags                    = 0,
		    .waitSemaphoreInfoCount   = 0,
		    .pWaitSemaphoreInfos      = nullptr,
		    .commandBufferInfoCount   = uint32_t( command_buffer_submit_infos.size() ),
		    .pCommandBufferInfos      = command_buffer_submit_infos.data(),
		    .signalSemaphoreInfoCount = 1,
		    .pSignalSemaphoreInfos    = &signal_semaphore_timeline_complete,
		};

		auto queue = self->queues[ current_submission.queue_idx ];

		backend_queue_submit( queue, 1, &submitInfo, nullptr, frame.must_create_queues_dot_graph, " subgraph { " + current_submission.debug_root_passes_names + " }" );
	}

	{
		/// Now that we have submitted our payloads, we can wait for any timeline semaphores from this frame.
		/// This is so that whatever gets executed on parallel queues will have time to complete until draw has completed.
		///
		/// Timeline Semaphores may be signalled from other (compute, transfer) queues.

		/// We first sumbit draw, then wait for timeline semaphores, so that compute and transfer queue operations can happen
		/// concurrently with the draw queue. The draw queue will then wait for completion.

		/// go through all compute queue submission and wait for the timeline semaphore of the highest submission
		/// go through all transwer queue submissions and wait for the timeline semaphore of the highest submission

		/// If there are no submissions on compute or transfer, just signal that the fence was crossed.
		///
		/// If submitted on the same queue, Queue submission order means that batch 1 needs to complete before batch 2
		/// -- see VkSpec 7.2 (Implicit Synchronization Guarantees)

		std::vector<VkSemaphoreSubmitInfo> timeline_wait_semaphores;

		for ( uint32_t i = 0; i != self->queues.size(); i++ ) {
			timeline_wait_semaphores.push_back( {
			    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			    .pNext       = nullptr,
			    .semaphore   = self->queues[ i ]->semaphore,
			    .value       = self->queues[ i ]->semaphore_wait_value, // wait for highest value this timeline semaphore will signal
			    .stageMask   = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,      // signal semaphore once all commands have been processed
			    .deviceIndex = 0,
			} );
		}

		// On default draw queue, wait for all timeline semaphores before signalling render complete.

		VkSubmitInfo2 submitInfo{
		    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		    .pNext                    = nullptr,
		    .flags                    = 0,
		    .waitSemaphoreInfoCount   = uint32_t( timeline_wait_semaphores.size() ), // This is where we wait for the timeline semaphores from other queue invocations from this frame.
		    .pWaitSemaphoreInfos      = timeline_wait_semaphores.data(),             // Wait for any timeline semaphores from sibling queues here
		    .commandBufferInfoCount   = 0,                                           // No commands submitted, this submission is purely for synchronisation
		    .pCommandBufferInfos      = nullptr,
		    .signalSemaphoreInfoCount = uint32_t( render_complete_semaphore_submit_infos.size() ),
		    .pSignalSemaphoreInfos    = render_complete_semaphore_submit_infos.data(), // signal render complete once this batch has finished processing

		};

		backend_queue_submit( self->queues[ self->queue_default_graphics_idx ], 1, &submitInfo, frame.frameFence, frame.must_create_queues_dot_graph, "graphics_queue_finalize" );
	}

	bool overall_result = true;

	{
		// submit frame for present using the graphics queue
		using namespace le_swapchain_vk;

		for ( size_t i = 0; i != self->swapchains.size(); i++ ) {

			bool result =
			    swapchain_i.present(
			        self->swapchains[ i ],
			        graphics_queue, // we must present on a queue which has present enabled, graphics queue should fit the bill.
			        render_complete_semaphore_submit_infos[ i ].semaphore,
			        &frame.swapchain_state[ i ].image_idx );

			frame.swapchain_state[ i ].present_successful = result;

			overall_result &= result;
		}
	}

	if ( LE_PRINT_DEBUG_MESSAGES ) {
		logger.info( "*** Dispatched frame %d", frame.frameNumber );
	}

	if ( frame.must_create_queues_dot_graph ) {
		backend_emit_queue_sync_dot_file( self, frame.frameNumber );
	}

	return overall_result;
}

// ----------------------------------------------------------------------
// Copy data from array of affinity masks into this frame's affinity masks array
static void backend_set_frame_queue_submission_keys( le_backend_o* self, size_t frameIndex, void const* p_affinity_masks, uint32_t num_affinity_masks, char const** root_names, uint32_t root_names_count ) {
	auto& frame = self->mFrames[ frameIndex ];

	if ( frame.must_create_queues_dot_graph ) {
		// only harvest root passes names if we're going to generate a diagram.
		for ( uint32_t i = 0; i != root_names_count; i++ ) {
			frame.debug_root_passes_names.emplace_back( std::string( std::string( root_names[ i ] ), 0, 255 ) );
		}
	}

	frame.queue_submission_keys.resize( num_affinity_masks );
	memcpy( frame.queue_submission_keys.data(), p_affinity_masks, sizeof( le::RootPassesField ) * num_affinity_masks );
}

// ----------------------------------------------------------------------

static le_rtx_blas_info_handle backend_create_rtx_blas_info( le_backend_o* self, le_rtx_geometry_t const* geometries, uint32_t geometries_count, le::BuildAccelerationStructureFlagsKHR const& flags ) {

	auto* blas_info = new le_rtx_blas_info_o{};

	// Copy geometry information
	blas_info->geometries.insert( blas_info->geometries.end(), geometries, geometries + geometries_count );

	// Store requested flags, but if no build flags requested, at least set the
	// allowUpdate flag so that primitive geometry may be updated.
	blas_info->flags = flags ? static_cast<VkBuildAccelerationStructureFlagsKHR>( flags ) : VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

	// Add to backend's kill list so that all infos associated to handles get cleaned up at the end.
	self->rtx_blas_info_kill_list.add_element( blas_info );

	return reinterpret_cast<le_rtx_blas_info_handle>( blas_info );
};

// ----------------------------------------------------------------------

static le_rtx_tlas_info_handle backend_create_rtx_tlas_info( le_backend_o* self, uint32_t instances_count, le::BuildAccelerationStructureFlagsKHR const& flags ) {

	auto* tlas_info = new le_rtx_tlas_info_o{};

	// Copy geometry information
	tlas_info->instances_count = instances_count;

	// Store requested flags, but if no build flags requested, at least set the
	// allowUpdate flag so that instance information such as transforms may be set.
	tlas_info->flags =
	    flags
	        ? static_cast<VkBuildAccelerationStructureFlagsKHR>( flags )
	        : VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

	// Add to backend's kill list so that all infos associated to handles get cleaned up at the end.
	self->rtx_tlas_info_kill_list.add_element( tlas_info );

	return reinterpret_cast<le_rtx_tlas_info_handle>( tlas_info );
};
// ----------------------------------------------------------------------

extern void register_le_instance_vk_api( void* api );       // for le_instance_vk.cpp
extern void register_le_allocator_linear_api( void* api_ ); // for le_allocator.cpp
extern void register_le_device_vk_api( void* api );         // for le_device_vk.cpp
extern void register_le_pipeline_vk_api( void* api );       // for le_pipeline_vk.cpp

// ----------------------------------------------------------------------
LE_MODULE_REGISTER_IMPL( le_backend_vk, api_ ) {
	auto  api_i        = static_cast<le_backend_vk_api*>( api_ );
	auto& vk_backend_i = api_i->vk_backend_i;

	vk_backend_i.create                          = backend_create;
	vk_backend_i.destroy                         = backend_destroy;
	vk_backend_i.setup                           = backend_setup;
	vk_backend_i.get_data_frames_count           = backend_get_data_frames_count;
	// vk_backend_i.reset_swapchain                 = backend_reset_swapchain;
	vk_backend_i.reset_failed_swapchains         = backend_reset_failed_swapchains;
	vk_backend_i.get_transient_allocators        = backend_get_transient_allocators;
	vk_backend_i.get_staging_allocator           = backend_get_staging_allocator;
	vk_backend_i.poll_frame_fence                = backend_poll_frame_fence;
	vk_backend_i.clear_frame                     = backend_clear_frame;
	vk_backend_i.acquire_physical_resources      = backend_acquire_physical_resources;
	vk_backend_i.process_frame                   = backend_process_frame;
	vk_backend_i.dispatch_frame                  = backend_dispatch_frame;
	vk_backend_i.set_frame_queue_submission_keys = backend_set_frame_queue_submission_keys;

	vk_backend_i.get_pipeline_cache    = backend_get_pipeline_cache;
	vk_backend_i.update_shader_modules = backend_update_shader_modules;
	vk_backend_i.create_shader_module  = backend_create_shader_module;

	vk_backend_i.get_swapchain_resource = backend_get_swapchain_resource;
	vk_backend_i.get_swapchain_extent   = backend_get_swapchain_extent;
	vk_backend_i.get_swapchain_count    = backend_get_swapchain_count;
	vk_backend_i.get_swapchain_info     = backend_get_swapchain_info;

	vk_backend_i.add_swapchain    = backend_add_swapchain;
	vk_backend_i.remove_swapchain = backend_remove_swapchain;

	vk_backend_i.create_rtx_blas_info = backend_create_rtx_blas_info;
	vk_backend_i.create_rtx_tlas_info = backend_create_rtx_tlas_info;

	auto& private_backend_i                           = api_i->private_backend_vk_i;
	private_backend_i.get_vk_device                   = backend_get_vk_device;
	private_backend_i.get_vk_physical_device          = backend_get_vk_physical_device;
	private_backend_i.get_le_device                   = backend_get_le_device;
	private_backend_i.get_instance                    = backend_get_instance;
	private_backend_i.allocate_image                  = backend_allocate_image;
	private_backend_i.destroy_image                   = backend_destroy_image;
	private_backend_i.allocate_buffer                 = backend_allocate_buffer;
	private_backend_i.destroy_buffer                  = backend_destroy_buffer;
	private_backend_i.get_default_graphics_queue_info = backend_get_default_graphics_queue_info;

	auto& staging_allocator_i   = api_i->le_staging_allocator_i;
	staging_allocator_i.create  = staging_allocator_create;
	staging_allocator_i.destroy = staging_allocator_destroy;
	staging_allocator_i.map     = staging_allocator_map;
	staging_allocator_i.reset   = staging_allocator_reset;

	// register/update submodules inside this plugin
	register_le_device_vk_api( api_ );
	register_le_instance_vk_api( api_ );
	register_le_allocator_linear_api( api_ );
	register_le_pipeline_vk_api( api_ );

	auto& le_instance_vk_i = api_i->vk_instance_i;

	//
	auto unique_instance = *le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_backend_instance_o" ) );

	if ( unique_instance ) {
		le_instance_vk_i.post_reload_hook( static_cast<le_backend_vk_instance_o*>( unique_instance ) );
	}

	// implemented in `le_backend_vk_settings.inl`
	auto& backend_settings_i                                        = api_i->le_backend_settings_i;
	backend_settings_i.add_required_device_extension                = le_backend_vk_settings_add_required_device_extension;
	backend_settings_i.add_required_instance_extension              = le_backend_vk_settings_add_required_instance_extension;
	backend_settings_i.add_swapchain_setting                        = le_backend_vk_settings_add_swapchain_setting;
	backend_settings_i.get_requested_physical_device_features_chain = le_backend_vk_get_requested_physical_device_features_chain;
	backend_settings_i.set_concurrency_count                        = le_backend_vk_settings_set_concurrency_count;
	backend_settings_i.get_requested_queue_capabilities             = le_backend_vk_settings_get_requested_queue_capabilities;
	backend_settings_i.set_requested_queue_capabilities             = le_backend_vk_settings_set_requested_queue_capabilities;

	void** p_settings_singleton_addr = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "backend_api_settings_singleton" ) );

	if ( nullptr == *p_settings_singleton_addr ) {
		*p_settings_singleton_addr = le_backend_vk_settings_create();
	}

	// Global settings object for backend - once a backend is initialized, this object is set to readonly.
	api_i->backend_settings_singleton = static_cast<le_backend_vk_settings_o*>( *p_settings_singleton_addr );
}
