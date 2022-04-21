#include "le_core.h"
#include "le_backend_vk.h"
#include "le_log.h"
#include "util/vk_mem_alloc/vk_mem_alloc.h" // for allocation
#include "le_backend_types_internal.h"      // includes vulkan.hpp
#include "le_swapchain_vk.h"
#include "le_window.h"
#include "le_renderer.h"
#include "private/le_renderer_types.h"
#include "private/le_resource_handle_t.inl"
#include "3rdparty/src/spooky/SpookyV2.h" // for hashing renderpass gestalt

#include <vector>
#include <unordered_map>
#include <forward_list>
#include <iostream>
#include <iomanip>
#include <list>
#include <set>
#include <atomic>
#include <mutex>

static constexpr auto LOGGER_LABEL = "le_backend";

#include <memory>

#ifdef _WIN32
#	define __PRETTY_FUNCTION__ __FUNCSIG__
#	include <intrin.h> // for __lzcnt
#endif

#ifndef PRINT_DEBUG_MESSAGES
#	define PRINT_DEBUG_MESSAGES false
#endif

#ifndef DEBUG_TAG_RESOURCES
// Whether to tag resources - requires the debugUtils extension to be present.
#	define DEBUG_TAG_RESOURCES true
#endif

// Helper macro to convert le:: enums to vk:: enums
#define LE_ENUM_TO_VK( enum_name, fun_name )                                    \
	static inline vk::enum_name fun_name( le::enum_name const& rhs ) noexcept { \
		return vk::enum_name( rhs );                                            \
	}

#define LE_C_ENUM_TO_VK( enum_name, fun_name, c_enum_name )                   \
	static inline vk::enum_name fun_name( c_enum_name const& rhs ) noexcept { \
		return vk::enum_name( rhs );                                          \
	}

constexpr size_t LE_FRAME_DATA_POOL_BLOCK_SIZE  = 1u << 24; // 16.77 MB
constexpr size_t LE_FRAME_DATA_POOL_BLOCK_COUNT = 1;
constexpr size_t LE_LINEAR_ALLOCATOR_SIZE       = 1u << 24;

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
			         bufferInfo.sharingMode == rhs.bufferInfo.sharingMode &&
			         bufferInfo.queueFamilyIndexCount == rhs.bufferInfo.queueFamilyIndexCount &&
			         bufferInfo.pQueueFamilyIndices == rhs.bufferInfo.pQueueFamilyIndices // should not be compared this way
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
			         imageInfo.initialLayout == rhs.imageInfo.initialLayout &&
			         imageInfo.queueFamilyIndexCount == rhs.imageInfo.queueFamilyIndexCount &&
			         imageInfo.pQueueFamilyIndices == rhs.imageInfo.pQueueFamilyIndices // should not be compared this way
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
			         bufferInfo.sharingMode == rhs.bufferInfo.sharingMode &&
			         bufferInfo.queueFamilyIndexCount == rhs.bufferInfo.queueFamilyIndexCount &&
			         bufferInfo.pQueueFamilyIndices == rhs.bufferInfo.pQueueFamilyIndices // should not be compared this way
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
			         imageInfo.initialLayout == rhs.imageInfo.initialLayout &&
			         imageInfo.queueFamilyIndexCount == rhs.imageInfo.queueFamilyIndexCount &&
			         ( void* )imageInfo.pQueueFamilyIndices == ( void* )rhs.imageInfo.pQueueFamilyIndices // should not be compared this way
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

	static ResourceCreateInfo from_le_resource_info( const le_resource_info_t& info, uint32_t* pQueueFamilyIndices, uint32_t queueFamilyindexCount );
};

// ----------------------------------------------------------------------

// bottom-level acceleration structure
struct le_rtx_blas_info_o {
	std::vector<le_rtx_geometry_t>         geometries;
	vk::BuildAccelerationStructureFlagsKHR flags;
};

// top-level acceleration structure
struct le_rtx_tlas_info_o {
	uint32_t                               instances_count;
	vk::BuildAccelerationStructureFlagsKHR flags;
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

static inline const vk::ClearValue& le_clear_value_to_vk( const LeClearValue& lhs ) {
	static_assert( sizeof( vk::ClearValue ) == sizeof( LeClearValue ), "Clear value type size must be equal between Le and Vk" );
	return reinterpret_cast<const vk::ClearValue&>( lhs );
}

// ----------------------------------------------------------------------

static inline constexpr le::Format vk_format_to_le( const vk::Format& format ) noexcept {
	return le::Format( format );
}

// ----------------------------------------------------------------------

LE_C_ENUM_TO_VK( BuildAccelerationStructureFlagsKHR, le_build_acceleration_structure_flags_to_vk, LeBuildAccelerationStructureFlags_t );
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
LE_ENUM_TO_VK( IndexType, le_index_type_to_vk );

// ----------------------------------------------------------------------

// Convert a log2 of sample count to the corresponding `vk::SampleCountFlagBits` enum
vk::SampleCountFlagBits le_sample_count_log_2_to_vk( uint32_t sample_count_log2 ) {

	// this method is a quick and dirty hack, but as long as the
	// following static asserts hold true, it will work.

	static_assert( uint32_t( vk::SampleCountFlagBits::e1 ) == 1 << 0, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( vk::SampleCountFlagBits::e2 ) == 1 << 1, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( vk::SampleCountFlagBits::e4 ) == 1 << 2, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( vk::SampleCountFlagBits::e8 ) == 1 << 3, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( vk::SampleCountFlagBits::e16 ) == 1 << 4, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( vk::SampleCountFlagBits::e32 ) == 1 << 5, "SampleCountFlagBits conversion failed." );
	static_assert( uint32_t( vk::SampleCountFlagBits::e64 ) == 1 << 6, "SampleCountFlagBits conversion failed." );

	return vk::SampleCountFlagBits( 1 << sample_count_log2 );
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

ResourceCreateInfo ResourceCreateInfo::from_le_resource_info( const le_resource_info_t& info, uint32_t* pQueueFamilyIndices, uint32_t queueFamilyIndexCount ) {
	ResourceCreateInfo res{};

	res.type = info.type;

	switch ( info.type ) {
	case ( LeResourceType::eBuffer ): {
		res.bufferInfo =
		    static_cast<VkBufferCreateInfo&>(
		        ( vk::BufferCreateInfo()
		              .setFlags( {} )
		              .setSize( info.buffer.size )
		              .setUsage( vk::BufferUsageFlags{ info.buffer.usage } ) // FIXME: we need to call an explicit le -> vk conversion
		              .setSharingMode( vk::SharingMode::eExclusive )
		              .setQueueFamilyIndexCount( queueFamilyIndexCount )
		              .setPQueueFamilyIndices( pQueueFamilyIndices ) ) );

	} break;
	case ( LeResourceType::eImage ): {
		auto const& img = info.image;
		res.imageInfo =
		    static_cast<VkImageCreateInfo&>(
		        vk::ImageCreateInfo()
		            .setFlags( le_image_create_flags_to_vk( img.flags ) )                   //
		            .setImageType( le_image_type_to_vk( img.imageType ) )                   //
		            .setFormat( le_format_to_vk( img.format ) )                             //
		            .setExtent( { img.extent.width, img.extent.height, img.extent.depth } ) //
		            .setMipLevels( img.mipLevels )                                          //
		            .setArrayLayers( img.arrayLayers )                                      //
		            .setSamples( le_sample_count_log_2_to_vk( img.sample_count_log2 ) )     //
		            .setTiling( le_image_tiling_to_vk( img.tiling ) )                       //
		            .setUsage( le_image_usage_flags_to_vk( img.usage ) )                    //
		            .setSharingMode( vk::SharingMode::eExclusive )                          // hardcoded to Exclusive - no sharing between queues
		            .setQueueFamilyIndexCount( queueFamilyIndexCount )                      //
		            .setPQueueFamilyIndices( pQueueFamilyIndices )                          //
		            .setInitialLayout( vk::ImageLayout::eUndefined )                        // must be either pre-initialised, or undefined (most likely)
		    );

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

// ResourceState keeps track of the resource stage *before* a barrier
struct ResourceState {
	vk::AccessFlags        visible_access; // which memory access must be be visible - if any of these are WRITE accesses, these must be made available(flushed) before next access - for the next src access we can OR this with ANY_WRITES
	vk::PipelineStageFlags write_stage;    // current or last stage at which write occurs
	vk::ImageLayout        layout;         // current layout (for images)

	bool operator==( const ResourceState& rhs ) const {
		return visible_access == rhs.visible_access &&
		       write_stage == rhs.write_stage &&
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
	std::vector<vk::Buffer>        buffers;        // 0..n staging buffers used with the current frame (freed on frame clear)
	std::vector<VmaAllocation>     allocations;    // SOA: counterpart to buffers[]
	std::vector<VmaAllocationInfo> allocationInfo; // SOA: counterpart to buffers[]
};

// ------------------------------------------------------------

struct swapchain_state_t {
	vk::Semaphore presentComplete = nullptr;
	vk::Semaphore renderComplete  = nullptr;

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
	vk::Fence       frameFence  = nullptr; // protects the frame - cpu waits on gpu to pass fence before deleting/recycling frame
	vk::CommandPool commandPool = nullptr;

	std::vector<swapchain_state_t> swapchain_state;
	std::vector<vk::CommandBuffer> commandBuffers;

	struct Texture {
		vk::Sampler   sampler;
		vk::ImageView imageView;
	};

	using texture_map_t = std::unordered_map<le_texture_handle, Texture>;

	std::unordered_map<le_img_resource_handle, vk::ImageView> imageViews; // non-owning, references to frame-local textures, cleared on frame fence.

	// With `syncChainTable` and image_attachment_info_o.syncState, we should
	// be able to create renderpasses. Each resource has a sync chain, and each attachment_info
	// has a struct which holds indices into the sync chain telling us where to look
	// up the sync state for a resource at different stages of renderpass construction.
	std::unordered_map<le_resource_handle, std::vector<ResourceState>> syncChainTable;

	static_assert( sizeof( VkBuffer ) == sizeof( VkImageView ) && sizeof( VkBuffer ) == sizeof( VkImage ), "size of AbstractPhysicalResource components must be identical" );

	// Map from renderer resource id to physical resources - only contains resources this frame uses.
	// Q: Does this table actually own the resources?
	// A: It must not: as it is used to map external resources as well.
	std::unordered_map<le_resource_handle, AbstractPhysicalResource> physicalResources;

	/// \brief vk resources retained and destroyed with BackendFrameData
	std::forward_list<AbstractPhysicalResource> ownedResources;

	/// \brief if user provides explicit resource info, we collect this here, so that we can make sure
	/// that any inferred resourceInfo is compatible with what the user selected.
	std::vector<le_resource_handle> declared_resources_id;   // | pre-declared resources (declared via module)
	std::vector<le_resource_info_t> declared_resources_info; // | pre-declared resources (declared via module)

	std::vector<LeRenderPass>  passes;
	std::vector<texture_map_t> textures_per_pass; // non-owning, references to frame-local textures, cleared on frame fence.

	std::vector<vk::DescriptorPool> descriptorPools; // one descriptor pool per pass

	/*

	  Each Frame has one allocation pool from which all allocations for scratch buffers are drawn.

	  When creating encoders, each encoder has their own sub-allocator, each sub-allocator owns an
	  independent block of memory allcated from the frame pool. This way, encoders can work on their
	  own thread.

	 */

	typedef std::unordered_map<le_resource_handle, AllocatedResourceVk> ResourceMap_T;

	ResourceMap_T availableResources; // resources this frame may use
	ResourceMap_T binnedResources;    // resources to delete when this frame comes round to clear()

	VmaPool allocationPool; // pool from which allocations for this frame come from

	std::vector<le_allocator_o*>   allocators;       // owning; typically one per `le_worker_thread`.
	std::vector<vk::Buffer>        allocatorBuffers; // per allocator: one vkBuffer
	std::vector<VmaAllocation>     allocations;      // per allocator: one allocation
	std::vector<VmaAllocationInfo> allocationInfos;  // per allocator: one allocationInfo

	le_staging_allocator_o* stagingAllocator; // owning: allocator for large objects to GPU memory
};

static const vk::BufferUsageFlags LE_BUFFER_USAGE_FLAGS_SCRATCH =
    vk::BufferUsageFlagBits::eIndexBuffer |
    vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eUniformBuffer |
    vk::BufferUsageFlagBits::eStorageBuffer |
#ifdef LE_FEATURE_RTX
    vk::BufferUsageFlagBits::eShaderDeviceAddress |
#endif
    vk::BufferUsageFlagBits::eTransferSrc;

/// \brief backend data object
struct le_backend_o {

	le_backend_vk_instance_o*   instance;
	std::unique_ptr<le::Device> device;

	std::vector<le_swapchain_o*> swapchains; // Owning.

	std::vector<vk::SurfaceKHR> windowSurfaces; // owning. one per window swapchain.

	// Default color formats are inferred during setup() based on
	// swapchain surface (color) and device properties (depth/stencil)
	std::vector<vk::Format>             swapchainImageFormat; ///< default image format used for swapchain (backbuffer image must be in this format)
	std::vector<uint32_t>               swapchainWidth;       ///< swapchain width gathered when setting/resetting swapchain
	std::vector<uint32_t>               swapchainHeight;      ///< swapchain height gathered when setting/resetting swapchain
	std::vector<le_img_resource_handle> swapchain_resources;  ///< resource handle for image associated with each swapchain

	le::Format defaultFormatColorAttachment        = {}; ///< default image format used for color attachments
	le::Format defaultFormatDepthStencilAttachment = {}; ///< default image format used for depth stencil attachments
	le::Format defaultFormatSampledImage           = {}; ///< default image format used for sampled images

	vk::PhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_props{};

	// Siloed per-frame memory
	std::vector<BackendFrameData> mFrames;

	le_pipeline_manager_o* pipelineCache = nullptr;

	VmaAllocator mAllocator = nullptr;

	uint32_t queueFamilyIndexGraphics = 0; // inferred during setup
	uint32_t queueFamilyIndexCompute  = 0; // inferred during setup

	KillList<le_rtx_blas_info_o> rtx_blas_info_kill_list; // used to keep track rtx_blas_infos.
	KillList<le_rtx_tlas_info_o> rtx_tlas_info_kill_list; // used to keep track rtx_blas_infos.

	struct {
		std::unordered_map<le_resource_handle, AllocatedResourceVk> allocatedResources; // Allocated resources, indexed by resource name hash
	} only_backend_allocate_resources_may_access;                                       // Only acquire_physical_resources may read/write
};

// State of arguments for currently bound pipeline - we keep this here,
// so that we can update in bulk before draw, or dispatch command is issued.
//
struct ArgumentState {
	uint32_t                                   dynamicOffsetCount = 0;  // count of dynamic elements in current pipeline
	std::array<uint32_t, 256>                  dynamicOffsets     = {}; // offset for each dynamic element in current pipeline
	uint32_t                                   setCount           = 0;  // current count of bound descriptorSets (max: 8)
	std::array<std::vector<DescriptorData>, 8> setData;                 // data per-set

	std::array<vk::DescriptorUpdateTemplate, 8> updateTemplates; // update templates for currently bound descriptor sets
	std::array<vk::DescriptorSetLayout, 8>      layouts;         // layouts for currently bound descriptor sets
	std::vector<le_shader_binding_info>         binding_infos;
};

struct DescriptorSetState {
	vk::DescriptorSetLayout     setLayout;
	std::vector<DescriptorData> setData;
};

// ----------------------------------------------------------------------

static inline void vk_format_get_is_depth_stencil( vk::Format format_, bool& isDepth, bool& isStencil ) {

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
static inline vk::ImageViewType le_to_vk( le::ImageViewType const& t ) {
	return vk::ImageViewType( t );
}

// ----------------------------------------------------------------------
static inline VkFormat le_to_vk( le::Format const& f ) {
	// this may change - but for now, we can map vk and le formats directly,
	// mostly because codegen guarantees that they stay in sync.
	return VkFormat( f );
}

static inline vk::PipelineStageFlags le_to_vk( LePipelineStageFlags const& f ) {
	return vk::PipelineStageFlags( f );
}

static inline vk::AccessFlags le_to_vk( LeAccessFlags const& f ) {
	return vk::AccessFlags( f );
}

// ----------------------------------------------------------------------

static void backend_create_window_surface( le_backend_o* self, le_swapchain_settings_t* settings ) {

	assert( settings->type == le_swapchain_settings_t::LE_KHR_SWAPCHAIN );
	assert( settings->khr_settings.window );

	using namespace le_window;
	vk::Instance instance             = le_backend_vk::vk_instance_i.get_vk_instance( self->instance );
	settings->khr_settings.vk_surface = window_i.create_surface( settings->khr_settings.window, instance );

	assert( settings->khr_settings.vk_surface );

	self->windowSurfaces.emplace_back( settings->khr_settings.vk_surface );
}

// ----------------------------------------------------------------------

static void backend_destroy_window_surfaces( le_backend_o* self ) {
	static auto logger = LeLog( LOGGER_LABEL );

	for ( auto& surface : self->windowSurfaces ) {
		vk::Instance instance = le_backend_vk::vk_instance_i.get_vk_instance( self->instance );
		instance.destroySurfaceKHR( surface );
		logger.debug( "Surface destroyed" );
	}
	self->windowSurfaces.clear();
}

// ----------------------------------------------------------------------

static le_backend_o* backend_create() {
	auto self = new le_backend_o;
	return self;
}

// ----------------------------------------------------------------------

static void backend_destroy( le_backend_o* self ) {

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

	for ( auto& s : self->swapchains ) {
		using namespace le_swapchain_vk;
		swapchain_i.destroy( s );
	}
	self->swapchains.clear();

	for ( auto& frameData : self->mFrames ) {

		using namespace le_backend_vk;

		// -- destroy per-frame data

		device.destroyFence( frameData.frameFence );

		for ( auto& swapchain_state : frameData.swapchain_state ) {
			device.destroySemaphore( swapchain_state.presentComplete );
			device.destroySemaphore( swapchain_state.renderComplete );
		}
		frameData.swapchain_state.clear();

		device.destroyCommandPool( frameData.commandPool );

		for ( auto& d : frameData.descriptorPools ) {
			device.destroyDescriptorPool( d );
		}

		{
			// Destroy linear allocators, and the buffers allocated for them.
			assert( frameData.allocatorBuffers.size() == frameData.allocators.size() &&
			        frameData.allocatorBuffers.size() == frameData.allocations.size() &&
			        frameData.allocatorBuffers.size() == frameData.allocationInfos.size() );

			vk::Buffer*    buffer     = frameData.allocatorBuffers.data();
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
				device.destroyBuffer( a.second.as.buffer );
			} else {
				device.destroyImage( a.second.as.image );
			}
#ifdef LE_FEATURE_RTX
			if ( a.second.info.isBlas() ) {
				device.destroyBuffer( a.second.info.blasInfo.buffer );
				device.destroyAccelerationStructureKHR( a.second.as.blas );
			}
			if ( a.second.info.isTlas() ) {
				device.destroyBuffer( a.second.info.tlasInfo.buffer );
				device.destroyAccelerationStructureKHR( a.second.as.tlas );
			}
#endif
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
			device.destroyImage( a.second.as.image );
			break;
		case LeResourceType::eBuffer:
			device.destroyBuffer( a.second.as.buffer );
			break;
#ifdef LE_FEATURE_RTX
		case LeResourceType::eRtxBlas:
			device.destroyBuffer( a.second.info.blasInfo.buffer );
			device.destroyAccelerationStructureKHR( a.second.as.blas );
			break;
		case LeResourceType::eRtxTlas:
			device.destroyBuffer( a.second.info.tlasInfo.buffer );
			device.destroyAccelerationStructureKHR( a.second.as.tlas );
			break;
#endif
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

	assert( num_settings && "num_settings must not be zero" );

	for ( size_t i = 0; i != num_settings; i++, settings++ ) {
		le_swapchain_o* swapchain = nullptr;

		switch ( settings->type ) {

		case le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN: {
			// Create an image swapchain
			swapchain = swapchain_i.create( api->swapchain_img_i, self, settings );
		} break;
		case le_swapchain_settings_t::Type::LE_DIRECT_SWAPCHAIN: {
			// Create a windowless swapchain
			swapchain = swapchain_i.create( api->swapchain_direct_i, self, settings );
		} break;
		case le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN: {
			backend_create_window_surface( self, settings );
			swapchain = swapchain_i.create( le_swapchain_vk::api->swapchain_khr_i, self, settings );
		} break;
		}

		assert( swapchain );

		self->swapchainImageFormat.push_back( vk::Format( swapchain_i.get_surface_format( swapchain )->format ) );
		self->swapchainWidth.push_back( swapchain_i.get_image_width( swapchain ) );
		self->swapchainHeight.push_back( swapchain_i.get_image_height( swapchain ) );

		self->swapchains.push_back( swapchain );
	}
}

// ----------------------------------------------------------------------

static size_t backend_get_num_swapchain_images( le_backend_o* self ) {
	assert( !self->swapchains.empty() );
	using namespace le_swapchain_vk;
	return swapchain_i.get_images_count( self->swapchains[ 0 ] );
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
		*count = self->swapchain_resources.size();
		return false;
	}

	// ---------| invariant: count is equal or larger than number of swapchain resources

	size_t num_items = *count = self->swapchain_resources.size();

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
	return self->swapchain_resources.size();
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

static inline uint32_t getMemoryIndexForGraphicsScratchBuffer( VmaAllocator const& allocator, uint32_t queueFamilyGraphics ) {

	// Find memory index for scratch buffer - we do this by pretending to create
	// an allocation.

	vk::BufferCreateInfo bufferInfo{};
	bufferInfo
	    .setFlags( {} )
	    .setSize( 1 )
	    .setUsage( LE_BUFFER_USAGE_FLAGS_SCRATCH )
	    .setSharingMode( vk::SharingMode::eExclusive )
	    .setQueueFamilyIndexCount( 1 )
	    .setPQueueFamilyIndices( &queueFamilyGraphics );

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	uint32_t memIndexScratchBufferGraphics = 0;
	vmaFindMemoryTypeIndexForBufferInfo( allocator, &reinterpret_cast<VkBufferCreateInfo&>( bufferInfo ), &allocInfo, &memIndexScratchBufferGraphics );
	return memIndexScratchBufferGraphics;
}

static inline uint32_t getMemoryIndexForGraphicsStagingBuffer( VmaAllocator const& allocator, uint32_t queueFamilyGraphics ) {

	// Find memory index for staging buffer - we do this by pretending to create
	// an allocation.

	vk::BufferCreateInfo bufferInfo{};
	bufferInfo
	    .setFlags( {} )
	    .setSize( 1 )
	    .setUsage( vk::BufferUsageFlagBits::eTransferSrc )
	    .setSharingMode( vk::SharingMode::eExclusive )
	    .setQueueFamilyIndexCount( 1 )
	    .setPQueueFamilyIndices( &queueFamilyGraphics );

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	uint32_t memIndexStagingBufferGraphics = 0;
	vmaFindMemoryTypeIndexForBufferInfo( allocator, &reinterpret_cast<VkBufferCreateInfo&>( bufferInfo ), &allocInfo, &memIndexStagingBufferGraphics );
	return memIndexStagingBufferGraphics;
}

// ----------------------------------------------------------------------

typedef void ( *pfn_get_required_vk_extensions )( const le_swapchain_settings_t* settings, char const*** exts, size_t* num_exts );

// ----------------------------------------------------------------------

static void collect_requested_swapchain_extensions(
    le_swapchain_settings_t*       swapchain_settings,
    uint32_t                       swapchain_settings_count,
    pfn_get_required_vk_extensions get_extensions_func,
    std::vector<char const*>&      requested_extensions ) {

	auto const swapchain_settings_end = swapchain_settings + swapchain_settings_count;

	for ( auto settings = swapchain_settings; settings != swapchain_settings_end; settings++ ) {

		char const** exts;
		size_t       num_exts;
		get_extensions_func( settings, &exts, &num_exts );

		if ( num_exts ) {
			requested_extensions.insert( requested_extensions.end(), exts, exts + num_exts );
		}
	}
}

// ----------------------------------------------------------------------

static std::vector<char const*> collect_requested_instance_extensions( le_backend_vk_settings_t const* settings ) {
	std::vector<char const*> requestedInstanceExtensions;

	// -- insert extensions necessary for glfw window

	uint32_t extensionCount           = 0;
	auto     requiredWindowExtensions = le::Window::getRequiredVkExtensions( &extensionCount );

	requestedInstanceExtensions.insert( requestedInstanceExtensions.end(),
	                                    requiredWindowExtensions,
	                                    requiredWindowExtensions + extensionCount );

	// -- insert any instance extensions requested for swapchains
	collect_requested_swapchain_extensions(
	    settings->pSwapchain_settings, settings->num_swapchain_settings,
	    le_swapchain_vk::swapchain_i.get_required_vk_instance_extensions, requestedInstanceExtensions );

	return requestedInstanceExtensions;
}

// ----------------------------------------------------------------------

static std::vector<char const*> collect_requested_device_extensions( le_backend_vk_settings_t const* settings ) {
	std::vector<char const*> requestedDeviceExtensions;

	// -- insert device extensions requested via renderer.settings

	if ( settings->requestedDeviceExtensions && settings->numRequestedDeviceExtensions ) {
		requestedDeviceExtensions.insert(
		    requestedDeviceExtensions.end(),
		    settings->requestedDeviceExtensions,
		    settings->requestedDeviceExtensions + settings->numRequestedDeviceExtensions );
	}

	// -- insert any device extensions requested via the swapchain.

	collect_requested_swapchain_extensions(
	    settings->pSwapchain_settings, settings->num_swapchain_settings,
	    le_swapchain_vk::swapchain_i.get_required_vk_device_extensions, requestedDeviceExtensions );

	// -- insert any additionally requested extensions

	requestedDeviceExtensions.insert(
	    requestedDeviceExtensions.end(),
	    settings->requestedDeviceExtensions,
	    settings->requestedDeviceExtensions + settings->numRequestedDeviceExtensions );

	return requestedDeviceExtensions;
}

// ----------------------------------------------------------------------

static void backend_initialise( le_backend_o* self, std::vector<char const*> requested_instance_extensions, std::vector<char const*> requested_device_extensions ) {
	using namespace le_backend_vk;
	self->instance      = vk_instance_i.create( requested_instance_extensions.data(), uint32_t( requested_instance_extensions.size() ) );
	self->device        = std::make_unique<le::Device>( self->instance, requested_device_extensions.data(), uint32_t( requested_device_extensions.size() ) );
	self->pipelineCache = le_pipeline_manager_i.create( *self->device );
}
// ----------------------------------------------------------------------

static void backend_create_main_allocator( VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VmaAllocator* allocator ) {
	VmaAllocatorCreateInfo createInfo{};
	createInfo.flags = {
#ifdef LE_FEATURE_RTX
	    VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
#endif
	};

	createInfo.device                      = device;
	createInfo.frameInUseCount             = 0;
	createInfo.physicalDevice              = physical_device;
	createInfo.preferredLargeHeapBlockSize = 0; // set to default, currently 256 MB
	createInfo.instance                    = instance;

	vmaCreateAllocator( &createInfo, allocator );
}

// ----------------------------------------------------------------------

static void backend_setup( le_backend_o* self, le_backend_vk_settings_t const* settings ) {

	using namespace le_backend_vk;

	assert( settings );
	if ( settings == nullptr ) {
		std::cerr << "FATAL: Must specify settings for backend." << std::endl
		          << std::flush;
		exit( 1 );
	}

	// -- initialise backend

	backend_initialise( self, collect_requested_instance_extensions( settings ), collect_requested_device_extensions( settings ) );

	vk::Device         vkDevice         = self->device->getVkDevice();
	vk::PhysicalDevice vkPhysicalDevice = self->device->getVkPhysicalDevice();
	vk::Instance       vkInstance       = vk_instance_i.get_vk_instance( self->instance );

	// -- query rtx properties, and store them with backend
	self->device->getRaytracingProperties( &static_cast<VkPhysicalDeviceRayTracingPipelinePropertiesKHR&>( self->ray_tracing_props ) );

	// -- Create allocator for backend vulkan memory
	// we do this here, because swapchain might want to already use the allocator.

	backend_create_main_allocator( vkInstance, vkPhysicalDevice, vkDevice, &self->mAllocator );

	// -- create swapchain if requested

	backend_create_swapchains( self, settings->num_swapchain_settings, settings->pSwapchain_settings );

	// -- setup backend memory objects

	auto frameCount = backend_get_num_swapchain_images( self );

	self->mFrames.reserve( frameCount );

	self->queueFamilyIndexGraphics = self->device->getDefaultGraphicsQueueFamilyIndex();
	self->queueFamilyIndexCompute  = self->device->getDefaultComputeQueueFamilyIndex();

	uint32_t memIndexScratchBufferGraphics = getMemoryIndexForGraphicsScratchBuffer( self->mAllocator, self->queueFamilyIndexGraphics ); // used for transient command buffer allocations
	uint32_t memIndexStagingBufferGraphics = getMemoryIndexForGraphicsStagingBuffer( self->mAllocator, self->queueFamilyIndexGraphics ); // used to stage transfers to persistent memory

	assert( vkDevice ); // device must come from somewhere! It must have been introduced to backend before, or backend must create device used by everyone else...

	{
		self->swapchain_resources.reserve( self->swapchains.size() );
		char swapchain_name[ 64 ];

		for ( uint32_t j = 0; j != self->swapchains.size(); j++ ) {
			snprintf( swapchain_name, sizeof( swapchain_name ), "Le_Swapchain_Image_Handle[%d]", j );
			self->swapchain_resources.emplace_back(
			    le_renderer::renderer_i.produce_img_resource_handle(
			        swapchain_name, 0, nullptr, le_img_resource_usage_flags_t::eIsRoot ) );
		}

		assert( !self->swapchain_resources.empty() && "swapchain_resources must not be empty" );
	}

	for ( size_t i = 0; i != frameCount; ++i ) {

		// -- Set up per-frame resources

		BackendFrameData frameData{};

		frameData.swapchain_state.resize( self->swapchains.size() );

		{
			for ( auto& state : frameData.swapchain_state ) {
				state.presentComplete = vkDevice.createSemaphore( {} );
				state.renderComplete  = vkDevice.createSemaphore( {} );
			}
		}

		frameData.frameFence  = vkDevice.createFence( {} ); // fence starts out as "signalled"
		frameData.commandPool = vkDevice.createCommandPool( { vk::CommandPoolCreateFlagBits::eTransient, self->device->getDefaultGraphicsQueueFamilyIndex() } );

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
		// We want to make sure to have at least one allocator.
		size_t num_allocators = std::max<size_t>( 1, settings->concurrency_count );

		for ( size_t i = 0; i != frameCount; ++i ) {
			// -- create linear allocators for each frame
			backend_create_transient_allocators( self, i, num_allocators );
		}
	}

	{
		// Set default image formats

		using namespace le_backend_vk;

		assert( !self->swapchainImageFormat.empty() && "must have at least one swapchain image format available." );

		self->defaultFormatColorAttachment        = vk_format_to_le( self->swapchainImageFormat[ 0 ] );
		self->defaultFormatDepthStencilAttachment = vk_format_to_le( vk_device_i.get_default_depth_stencil_format( *self->device ) );

		// We hard-code default format for sampled images, since this is the most likely
		// format we will encounter bitmaps to be encoded in, and there is no good way
		// to infer it.
		self->defaultFormatSampledImage = le::Format::eR8G8B8A8Unorm;
	}
}

// ----------------------------------------------------------------------
// Add image attachments to leRenderPass
// Update syncchain for images affected.
static void le_renderpass_add_attachments( le_renderpass_o const* pass, LeRenderPass& currentPass, BackendFrameData& frame, le::SampleCountFlagBits const& sampleCount ) {

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
			    img_resource->data->debug_name, numSamplesLog2, img_resource, 0 );
		}

		auto& syncChain = frame.syncChainTable[ img_resource ];

		assert( !syncChain.empty() && "SyncChain must not be empty" );

		vk::Format attachmentFormat = vk::Format( frame.availableResources[ img_resource ].info.imageInfo.format );

		bool isDepth = false, isStencil = false;
		vk_format_get_is_depth_stencil( attachmentFormat, isDepth, isStencil );
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
		currentAttachment->format     = attachmentFormat;
		currentAttachment->numSamples = le_sample_count_flag_bits_to_vk( sampleCount );
		currentAttachment->loadOp     = le_attachment_load_op_to_vk( image_attachment_info.loadOp );
		currentAttachment->storeOp    = le_attachment_store_op_to_vk( image_attachment_info.storeOp );
		currentAttachment->clearValue = le_clear_value_to_vk( image_attachment_info.clearValue );

		{
			// track resource state before entering a subpass

			auto& previousSyncState = syncChain.back();
			auto  beforeFirstUse{ previousSyncState };

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

			auto& previousSyncState = syncChain.back();
			auto  beforeSubpass{ previousSyncState };

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

		vk::Format attachmentFormat = vk::Format( frame.availableResources[ img_resource ].info.imageInfo.format );

		bool isDepth = false, isStencil = false;
		vk_format_get_is_depth_stencil( attachmentFormat, isDepth, isStencil );
		bool isDepthStencil = isDepth || isStencil;

		AttachmentInfo* currentAttachment = currentPass.attachments +
		                                    currentPass.numColorAttachments +
		                                    currentPass.numDepthStencilAttachments +
		                                    currentPass.numResolveAttachments;

		// we're dealing with a resolve attachment here.
		currentPass.numResolveAttachments++;

		currentAttachment->resource   = img_resource;
		currentAttachment->format     = attachmentFormat;
		currentAttachment->numSamples = vk::SampleCountFlagBits::e1; // this is a requirement for resolve passes.
		currentAttachment->loadOp     = vk::AttachmentLoadOp::eDontCare;
		currentAttachment->storeOp    = le_attachment_store_op_to_vk( image_attachment_info.storeOp );
		currentAttachment->clearValue = le_clear_value_to_vk( image_attachment_info.clearValue );
		currentAttachment->type       = AttachmentInfo::Type::eResolveAttachment;

		{
			// track resource state before entering a subpass

			auto& previousSyncState = syncChain.back();
			auto  beforeFirstUse{ previousSyncState };

			currentAttachment->initialStateOffset = uint16_t( syncChain.size() );
			syncChain.emplace_back( std::move( beforeFirstUse ) ); // attachment initial state for a renderpass - may be loaded/cleared on first use
			                                                       // * sync state: ready for load/store *
		}

		{
			// track resource state before subpass

			auto& previousSyncState = syncChain.back();
			auto  beforeSubpass{ previousSyncState };

			{
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

		// TODO: here, go through command instructions for renderpass and update resource chain if necessary.
		// If resource is modified by commands inside the renderpass, this needs to be added to the sync chain here.

		// Whichever next resource state will be in the sync chain will be the resource state we should transition to
		// when defining the last_subpass_to_external dependency
		// which is why, optimistically, we designate the index of the next, not yet written state here -
		currentAttachment->finalStateOffset = uint16_t( syncChain.size() );

	} // end foreach image attachment
}

// ----------------------------------------------------------------------
// Updates sync chain for resourcess referenced in rendergraph
// each renderpass contains offsets into sync chain for given resource used by renderpass.
// resource sync state for images used as renderpass attachments is chosen so that they
// can be implicitly synced using subpass dependencies.
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
			backbufferState.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput; // we need this, since semaphore waits on this stage
			backbufferState.visible_access = vk::AccessFlagBits( 0 );                           // semaphore took care of availability - we can assume memory is already available
		} else {
			logger.warn( "No reference to backbuffer found in renderpass" );
		}
	}

	using namespace le_renderer;

	auto get_stage_flags_based_on_renderpass_type = []( LeRenderPassType const& rp_type ) -> vk::PipelineStageFlags {
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

		currentPass.width       = renderpass_i.get_width( *pass );
		currentPass.height      = renderpass_i.get_height( *pass );
		currentPass.sampleCount = le_sample_count_flag_bits_to_vk( renderpass_i.get_sample_count( *pass ) );

		// Find explicit sync ops needed for resources which are not image
		// attachments.
		//
		{
			le_resource_handle const*   resources       = nullptr;
			LeResourceUsageFlags const* resources_usage = nullptr;
			size_t                      resources_count = 0;
			renderpass_i.get_used_resources( *pass, &resources, &resources_usage, &resources_count );

			for ( size_t i = 0; i != resources_count; ++i ) {
				auto const& resource = resources[ i ];
				auto const& usage    = resources_usage[ i ];

				auto& syncChain = syncChainTable[ resource ];
				assert( !syncChain.empty() ); // must not be empty - this resource must exist, and have an initial sync state

				LeRenderPass::ExplicitSyncOp syncOp{};

				syncOp.resource                  = resource;
				syncOp.active                    = true;
				syncOp.sync_chain_offset_initial = uint32_t( syncChain.size() - 1 );

				ResourceState requestedState{}; // State we want our image to be in when pass begins.

				// Define synchronisation requirements for each resource based on resource type,
				// and resource usage.
				//
				if ( usage.type == LeResourceType::eImage ) {

					if ( usage.as.image_usage_flags & LE_IMAGE_USAGE_SAMPLED_BIT ) {

						requestedState.visible_access = vk::AccessFlagBits::eShaderRead;
						requestedState.write_stage    = get_stage_flags_based_on_renderpass_type( currentPass.type );
						requestedState.layout         = vk::ImageLayout::eShaderReadOnlyOptimal;

					} else if ( usage.as.image_usage_flags & LE_IMAGE_USAGE_STORAGE_BIT ) {

						requestedState.visible_access = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
						requestedState.write_stage    = get_stage_flags_based_on_renderpass_type( currentPass.type );
						requestedState.layout         = vk::ImageLayout::eGeneral;

					} else if ( usage.as.image_usage_flags & LE_IMAGE_USAGE_TRANSFER_DST_BIT ) {
						// this is an image write operation.
						requestedState.visible_access = vk::AccessFlagBits::eShaderRead;
						requestedState.layout         = vk::ImageLayout::eShaderReadOnlyOptimal;
						requestedState.write_stage    = vk::PipelineStageFlagBits::eVertexShader;

						//						continue;

						// TODO: implement - and make sure we're still compatible with the barriers inserted
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

		// Iterate over all image attachments
		auto const& sampleCount = renderpass_i.get_sample_count( *pass );
		le_renderpass_add_attachments( *pass, currentPass, frame, sampleCount );

		// Note that we "steal" the encoder from the renderer pass -
		// it becomes now our (the backend's) job to destroy it.
		currentPass.encoder = renderpass_i.steal_encoder( *pass );

		frame.passes.emplace_back( std::move( currentPass ) );
	} // end for all passes

	for ( auto& syncChainPair : syncChainTable ) {
		const auto& id        = syncChainPair.first;
		auto&       syncChain = syncChainPair.second;

		auto finalState{ syncChain.back() };

		if ( std::find( backbufferImageHandles.begin(), backbufferImageHandles.end(), id ) != backbufferImageHandles.end() ) {
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
	auto&      frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	// Non-blocking, polling
	// auto result = device.getFenceStatus( {frame.frameFence} );

	// NOTE: this may block.
	auto result = device.waitForFences( { frame.frameFence }, true, 1000'000'000 );

	if ( result != vk::Result::eSuccess ) {
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

	auto&      frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	//	auto result = device.waitForFences( {frame.frameFence}, true, 100'000'000 );

	//	if ( result != vk::Result::eSuccess ) {
	//		return false;
	//	}

	// -------- Invariant: fence has been crossed, all resources protected by fence
	//          can now be claimed back.

	device.resetFences( { frame.frameFence } );

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

	for ( auto& d : frame.descriptorPools ) {
		device.resetDescriptorPool( d );
	}

	{ // clear resources owned exclusively by this frame

		for ( auto& r : frame.ownedResources ) {
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
				logger.warn( "%s: abstract physical resource has unknown type (%p) and cannot be deleted. leaking...", __PRETTY_FUNCTION__, r.type );
				break;
			}
		}
		frame.ownedResources.clear();
	}

	device.freeCommandBuffers( frame.commandPool, frame.commandBuffers );
	frame.commandBuffers.clear();

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

	device.resetCommandPool( frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources );

	return true;
};

// ----------------------------------------------------------------------

static void backend_create_renderpasses( BackendFrameData& frame, vk::Device& device ) {

	static auto logger = LeLog( LOGGER_LABEL );
	// NOTE: we might be able to simplify this along the lines of
	// <https://github.com/Tobski/simple_vulkan_synchronization>
	// <https://github.com/gwihlidal/vk-sync-rs>

	// create renderpasses
	const auto& syncChainTable = frame.syncChainTable;

	// we use this to mask out any reads in srcAccess, as it never makes sense to flush reads
	const auto ANY_WRITE_ACCESS_FLAGS = ( vk::AccessFlagBits::eColorAttachmentWrite |
	                                      vk::AccessFlagBits::eDepthStencilAttachmentWrite |
	                                      vk::AccessFlagBits::eAccelerationStructureWriteKHR |
	                                      vk::AccessFlagBits::eHostWrite |
	                                      vk::AccessFlagBits::eMemoryWrite |
	                                      vk::AccessFlagBits::eShaderWrite |
	                                      vk::AccessFlagBits::eTransferWrite |
	                                      vk::AccessFlagBits::eAccelerationStructureWriteKHR |
	                                      vk::AccessFlagBits::eAccelerationStructureWriteKHR |
	                                      vk::AccessFlagBits::eCommandPreprocessWriteNV |
	                                      vk::AccessFlagBits::eTransformFeedbackCounterWriteEXT );

	// for each attachment, we want to keep track of its last used sync state
	// so that we may know whether to issue a barrier or not.

	for ( auto& pass : frame.passes ) {

		// The rest of this loop only concerns draw passes
		//
		if ( pass.type != LE_RENDER_PASS_TYPE_DRAW ) {
			continue;
		}

		// ---------| Invariant: current pass is a draw pass.

		std::vector<vk::AttachmentDescription> attachments;
		attachments.reserve( pass.numColorAttachments + pass.numDepthStencilAttachments );

		std::vector<vk::AttachmentReference>     colorAttachmentReferences;
		std::vector<vk::AttachmentReference>     resolveAttachmentReferences;
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
			logger.info( "* Renderpass: '%s'", pass.debugName.c_str() );
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
				logger.info( " %30s (s: %4d) : %30s : %30s : %30s | sync chain indices: %4d : %4d : %4d",
				             attachment->resource->data->debug_name, attachment->resource->data->num_samples,
				             vk::to_string( syncInitial.layout ).c_str(),
				             vk::to_string( syncSubpass.layout ).c_str(),
				             vk::to_string( syncFinal.layout ).c_str(),
				             attachment->initialStateOffset,
				             attachment->initialStateOffset + 1,
				             attachment->finalStateOffset );
			}

			attachments.emplace_back( attachmentDescription );

			switch ( attachment->type ) {
			case AttachmentInfo::Type::eDepthStencilAttachment:
				dsAttachmentReference = std::make_unique<vk::AttachmentReference>( attachments.size() - 1, syncSubpass.layout );
				break;
			case AttachmentInfo::Type::eColorAttachment:
				colorAttachmentReferences.emplace_back( attachments.size() - 1, syncSubpass.layout );
				break;
			case AttachmentInfo::Type::eResolveAttachment:
				resolveAttachmentReferences.emplace_back( attachments.size() - 1, syncSubpass.layout );
				break;
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
		    .setPResolveAttachments( resolveAttachmentReferences.empty() ? nullptr : resolveAttachmentReferences.data() ) // must be NULL or have same length as colorAttachments
		    .setPDepthStencilAttachment( dsAttachmentReference.get() )
		    .setPreserveAttachmentCount( 0 )
		    .setPPreserveAttachments( nullptr );

		subpasses.emplace_back( subpassDescription );

		std::vector<vk::SubpassDependency> dependencies;
		dependencies.reserve( 2 );
		{
			if ( PRINT_DEBUG_MESSAGES && false ) {

				logger.info( "Subpass Dependency: VK_SUBPASS_EXTERNAL to subpass [0]" );
				logger.info( "\t srcStage: %s", vk::to_string( srcStageFromExternalFlags ).c_str() );
				logger.info( "\t dstStage: %s", vk::to_string( dstStageFromExternalFlags ).c_str() );
				logger.info( "\tsrcAccess: %s", vk::to_string( srcAccessFromExternalFlags ).c_str() );
				logger.info( "\tdstAccess: %s", vk::to_string( dstAccessFromExternalFlags ).c_str() );

				logger.info( "Subpass Dependency: subpass [0] to VK_SUBPASS_EXTERNAL:" );
				logger.info( "\t srcStage: %s", vk::to_string( srcStageToExternalFlags ).c_str() );
				logger.info( "\t dstStage: %s", vk::to_string( dstStageToExternalFlags ).c_str() );
				logger.info( "\tsrcAccess: %s", vk::to_string( srcAccessToExternalFlags ).c_str() );
				logger.info( "\tdstAccess: %s", vk::to_string( dstAccessToExternalFlags ).c_str() );
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
				for ( const auto& a : attachments ) {
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
				for ( const auto& s : subpasses ) {

					// Note: Attachment references are not that straightforward to hash either, as they contain a layout
					// field, which we want to ignore, since it makes no difference for render pass compatibility.

					rp_hash = SpookyHash::Hash64( &s.flags, sizeof( s.flags ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.pipelineBindPoint, sizeof( s.pipelineBindPoint ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.inputAttachmentCount, sizeof( s.inputAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.colorAttachmentCount, sizeof( s.colorAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.preserveAttachmentCount, sizeof( s.preserveAttachmentCount ), rp_hash );

					// We define this as a pure function lambda, and hope for it to be inlined
					auto calc_hash_for_attachment_references = []( vk::AttachmentReference const* pAttachmentRefs, unsigned int count, uint64_t seed ) -> uint64_t {
						if ( pAttachmentRefs == nullptr ) {
							return seed;
						}
						// ----------| invariant: pAttachmentRefs is valid
						for ( auto const* pAr = pAttachmentRefs; pAr != pAttachmentRefs + count; pAr++ ) {
							seed = SpookyHash::Hash64( pAr, sizeof( vk::AttachmentReference::attachment ), seed );
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
static inline vk::Buffer frame_data_get_buffer_from_le_resource_id( const BackendFrameData& frame, const le_buf_resource_handle& buffer ) {

	if ( buffer->data->flags == uint8_t( le_buf_resource_usage_flags_t::eIsVirtual ) ) {
		return frame.allocatorBuffers[ buffer->data->index ];
	} else if ( buffer->data->flags == uint8_t( le_buf_resource_usage_flags_t::eIsStaging ) ) {
		return frame.stagingAllocator->buffers[ buffer->data->index ];
	} else {
		return frame.availableResources.at( buffer ).as.buffer;
	}
}

// ----------------------------------------------------------------------
static inline vk::Image frame_data_get_image_from_le_resource_id( const BackendFrameData& frame, const le_img_resource_handle& img ) {
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
		return le_to_vk( texInfo.imageView.format );
	}
}

// ----------------------------------------------------------------------

vk::ImageAspectFlags get_aspect_flags_from_format( vk::Format const& format ) {
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
static void backend_create_frame_buffers( BackendFrameData& frame, vk::Device& device ) {

	for ( auto& pass : frame.passes ) {

		if ( pass.type != LE_RENDER_PASS_TYPE_DRAW ) {
			continue;
		}

		uint32_t attachmentCount = pass.numColorAttachments +
		                           pass.numResolveAttachments +
		                           pass.numDepthStencilAttachments;

		std::vector<vk::ImageView> framebufferAttachments;
		framebufferAttachments.reserve( attachmentCount );

		auto const attachment_end = pass.attachments + attachmentCount;
		for ( AttachmentInfo const* attachment = pass.attachments; attachment != attachment_end; attachment++ ) {

			vk::ImageSubresourceRange subresourceRange;
			subresourceRange
			    .setAspectMask( get_aspect_flags_from_format( attachment->format ) )
			    .setBaseMipLevel( 0 )
			    .setLevelCount( 1 )
			    .setBaseArrayLayer( 0 )
			    .setLayerCount( 1 );

			vk::ImageViewCreateInfo imageViewCreateInfo;
			imageViewCreateInfo
			    .setImage( frame_data_get_image_from_le_resource_id( frame, attachment->resource ) )
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
		    .setAttachmentCount( attachmentCount )
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

// ----------------------------------------------------------------------

static void backend_create_descriptor_pools( BackendFrameData& frame, vk::Device& device, size_t numRenderPasses ) {

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

		std::vector<vk::DescriptorPoolSize> descriptorPoolSizes;

		descriptorPoolSizes.reserve( DESCRIPTOR_TYPE_COUNT );

		for ( size_t i = 0; i != DESCRIPTOR_TYPE_COUNT; ++i ) {
			descriptorPoolSizes.emplace_back( vk::DescriptorType( DESCRIPTOR_TYPES[ i ] ), 1000 ); // 1000 descriptors of each type
		}

		::vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
		descriptorPoolCreateInfo
		    .setMaxSets( 2000 )
		    .setPoolSizeCount( uint32_t( descriptorPoolSizes.size() ) )
		    .setPPoolSizes( descriptorPoolSizes.data() );

		vk::DescriptorPool descriptorPool = device.createDescriptorPool( descriptorPoolCreateInfo );

		frame.descriptorPools.emplace_back( std::move( descriptorPool ) );
	}
}

// ----------------------------------------------------------------------
// Returns a VkFormat which will match a given set of LeImageUsageFlags.
// If a matching format cannot be inferred, this method
le::Format infer_image_format_from_le_image_usage_flags( le_backend_o* self, LeImageUsageFlags flags ) {
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
static inline AllocatedResourceVk allocate_resource_vk( const VmaAllocator& alloc, const ResourceCreateInfo& resourceInfo, VkDevice vk_device = nullptr ) {
	static auto         logger = LeLog( LOGGER_LABEL );
	AllocatedResourceVk res{};
	res.info = resourceInfo;
	VmaAllocationCreateInfo allocationCreateInfo{};
	allocationCreateInfo.flags          = {}; // default flags
	allocationCreateInfo.usage          = VMA_MEMORY_USAGE_GPU_ONLY;
	allocationCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkResult result = VK_SUCCESS;

	if ( resourceInfo.isBuffer() ) {

		result = vmaCreateBuffer( alloc,
		                          &resourceInfo.bufferInfo,
		                          &allocationCreateInfo,
		                          &res.as.buffer,
		                          &res.allocation,
		                          &res.allocationInfo );
		assert( result == VK_SUCCESS );

	} else if ( resourceInfo.isImage() ) {

		result = vmaCreateImage( alloc,
		                         &resourceInfo.imageInfo,
		                         &allocationCreateInfo,
		                         &res.as.image,
		                         &res.allocation,
		                         &res.allocationInfo );
		assert( result == VK_SUCCESS );
	} else if ( resourceInfo.isBlas() ) {

#ifdef LE_FEATURE_RTX

		// Allocate bottom level ray tracing acceleration structure

		assert( vk_device && "blas allocation needs device" );
		vk::Device device( vk_device );

		auto const blas = reinterpret_cast<le_rtx_blas_info_o*>( resourceInfo.blasInfo.handle );

		std::vector<vk::AccelerationStructureGeometryKHR> geometries;
		std::vector<uint32_t>                             primitive_counts;

		geometries.reserve( blas->geometries.size() );
		primitive_counts.reserve( geometries.size() );

		for ( auto const& g : blas->geometries ) {

			vk::AccelerationStructureGeometryTrianglesDataKHR t_data{};
			t_data
			    .setMaxVertex( g.vertex_count - 1 ) // highest index of a vertex that will be accessed by build command
			    .setIndexType( le_index_type_to_vk( g.index_type ) )
			    .setVertexFormat( le_format_to_vk( g.vertex_format ) );

			vk::AccelerationStructureGeometryDataKHR g_data{};
			g_data
			    .setInstances( 1 )
			    .setTriangles( t_data );

			vk::AccelerationStructureGeometryKHR geom{};
			geom
			    .setFlags( vk::GeometryFlagBitsKHR::eOpaque )
			    .setGeometryType( vk::GeometryTypeKHR::eTriangles )
			    .setGeometry( g_data );

			geometries.push_back( geom );

			primitive_counts.push_back( g.index_count ? g.index_count / 3 : g.vertex_count / 3 );
		}

		vk::AccelerationStructureBuildGeometryInfoKHR build_info{};

		build_info
		    .setType( vk::AccelerationStructureTypeKHR::eBottomLevel )
		    .setGeometries( geometries );
		vk::AccelerationStructureBuildSizesInfoKHR build_sizes =
		    device.getAccelerationStructureBuildSizesKHR(
		        vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_counts );

		vk::BufferCreateInfo bufferInfo{};
		bufferInfo
		    .setFlags( vk::BufferCreateFlags{} )
		    .setSize( build_sizes.accelerationStructureSize )
		    .setUsage( vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR )
		    .setSharingMode( vk::SharingMode::eExclusive )
		    .setQueueFamilyIndexCount( 0 )
		    .setPQueueFamilyIndices( 0 );

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

		vk::AccelerationStructureCreateInfoKHR create_info{};
		create_info
		    .setCreateFlags( vk::AccelerationStructureCreateFlagsKHR() )
		    .setBuffer( res.info.blasInfo.buffer )
		    .setOffset( 0 ) // must be a multiple of 256 : vkSpec
		    .setSize( build_sizes.accelerationStructureSize )
		    .setType( vk::AccelerationStructureTypeKHR::eBottomLevel )
		    .setDeviceAddress( 0 ) // if 0, this means no specific device address requested
		    ;

		res.as.blas = device.createAccelerationStructureKHR( create_info );

		logger.info( "Created acceleration structure '%p' with size: %d, scratch size: %d", res.as.blas, build_sizes.accelerationStructureSize, build_sizes.buildScratchSize );

		res.info.blasInfo.buffer_size = build_sizes.accelerationStructureSize;

		// Store memory requirements for scratch buffer into allocation info for this blas element
		res.info.blasInfo.scratch_buffer_size = build_sizes.buildScratchSize;

		// Query, and store object integer handle, which is used to refer
		// to this bottom-level acceleration structure from a top-level
		// acceleration structure
		vk::AccelerationStructureDeviceAddressInfoKHR device_address_info{};
		device_address_info.setAccelerationStructure( res.as.blas );

		res.info.blasInfo.device_address = device.getAccelerationStructureAddressKHR( device_address_info );
#else
		assert( false && "backend compiled without RTX features, but RTX feature requested." );
#endif
	} else if ( resourceInfo.isTlas() ) {

#ifdef LE_FEATURE_RTX

		// Allocate top level ray tracing allocation structure

		assert( vk_device && "tlas allocation needs device" );
		vk::Device device( vk_device );

		auto const tlas = reinterpret_cast<le_rtx_tlas_info_o*>( resourceInfo.tlasInfo.handle );

		assert( tlas && "tlas must be valid." );

		vk::AccelerationStructureBuildGeometryInfoKHR build_info{};

		vk::AccelerationStructureGeometryInstancesDataKHR instances_data{};

		vk::AccelerationStructureGeometryDataKHR geometry_data{};
		geometry_data.setInstances( instances_data );

		vk::AccelerationStructureGeometryKHR geom{};
		geom
		    .setGeometryType( vk::GeometryTypeKHR::eInstances )
		    .setGeometry( geometry_data );

		build_info
		    .setType( vk::AccelerationStructureTypeKHR::eTopLevel )
		    .setMode( vk::BuildAccelerationStructureModeKHR::eBuild )
		    .setGeometries( geom );

		vk::AccelerationStructureBuildSizesInfoKHR build_sizes =
		    device.getAccelerationStructureBuildSizesKHR(
		        vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, tlas->instances_count );

		vk::BufferCreateInfo bufferInfo{};
		bufferInfo
		    .setFlags( vk::BufferCreateFlags{} )
		    .setSize( build_sizes.accelerationStructureSize )
		    .setUsage( vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR )
		    .setSharingMode( vk::SharingMode::eExclusive )
		    .setQueueFamilyIndexCount( 0 )
		    .setPQueueFamilyIndices( 0 );

		{
			result =
			    vmaCreateBuffer(
			        alloc,
			        &static_cast<VkBufferCreateInfo&>( bufferInfo ),
			        &allocationCreateInfo,
			        &res.info.tlasInfo.buffer,
			        &res.allocation,
			        &res.allocationInfo );

			assert( result == VkResult::VK_SUCCESS );
		}

		vk::AccelerationStructureCreateInfoKHR create_info{};
		create_info
		    .setCreateFlags( vk::AccelerationStructureCreateFlagsKHR() )
		    .setBuffer( res.info.tlasInfo.buffer )
		    .setOffset( 0 )
		    .setSize( build_sizes.accelerationStructureSize )
		    .setType( vk::AccelerationStructureTypeKHR::eTopLevel )
		    .setDeviceAddress( 0 ) // if 0, this means no specific device address requested
		    ;

		res.as.tlas = device.createAccelerationStructureKHR( create_info );

		// Store memory requirements for scratch buffer into allocation info for this tlas element
		res.info.tlasInfo.scratch_buffer_size = build_sizes.buildScratchSize;
		res.info.tlasInfo.buffer_size         = build_sizes.accelerationStructureSize;

#else
		assert( false && "backend compiled without RTX features, but RTX feature requested." );
#endif

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

	VkBufferCreateInfo bufferCreateInfo =
	    static_cast<VkBufferCreateInfo&>(
	        vk::BufferCreateInfo()
	            .setSize( numBytes )
	            .setSharingMode( vk::SharingMode::eExclusive )
	            .setUsage( vk::BufferUsageFlagBits::eTransferSrc ) );

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
		self->buffers.push_back( buffer );

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
			        le_buf_resource_usage_flags_t::eIsStaging, index ) );
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
	auto lock   = std::scoped_lock( self->mtx );
	auto device = vk::Device{ self->device };

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
//
static void collect_resource_infos_per_resource(
    le_renderpass_o const* const*                 passes,
    size_t                                        numRenderPasses,
    std::vector<le_resource_handle> const&        frame_declared_resources_id,   // | pre-declared resources (declared via module)
    std::vector<le_resource_info_t> const&        frame_declared_resources_info, // | info for each pre-declared resource
    std::vector<le_resource_handle>&              usedResources,
    std::vector<std::vector<le_resource_info_t>>& usedResourcesInfos ) {

	using namespace le_renderer;

	for ( auto rp = passes; rp != passes + numRenderPasses; rp++ ) {

		auto pass_width            = renderpass_i.get_width( *rp );
		auto pass_height           = renderpass_i.get_height( *rp );
		auto pass_num_samples_log2 = get_sample_count_log_2( uint32_t( renderpass_i.get_sample_count( *rp ) ) );

		le_resource_handle const*   p_resources             = nullptr;
		LeResourceUsageFlags const* p_resources_usage_flags = nullptr;
		size_t                      resources_count         = 0;

		renderpass_i.get_used_resources( *rp, &p_resources, &p_resources_usage_flags, &resources_count );

		for ( size_t i = 0; i != resources_count; ++i ) {

			le_resource_handle const&   resource             = p_resources[ i ];             // Resource handle
			LeResourceUsageFlags const& resource_usage_flags = p_resources_usage_flags[ i ]; // Resource usage flags

			assert( resource_usage_flags.type == resource->data->type ); // Resource Usage Flags must be for matching resource type.

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
				for ( auto const& id : frame_declared_resources_id ) {
					if ( id == resource ) {
						// resource found, let's use this declared_resource_index.
						break;
					}
					found_resource_index++;
				}

				if ( found_resource_index == frame_declared_resources_id.size() ) {
					// Nothing found. Insert empty entry
					usedResources.push_back( resource );
					usedResourcesInfos.push_back( {} );
				} else {
					// Explicitly declared resource found. Insert declaration info.
					usedResources.push_back( frame_declared_resources_id[ found_resource_index ] );
					usedResourcesInfos.push_back( { frame_declared_resources_info[ found_resource_index ] } );
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

				resourceInfo.image.usage = resource_usage_flags.as.image_usage_flags;

				auto& imgInfo   = resourceInfo.image;
				auto& imgExtent = imgInfo.extent;

				imgInfo.extent_from_pass = { pass_width, pass_height, 1 };

				if ( imgInfo.usage & ( LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) ) {

					imgInfo.mipLevels         = 1;
					imgInfo.imageType         = le::ImageType::e2D;
					imgInfo.tiling            = le::ImageTiling::eOptimal;
					imgInfo.arrayLayers       = 1;
					imgInfo.sample_count_log2 = pass_num_samples_log2;

					imgExtent.width  = pass_width;
					imgExtent.height = pass_height;
				}

				// depth must be at least 1, but may arrive zero-initialised.
				imgExtent.depth = std::max<uint32_t>( imgExtent.depth, 1 );

			} else if ( resourceInfo.type == LeResourceType::eBuffer ) {
				resourceInfo.buffer.usage = resource_usage_flags.as.buffer_usage_flags;
			} else if ( resourceInfo.type == LeResourceType::eRtxBlas ) {
				resourceInfo.blas.usage = resource_usage_flags.as.rtx_blas_usage_flags;
			} else if ( resourceInfo.type == LeResourceType::eRtxTlas ) {
				resourceInfo.tlas.usage = resource_usage_flags.as.rtx_tlas_usage_flags;
			} else {
				assert( false ); // unreachable
			}

			usedResourcesInfos[ resource_index ].emplace_back( resourceInfo );

		} // end for all resources

	} // end for all passes
}

// ----------------------------------------------------------------------

static void patch_renderpass_extents(
    le_renderpass_o** passes,
    size_t            numRenderPasses,
    uint32_t          swapchainWidth,
    uint32_t          swapchainHeight ) {
	using namespace le_renderer;

	auto passes_begin = passes;
	auto passes_end   = passes + numRenderPasses;

	for ( auto rp = passes; rp != passes_end; rp++ ) {
		auto pass_width  = renderpass_i.get_width( *rp );
		auto pass_height = renderpass_i.get_height( *rp );
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

// per resource, combine resource_infos so that first element in resource infos
// contains superset of all resource_infos available for this particular resource
static void consolidate_resource_infos(
    std::vector<le_resource_info_t>& resourceInfoVersions ) {

	if ( resourceInfoVersions.empty() )
		return;

	// ---------| invariant: there is at least a first element.

	le_resource_info_t* const       first_info = resourceInfoVersions.data();
	le_resource_info_t const* const info_end   = first_info + resourceInfoVersions.size();

	switch ( first_info->type ) {
	case LeResourceType::eBuffer: {
		// Consolidate into first_info, beginning with the second element
		for ( auto* info = first_info + 1; info != info_end; info++ ) {
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

		first_info->image.samplesFlags |= uint32_t( 1 << first_info->image.sample_count_log2 );

		// Consolidate into first_info, beginning with the second element
		for ( auto* info = first_info + 1; info != info_end; info++ ) {

			// TODO (tim): check how we can enforce correct number of array layers and mip levels

			if ( info->image.arrayLayers > first_info->image.arrayLayers ) {
				first_info->image.arrayLayers = info->image.arrayLayers;
			}

			if ( info->image.mipLevels > first_info->image.mipLevels ) {
				first_info->image.mipLevels = info->image.mipLevels;
			}

			if ( uint32_t( info->image.imageType ) > uint32_t( first_info->image.imageType ) ) {
				// this is a bit sketchy.
				first_info->image.imageType = info->image.imageType;
			}

			first_info->image.flags |= info->image.flags;
			first_info->image.usage |= info->image.usage;
			first_info->image.samplesFlags |= uint32_t( 1 << info->image.sample_count_log2 );

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

			first_info->image.extent.width  = std::max( first_info->image.extent.width, info->image.extent.width );
			first_info->image.extent.height = std::max( first_info->image.extent.height, info->image.extent.height );
			first_info->image.extent.depth  = std::max( first_info->image.extent.depth, info->image.extent.depth );

			first_info->image.extent_from_pass.width  = std::max( first_info->image.extent_from_pass.width, info->image.extent_from_pass.width );
			first_info->image.extent_from_pass.height = std::max( first_info->image.extent_from_pass.height, info->image.extent_from_pass.height );
			first_info->image.extent_from_pass.depth  = std::max( first_info->image.extent_from_pass.depth, info->image.extent_from_pass.depth );
		}

		// If extents for first_info are zero, this means extents have not been explicitly specified.
		// We therefore will fall back to setting extents from pass extents.

		if ( first_info->image.extent.width == 0 ||
		     first_info->image.extent.height == 0 ||
		     first_info->image.extent.depth == 0 ) {
			first_info->image.extent = first_info->image.extent_from_pass;
		}

		// Do a final sanity check to make sure all required fields are valid.

		assert( first_info->image.extent.width * first_info->image.extent.height * first_info->image.extent.depth != 0 &&
		        "Extents with zero volume are illegal. You must specify depth, width, and height to be > 0" ); // Extents with zero volume are illegal.
		assert( first_info->image.usage != 0 );                                                                // Some kind of usage must be specified.

	} break;
	case LeResourceType::eRtxBlas: {
		for ( auto* info = first_info + 1; info != info_end; info++ ) {
			first_info->blas.usage |= info->blas.usage;
		}
		break;
	}
	case LeResourceType::eRtxTlas: {
		for ( auto* info = first_info + 1; info != info_end; info++ ) {
			first_info->tlas.usage |= info->tlas.usage;
		}
		break;
	}
	default:
		assert( false && "unhandled resource type" );
		break;
	}
}

// ----------------------------------------------------------------------

static void insert_msaa_versions(
    std::vector<le_resource_handle>&              usedResources,
    std::vector<std::vector<le_resource_info_t>>& usedResourcesInfos ) {
	// For each image resource which is specified with versions of additional sample counts
	// we create additional resource_ids (by patching in the sample count), and add matching
	// resource info, so that multisample versions of image resources can be allocated dynamically.

	const size_t usedResourcesSize = usedResources.size();

	std::vector<le_resource_handle>              msaa_resources;
	std::vector<std::vector<le_resource_info_t>> msaa_resource_infos;

	for ( size_t i = 0; i != usedResourcesSize; ++i ) {

		le_resource_handle& resource = usedResources[ i ];

		if ( resource->data->type != LeResourceType::eImage ) {
			continue;
		}
		le_resource_info_t& resourceInfo = usedResourcesInfos[ i ][ 0 ]; ///< consolidated resource info for this resource over all passes

		// --------| invariant: resource is image

		if ( resourceInfo.image.samplesFlags & ~uint32( le::SampleCountFlagBits::e1 ) ) {

			// We found a resource with flags requesting more than just single sample.
			// for each flag we must clone the current resource and add to extra resources

			uint16_t current_sample_count_log_2 = get_sample_count_log_2( resourceInfo.image.samplesFlags );

			le_resource_handle resource_copy =
			    le_renderer::renderer_i.produce_img_resource_handle(
			        resource->data->debug_name, current_sample_count_log_2, static_cast<le_img_resource_handle>( resource ), 0 );

			le_resource_info_t resource_info_copy = resourceInfo;

			// Patch original resource info to note 1 sample - we do this because
			// handle and info must be in sync.
			//
			resourceInfo.image.sample_count_log2 = 0;

			msaa_resources.push_back( resource_copy );
			msaa_resource_infos.push_back( { resource_info_copy } );
		}
	}

	// -- Insert additional msaa resources into usedResources
	// -- Insert additional msaa resource infos into usedResourceInfos

	usedResources.insert( usedResources.end(), msaa_resources.begin(), msaa_resources.end() );
	usedResourcesInfos.insert( usedResourcesInfos.end(), msaa_resource_infos.begin(), msaa_resource_infos.end() );
}

// ----------------------------------------------------------------------

static void printResourceInfo( le_resource_handle const& handle, ResourceCreateInfo const& info ) {
	static auto logger = LeLog( LOGGER_LABEL );
	if ( info.isBuffer() ) {
		logger.info( "% 32s : %11d : %30s : %30s", handle->data->debug_name, info.bufferInfo.size, "-", to_string( vk::BufferUsageFlags( info.bufferInfo.usage ) ).c_str() );
	} else if ( info.isImage() ) {
		logger.info( "%p, % 32s : %dx%dx%d : %30s : %30s : %5s samples",
		             handle,
		             !( handle->data->debug_name[ 0 ] == '\0' )
		                 ? handle->data->debug_name
		             : handle->data->reference_handle
		                 ? handle->data->reference_handle->data->debug_name
		                 : "unnamed",
		             info.imageInfo.extent.width,
		             info.imageInfo.extent.height,
		             info.imageInfo.extent.depth,
		             to_string( vk::Format( info.imageInfo.format ) ).c_str(),
		             to_string( vk::ImageUsageFlags( info.imageInfo.usage ) ).c_str(),
		             to_string( vk::SampleCountFlags( info.imageInfo.samples ) ).c_str() );
	} else if ( info.isBlas() ) {
		logger.info( "% 32s : %11d : (%28d) : %30s",
		             handle->data->debug_name,
		             info.blasInfo.buffer_size,
		             info.blasInfo.scratch_buffer_size,
		             "-" );
	} else if ( info.isTlas() ) {
		logger.info( "% 32s : %11d : (%28d) : %30s",
		             handle->data->debug_name,
		             info.tlasInfo.buffer_size,
		             info.tlasInfo.scratch_buffer_size,
		             "-" );
	}
}

// ----------------------------------------------------------------------

static bool inferImageFormat( le_backend_o* self, le_img_resource_handle const& resource, LeImageUsageFlags const& usageFlags, ResourceCreateInfo* createInfo ) {

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
		createInfo->imageInfo.format = VkFormat( le_format_to_vk( inferred_format ) );
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

	auto vk_result_assert_success = []( vk::Result const&& result ) {
		if ( result != vk::Result::eSuccess ) {
			logger.error( "Vulkan operation returned: '%s', but we expected vk::Result::eSuccess.", vk::to_string( result ).c_str() );
		}
		assert( result == vk::Result::eSuccess && "Vulkan operation must succeed" );
	};

	for ( auto const& r : resources ) {

		auto device = vk::Device( device_ );

		vk::DebugUtilsObjectNameInfoEXT nameInfo;

		nameInfo.setPObjectName( r.first->data->debug_name );

		switch ( r.first->data->type ) {
		case LeResourceType::eImage:
			nameInfo.setObjectType( vk::ObjectType::eImage );
			nameInfo.setObjectHandle( reinterpret_cast<uint64_t>( r.second.as.image ) );
			break;
		case LeResourceType::eBuffer:
			nameInfo.setObjectType( vk::ObjectType::eBuffer );
			nameInfo.setObjectHandle( reinterpret_cast<uint64_t>( r.second.as.buffer ) );
			break;
		case LeResourceType::eRtxBlas:
			nameInfo.setObjectType( vk::ObjectType::eAccelerationStructureKHR );
			nameInfo.setObjectHandle( reinterpret_cast<uint64_t>( r.second.as.blas ) );
			break;
		case LeResourceType::eRtxTlas:
			nameInfo.setObjectType( vk::ObjectType::eAccelerationStructureKHR );
			nameInfo.setObjectHandle( reinterpret_cast<uint64_t>( r.second.as.tlas ) );
			break;
		default:
			assert( false && "unknown resource type" );
		}

		vk_result_assert_success( device.setDebugUtilsObjectNameEXT( &nameInfo ) );
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

#ifdef LE_FEATURE_RTX
	static le_resource_handle LE_RTX_SCRATCH_BUFFER_HANDLE = LE_BUF_RESOURCE( "le_rtx_scratch_buffer_handle" ); // opaque handle for rtx scratch buffer
#endif

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
	std::vector<le_resource_handle>              usedResources;      // (
	std::vector<std::vector<le_resource_info_t>> usedResourcesInfos; // ( usedResourceInfos[index] contains vector of usages for usedResource[index]

	collect_resource_infos_per_resource(
	    passes, numRenderPasses,
	    frame.declared_resources_id, frame.declared_resources_info,
	    usedResources, usedResourcesInfos );

	assert( usedResources.size() == usedResourcesInfos.size() );

	// For each resource, consolidate infos so that the first element in the vector of
	// resourceInfos for a resource covers all intended usages of a resource.
	//
	for ( auto& versions : usedResourcesInfos ) {
		consolidate_resource_infos( versions );
	}

	// For each image resource which has versions of additional sample counts
	// we create additional resource_ids (by patching in the sample count), and add matching
	// resource info, so that multisample versions of image resources can be allocated dynamically.
	insert_msaa_versions( usedResources, usedResourcesInfos );

	// Check if all resources declared in this frame are already available in backend.
	// If a resource is not available yet, this resource must be allocated.

	auto& backendResources = self->only_backend_allocate_resources_may_access.allocatedResources;

	const size_t usedResourcesCount = usedResources.size();
	for ( size_t i = 0; i != usedResourcesCount; ++i ) {

		le_resource_handle const& resource     = usedResources[ i ];
		le_resource_info_t const& resourceInfo = usedResourcesInfos[ i ][ 0 ]; ///< consolidated resource info for this resource over all passes

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

		auto       resourceCreateInfo = ResourceCreateInfo::from_le_resource_info( resourceInfo, &self->queueFamilyIndexGraphics, 0 );
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

			if ( PRINT_DEBUG_MESSAGES || true ) {
				//				logger.info( "Allocated resource: " );
				printResourceInfo( resource, allocatedResource.info );
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

				if ( PRINT_DEBUG_MESSAGES || true ) {
					logger.info( "Re-allocated resource: " );
					printResourceInfo( resource, allocatedResource.info );
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

#ifdef LE_FEATURE_RTX
	// -- Create rtx acceleration structure scratch buffer
	{
		// In case there are acceleration structures with the `build` flag set, we must allocate
		// a scratch buffer which is large enough to hold the largest of the acceleration structures
		// with the build flag set

		// TODO: this should also apply for any acceleration structures which have the `update` flag
		// set, as updating requires a scratch buffer too.

		uint64_t scratchbuffer_max_size = 0;

		const size_t usedResourcesCount = usedResources.size();
		for ( size_t i = 0; i != usedResourcesCount; ++i ) {

			le_resource_handle const& resourceId   = usedResources[ i ];
			le_resource_info_t const& resourceInfo = usedResourcesInfos[ i ][ 0 ]; ///< consolidated resource info for this resource over all passes

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
			resourceInfo.buffer.usage             = { LE_BUFFER_USAGE_STORAGE_BUFFER_BIT | LE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
			resourceInfo.type                     = LeResourceType::eBuffer;
			ResourceCreateInfo resourceCreateInfo = ResourceCreateInfo::from_le_resource_info( resourceInfo, &self->queueFamilyIndexGraphics, 0 );
			auto               resource_id        = LE_RTX_SCRATCH_BUFFER_HANDLE;
			auto               allocated_resource = allocate_resource_vk( self->mAllocator, resourceCreateInfo, self->device->getVkDevice() );
			frame.availableResources.insert_or_assign( resource_id, allocated_resource );

			// We immediately bin the buffer resource, so that its lifetime is tied to the current frame.
			frame.binnedResources.insert_or_assign( resource_id, allocated_resource );
		}
	}
#endif

	// If we locked backendResources with a mutex, this would be the right place to release it.

	if ( PRINT_DEBUG_MESSAGES ) {
		logger.info( "Available Resources" );
		logger.info( "%10s : %38s : %30s", "Type", "debugName", "Vk Handle" );
		for ( auto const& r : frame.availableResources ) {
			if ( r.second.info.isBuffer() ) {
				logger.info( "%10s : %37s : %30p",
				             "Buffer",
				             r.first->data->debug_name,
				             r.second.as.buffer );
			} else {
				logger.info( "%10s : %30s (s: %2d) : %30p",
				             "Image",
				             r.first->data->debug_name,
				             1 << r.first->data->num_samples,
				             r.second.as.buffer );
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
static void frame_allocate_transient_resources( BackendFrameData& frame, vk::Device const& device, le_renderpass_o** passes, size_t numRenderPasses ) {

	using namespace le_renderer;
	static auto logger = LeLog( LOGGER_LABEL );

	// Only for compute passes: Create imageviews for all available
	// resources which are of type image and which have usage
	// sampled or storage.
	//
	for ( auto p = passes; p != passes + numRenderPasses; p++ ) {

		if ( renderpass_i.get_type( *p ) != LeRenderPassType::LE_RENDER_PASS_TYPE_COMPUTE ) {
			continue;
		}

		const le_resource_handle*   resources      = nullptr;
		const LeResourceUsageFlags* resource_usage = nullptr;
		size_t                      resource_count = 0;

		renderpass_i.get_used_resources( *p, &resources, &resource_usage, &resource_count );

		for ( size_t i = 0; i != resource_count; ++i ) {
			auto const& r_usage_flags = resource_usage[ i ];

			if ( r_usage_flags.type == LeResourceType::eImage &&
			     ( r_usage_flags.as.image_usage_flags & ( LE_IMAGE_USAGE_SAMPLED_BIT | LE_IMAGE_USAGE_STORAGE_BIT ) ) ) {
				auto const& r = static_cast<le_img_resource_handle>( resources[ i ] );

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

				vk::Format imageFormat = vk::Format( vk_resource_info.info.imageInfo.format );

				// If the format is still undefined at this point, we can only throw our hands up in the air...
				//
				if ( imageFormat == vk::Format::eUndefined ) {
					logger.warn( "Cannot create default view for image: '%s;, as format is unspecified", r->data->debug_name );
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
				    .setImage( vk_resource_info.as.image )
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

				vk::ImageView imageView{};
				{
					// Set or create vkImageview

					auto imageFormat = vk::Format( frame_data_get_image_format_from_texture_info( frame, texInfo ) );

					vk::ImageSubresourceRange subresourceRange;
					subresourceRange
					    .setAspectMask( get_aspect_flags_from_format( imageFormat ) )
					    .setBaseMipLevel( 0 )
					    .setLevelCount( VK_REMAINING_MIP_LEVELS ) // we set VK_REMAINING_MIP_LEVELS which activates all mip levels remaining.
					    .setBaseArrayLayer( texInfo.imageView.base_array_layer )
					    .setLayerCount( VK_REMAINING_ARRAY_LAYERS ); // FIXME: texInfo.imageView.layer_count must be 6 if imageView.type is cubemap

					// TODO: fill in additional image view create info based on info from pass...

					vk::ImageViewCreateInfo imageViewCreateInfo{};
					imageViewCreateInfo
					    .setFlags( {} ) // no special flags
					    .setImage( frame_data_get_image_from_le_resource_id( frame, texInfo.imageView.imageId ) )
					    .setViewType( le_to_vk( texInfo.imageView.image_view_type ) )
					    .setFormat( imageFormat ) // we got this earlier via texInfo
					    .setComponents( {} )      // default component mapping
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
	// This is so that the every semaphore for presentComplete is correctly
	// waited upon.

	bool acquire_success = true;

	using namespace le_swapchain_vk;

	for ( size_t i = 0; i != self->swapchains.size(); ++i ) {
		if ( !swapchain_i.acquire_next_image(
		         self->swapchains[ i ],
		         frame.swapchain_state[ i ].presentComplete,
		         frame.swapchain_state[ i ].image_idx ) ) {
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

	for ( size_t i = 0; i != self->swapchains.size(); ++i ) {
		// Acquire swapchain image

		// ----------| invariant: swapchain acquisition successful.

		frame.swapchain_state[ i ].surface_width  = swapchain_i.get_image_width( self->swapchains[ i ] );
		frame.swapchain_state[ i ].surface_height = swapchain_i.get_image_height( self->swapchains[ i ] );

		// TODO: we should be able to query swapchain image info so that we can mark the
		// swapchain image as a frame available resource.

		auto const& img_resource_handle = self->swapchain_resources[ i ];

		frame.availableResources[ img_resource_handle ].as.image = swapchain_i.get_image( self->swapchains[ i ], frame.swapchain_state[ i ].image_idx );
		{
			auto& backbufferInfo       = frame.availableResources[ img_resource_handle ].info.imageInfo;
			backbufferInfo             = static_cast<VkImageCreateInfo&>( vk::ImageCreateInfo{} );
			backbufferInfo.extent      = static_cast<VkExtent3D&>( vk::Extent3D( frame.swapchain_state[ i ].surface_width, frame.swapchain_state[ i ].surface_height, 1 ) );
			backbufferInfo.format      = VkFormat( self->swapchainImageFormat[ i ] );
			backbufferInfo.usage       = VkImageUsageFlags( vk::ImageUsageFlagBits::eColorAttachment );
			backbufferInfo.mipLevels   = 1;
			backbufferInfo.arrayLayers = 1;
		}
	}

	// For all passes - set pass width/height to swapchain width/height if not known.

	assert( !frame.swapchain_state.empty() && "frame.swapchains_state must not be empty" );

	// Only extents of swapchain[0] are used to infer extents for renderpasses which lack extents info
	patch_renderpass_extents(
	    passes,
	    numRenderPasses,
	    frame.swapchain_state[ 0 ].surface_width,
	    frame.swapchain_state[ 0 ].surface_height );

	// Setup declared resources per frame - These are resources declared using resource infos
	// which are explicitly declared by user via the rendermodule, but which may or may not be
	// actually used in the frame.

	frame.declared_resources_id   = { declared_resources, declared_resources + declared_resources_count };
	frame.declared_resources_info = { declared_resources_infos, declared_resources_infos + declared_resources_count };

	backend_allocate_resources( self, frame, passes, numRenderPasses );

	// Initialise sync chain table - each resource receives initial state
	// from current entry in frame.availableResources resource map.
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

		static le_resource_handle LE_RTX_SCRATCH_BUFFER_HANDLE = LE_BUF_RESOURCE( "le_rtx_scratch_buffer_handle" ); // opaque handle for rtx scratch buffer
		// Update final sync state for each pre-existing backend resource.
		auto& backendResources = self->only_backend_allocate_resources_may_access.allocatedResources;
		for ( auto const& tbl : frame.syncChainTable ) {
			auto& resId       = tbl.first;
			auto& resSyncList = tbl.second;

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

	vk::Device device = self->device->getVkDevice();

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

	auto& frame = self->mFrames[ frameIndex ];

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

		VkBufferCreateInfo bufferCreateInfo;
		{
			// we use the cpp proxy because it's more ergonomic to fill the values.
			vk::BufferCreateInfo bufferInfoProxy;
			bufferInfoProxy
			    .setFlags( {} )
			    .setSize( LE_LINEAR_ALLOCATOR_SIZE )
			    .setUsage( LE_BUFFER_USAGE_FLAGS_SCRATCH )
			    .setSharingMode( vk::SharingMode::eExclusive )
			    .setQueueFamilyIndexCount( 1 )
			    .setPQueueFamilyIndices( &self->queueFamilyIndexGraphics ); // TODO: use compute queue for compute passes, or transfer for transfer passes
			bufferCreateInfo = static_cast<VkBufferCreateInfo&>( bufferInfoProxy );
		}

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

static bool updateArguments( const vk::Device&                  device,
                             const vk::DescriptorPool&          descriptorPool_,
                             const ArgumentState&               argumentState,
                             std::array<DescriptorSetState, 8>& previousSetData,
                             vk::DescriptorSet*                 descriptorSets ) {

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

		static constexpr auto NULL_VK_BUFFER                     = vk::Buffer( nullptr );
		static constexpr auto NULL_VK_IMAGE_VIEW                 = vk::ImageView( nullptr );
		static constexpr auto NULL_VK_ACCELERATION_STRUCTURE_KHR = vk::AccelerationStructureKHR( nullptr );

		for ( auto& a : argumentState.setData[ setId ] ) {

			switch ( a.type ) {
			case vk::DescriptorType::eStorageBufferDynamic: //
			case vk::DescriptorType::eUniformBuffer:        //
			case vk::DescriptorType::eUniformBufferDynamic: //
			case vk::DescriptorType::eStorageBuffer:        // fall-through
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
			case vk::DescriptorType::eCombinedImageSampler:
			case vk::DescriptorType::eSampledImage:
			case vk::DescriptorType::eStorageImage:
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
			case vk::DescriptorType::eAccelerationStructureKHR:
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

				vk::DescriptorSetAllocateInfo allocateInfo;
				allocateInfo.setDescriptorPool( descriptorPool_ )
				    .setDescriptorSetCount( 1 )
				    .setPSetLayouts( &argumentState.layouts[ setId ] );

				// -- allocate descriptorSets based on current layout
				// and place them in the correct position
				auto result = device.allocateDescriptorSets( &allocateInfo, &descriptorSets[ setId ] );

				assert( result == vk::Result::eSuccess && "failed to allocate descriptor set" );

				if ( /* DISABLES CODE */ ( false ) ) {
					// I wish that this would work - but it appears that accelerator decriptors cannot be updated using templates.
					device.updateDescriptorSetWithTemplate( descriptorSets[ setId ], argumentState.updateTemplates[ setId ], argumentState.setData[ setId ].data() );
				} else {

					std::vector<vk::WriteDescriptorSet> write_descriptor_sets;

					// We deliberately allocate write descriptor set acceleration structure objects on the heap,
					// so that the pointer to the object will not change if and when the vector grows.
					//
					// This means that we can hand out copies of pointers from this vector without fear from
					// within the current scope, but also that we must clean up the contents of the vector
					// manually before leaving the current scope or else we will leak these objects.
					std::vector<vk::WriteDescriptorSetAccelerationStructureKHR*> write_acceleration_structures;

					write_descriptor_sets.reserve( argumentState.setData[ setId ].size() );

					for ( auto& a : argumentState.setData[ setId ] ) {
						vk::WriteDescriptorSet w{};

						w
						    .setDstSet( descriptorSets[ setId ] )
						    .setDstBinding( a.bindingNumber )
						    .setDstArrayElement( a.arrayIndex )
						    .setDescriptorCount( 1 )
						    .setDescriptorType( a.type ) //
						    ;

						switch ( a.type ) {
						case vk::DescriptorType::eSampler:
						case vk::DescriptorType::eCombinedImageSampler:
						case vk::DescriptorType::eSampledImage:
						case vk::DescriptorType::eStorageImage:
						case vk::DescriptorType::eInputAttachment:
							w.setPImageInfo( reinterpret_cast<vk::DescriptorImageInfo const*>( &a.imageInfo ) );
							break;
						case vk::DescriptorType::eUniformTexelBuffer:
						case vk::DescriptorType::eStorageTexelBuffer:
							w.setPTexelBufferView( reinterpret_cast<vk::BufferView const*>( &a.texelBufferInfo ) );
							break;
						case vk::DescriptorType::eUniformBuffer:
						case vk::DescriptorType::eStorageBuffer:
						case vk::DescriptorType::eUniformBufferDynamic:
						case vk::DescriptorType::eStorageBufferDynamic:
							w.setPBufferInfo( reinterpret_cast<vk::DescriptorBufferInfo const*>( &a.bufferInfo ) );
							break;
						case vk::DescriptorType::eInlineUniformBlockEXT:
							assert( false && "inline uniform blocks are not yet supported" );
							break;
						case vk::DescriptorType::eAccelerationStructureNV:
							assert( false && "NV acceleration structures are not supported anymore. Use KHR acceleration structures." );
							break;
						case vk::DescriptorType::eAccelerationStructureKHR: {
							auto wd                        = new vk::WriteDescriptorSetAccelerationStructureKHR{};
							wd->accelerationStructureCount = 1;
							wd->pAccelerationStructures    = &a.accelerationStructureInfo.accelerationStructure;
							w.setPNext( wd );
						} break;
						default:
							assert( false && "Unhandled descriptor Type" );
						}

						write_descriptor_sets.emplace_back( w );
					}
					device.updateDescriptorSets( uint32_t( write_descriptor_sets.size() ), write_descriptor_sets.data(), 0, nullptr );

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
                case(le::CommandType::eDrawMeshTasks): os << "eDrawMeshTasks"; break;
                case(le::CommandType::eTraceRays): os << "eTraceRays"; break;
                case(le::CommandType::eSetArgumentTlas): os << "eSetArgumentTlas"; break;
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

// ----------------------------------------------------------------------
// Decode commandStream for each pass (may happen in parallel)
// translate into vk specific commands.
static void backend_process_frame( le_backend_o* self, size_t frameIndex ) {

	static auto logger = LeLog( LOGGER_LABEL );

	if ( PRINT_DEBUG_MESSAGES ) {
		logger.debug( "** Process Frame #%8d **", frameIndex );
	}

	using namespace le_renderer;   // for encoder
	using namespace le_backend_vk; // for device

#ifdef NDEBUG
	bool should_insert_debug_labels = false; // whether we want to insert debug labels into the command stream (useful for renderdoc)
#else
	bool should_insert_debug_labels = true; // whether we want to insert debug labels into the command stream (useful for renderdoc)
#endif
	auto& frame = self->mFrames[ frameIndex ];

	vk::Device device = self->device->getVkDevice();

	static_assert( sizeof( vk::Viewport ) == sizeof( le::Viewport ), "Viewport data size must be same in vk and le" );
	static_assert( sizeof( vk::Rect2D ) == sizeof( le::Rect2D ), "Rect2D data size must be same in vk and le" );

	static auto maxVertexInputBindings = vk_device_i.get_vk_physical_device_properties( *self->device ).limits.maxVertexInputBindings;

	// TODO: (parallelize) when going wide, there needs to be a commandPool for each execution context so that
	// command buffer generation may be free-threaded.
	auto numCommandBuffers = uint32_t( frame.passes.size() );
	auto cmdBufs           = device.allocateCommandBuffers( { frame.commandPool, vk::CommandBufferLevel::ePrimary, numCommandBuffers } );

	std::array<vk::ClearValue, 16> clearValues{};

	// TODO: (parallel for)
	// note that access to any caches when creating pipelines and layouts and descriptorsets must be
	// mutex-controlled when processing happens concurrently.
	for ( size_t passIndex = 0; passIndex != frame.passes.size(); ++passIndex ) {

		auto& pass           = frame.passes[ passIndex ];
		auto& cmd            = cmdBufs[ passIndex ];
		auto& descriptorPool = frame.descriptorPools[ passIndex ];

		// create frame buffer, based on swapchain and renderpass

		cmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

		if ( should_insert_debug_labels ) {
			vk::DebugUtilsLabelEXT labelInfo;
			labelInfo.pLabelName = pass.debugName.c_str();

			static constexpr auto LE_COLOUR_LIGHTBLUE    = hex_rgba_to_float_colour( 0x61BBEFFF );
			static constexpr auto LE_COLOUR_GREENY_BLUE  = hex_rgba_to_float_colour( 0x4EC9B0FF );
			static constexpr auto LE_COLOUR_BRICK_ORANGE = hex_rgba_to_float_colour( 0xCE4B0EFF );
			static constexpr auto LE_COLOUR_PALE_PEACH   = hex_rgba_to_float_colour( 0xFFDBA3FF );

			switch ( pass.type ) {
			case LeRenderPassType::LE_RENDER_PASS_TYPE_COMPUTE:
				labelInfo.setColor( LE_COLOUR_LIGHTBLUE );
				break;
			case LeRenderPassType::LE_RENDER_PASS_TYPE_DRAW:
				labelInfo.setColor( LE_COLOUR_GREENY_BLUE );
				break;
			case LeRenderPassType::LE_RENDER_PASS_TYPE_TRANSFER:
				labelInfo.setColor( LE_COLOUR_BRICK_ORANGE );
				break;
			default:
				break;
			}

			cmd.beginDebugUtilsLabelEXT( labelInfo );
		}

		{

			if ( PRINT_DEBUG_MESSAGES ) {
				logger.debug( "Renderpass '%s'", pass.debugName.c_str() );
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

					if ( PRINT_DEBUG_MESSAGES ) {

						// --------| invariant: barrier is active.

						// print out sync chain for sampled image
						logger.debug( "\t Explicit Barrier for: %s (s: %d)", op.resource->data->debug_name, 1 << op.resource->data->num_samples );
						logger.debug( "\t % 3s : % 30s : % 30s : % 10s", "#", "visible_access", "write_stage", "layout" );

						auto const& syncChain = frame.syncChainTable.at( op.resource );

						for ( size_t i = op.sync_chain_offset_initial; i <= op.sync_chain_offset_final; i++ ) {
							auto const& s = syncChain[ i ];
							logger.debug( "\t % 3d : % 30s : % 30s : % 10s", i,
							              to_string( s.visible_access ).c_str(),
							              to_string( s.write_stage ).c_str(),
							              to_string( s.layout ).c_str() );
						}
					}

					auto dstImage = frame_data_get_image_from_le_resource_id( frame, static_cast<le_img_resource_handle>( op.resource ) );

					vk::ImageSubresourceRange rangeAllMiplevels;
					rangeAllMiplevels
					    .setAspectMask( vk::ImageAspectFlagBits::eColor )
					    .setBaseMipLevel( 0 )
					    .setLevelCount( VK_REMAINING_MIP_LEVELS ) // we want all miplevels to be in transferDstOptimal.
					    .setBaseArrayLayer( 0 )
					    .setLayerCount( VK_REMAINING_ARRAY_LAYERS );

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
					    {},                     // buffer: host write -> transfer read
					    { imageLayoutTransfer } // image: transfer layout
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
			    .setRenderArea( vk::Rect2D( { 0, 0 }, { pass.width, pass.height } ) )
			    .setClearValueCount( pass.numColorAttachments + pass.numDepthStencilAttachments )
			    .setPClearValues( clearValues.data() );

			cmd.beginRenderPass( renderPassBeginInfo, vk::SubpassContents::eInline );
		}

		// -- Translate intermediary command stream data to api-native instructions

		void*    commandStream = nullptr;
		size_t   dataSize      = 0;
		size_t   numCommands   = 0;
		size_t   commandIndex  = 0;
		uint32_t subpassIndex  = 0;

		vk::PipelineLayout currentPipelineLayout                          = nullptr;
		vk::DescriptorSet  descriptorSets[ VK_MAX_BOUND_DESCRIPTOR_SETS ] = {}; // currently bound descriptorSets (allocated from pool, therefore we must not worry about freeing, and may re-use freely)

		// We store currently bound descriptors so that we only allocate new DescriptorSets
		// if the descriptors really change. With dynamic descriptors, it is very likely
		// that we don't need to allocate new descriptors, as the same descriptors are used
		// for different accessors, only with different dynamic binding offsets.
		//
		//
		std::array<DescriptorSetState, 8> previousSetState; ///< currently bound descriptorSetLayout+Data for each set

		ArgumentState argumentState{};

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

		RtxState                      rtx_state{};                                                                      // used to keep track of shader binding tables bound with rtx pipelines.
		static le_buf_resource_handle LE_RTX_SCRATCH_BUFFER_HANDLE = LE_BUF_RESOURCE( "le_rtx_scratch_buffer_handle" ); // opaque handle for rtx scratch buffer

		if ( pass.encoder ) {
			encoder_i.get_encoded_data( pass.encoder, &commandStream, &dataSize, &numCommands );
		} else {
			// This is legit behaviour for draw passes which are used only to clear attachments,
			// in which case they don't need to include any draw commands.
		}

		if ( commandStream != nullptr && numCommands > 0 ) {

			le_pipeline_manager_o* pipelineManager = encoder_i.get_pipeline_manager( pass.encoder );

			std::vector<vk::Buffer>       vertexInputBindings( maxVertexInputBindings, nullptr );
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

					if ( pass.type == LE_RENDER_PASS_TYPE_DRAW ) {
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

										descriptorData.type          = vk::DescriptorType( b.type );
										descriptorData.bindingNumber = uint32_t( b.binding );
										descriptorData.arrayIndex    = uint32_t( arrayIndex );

										if ( b.type == vk::DescriptorType::eStorageBuffer ||
										     b.type == vk::DescriptorType::eUniformBuffer ||
										     b.type == vk::DescriptorType::eStorageBufferDynamic ||
										     b.type == vk::DescriptorType::eUniformBufferDynamic ) {

											descriptorData.bufferInfo.range = b.range;
										}

										setData.emplace_back( descriptorData );
									}

									if ( b.type == vk::DescriptorType::eStorageBufferDynamic ||
									     b.type == vk::DescriptorType::eUniformBufferDynamic ) {
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

							cmd.bindPipeline( vk::PipelineBindPoint::eGraphics, currentPipeline.pipeline );
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

										descriptorData.type          = vk::DescriptorType( b.type );
										descriptorData.bindingNumber = uint32_t( b.binding );
										descriptorData.arrayIndex    = uint32_t( arrayIndex );

										descriptorData.bufferInfo.range = b.range;

										setData.emplace_back( std::move( descriptorData ) );
									}

									if ( b.type == vk::DescriptorType::eStorageBufferDynamic ||
									     b.type == vk::DescriptorType::eUniformBufferDynamic ) {
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

				case le::CommandType::eBindRtxPipeline: {
					auto* le_cmd = static_cast<le::CommandBindRtxPipeline*>( dataIt );
					if ( pass.type == LE_RENDER_PASS_TYPE_COMPUTE ) {
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

										descriptorData.type          = vk::DescriptorType( b.type );
										descriptorData.bindingNumber = uint32_t( b.binding );
										descriptorData.arrayIndex    = uint32_t( arrayIndex );

										if ( b.type == vk::DescriptorType::eStorageBuffer ||
										     b.type == vk::DescriptorType::eUniformBuffer ||
										     b.type == vk::DescriptorType::eStorageBufferDynamic ||
										     b.type == vk::DescriptorType::eUniformBufferDynamic ) {

											descriptorData.bufferInfo.range = b.range;
										}

										setData.emplace_back( std::move( descriptorData ) );
									}

									if ( b.type == vk::DescriptorType::eStorageBufferDynamic ||
									     b.type == vk::DescriptorType::eUniformBufferDynamic ) {
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

						cmd.bindPipeline( vk::PipelineBindPoint::eRayTracingKHR, currentPipeline.pipeline );

						// -- "bind" shader binding table state

						rtx_state.sbt_buffer = le_cmd->info.sbt_buffer;

						VkBuffer vk_buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.sbt_buffer );
						uint64_t offset    = device.getBufferAddress( { vk_buffer } );

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

					} else {
						// -- TODO: warn that rtx pipelines may only be bound within
						// compute passes.
					}

				} break;
#ifdef LE_FEATURE_RTX
				case le::CommandType::eTraceRays: {
					auto* le_cmd = static_cast<le::CommandTraceRays*>( dataIt );

					// -- update descriptorsets via template if tainted
					bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

					if ( false == argumentsOk ) {
						break;
					}

					// --------| invariant: arguments were updated successfully

					if ( argumentState.setCount > 0 ) {

						cmd.bindDescriptorSets( vk::PipelineBindPoint::eRayTracingKHR,
						                        currentPipelineLayout,
						                        0,
						                        argumentState.setCount,
						                        descriptorSets,
						                        argumentState.dynamicOffsetCount,
						                        argumentState.dynamicOffsets.data() );
					}

					assert( rtx_state.is_set && "sbt state must have been set before calling traceRays" );

					// vk::Buffer sbt_vk_buffer = frame_data_get_buffer_from_le_resource_id( frame, rtx_state.sbt_buffer );

					//					std::cout << "sbt buffer: " << std::hex << sbt_vk_buffer << std::endl
					//					          << std::flush;
					//					std::cout << "sbt buffer raygen offset: " << std::dec << rtx_state.ray_gen_sbt_offset << std::endl
					//					          << std::flush;

					// buffer, offset, stride, size
					vk::StridedDeviceAddressRegionKHR sbt_ray_gen{ rtx_state.ray_gen_sbt_offset, rtx_state.ray_gen_sbt_size, rtx_state.ray_gen_sbt_size };
					vk::StridedDeviceAddressRegionKHR sbt_miss{ rtx_state.miss_sbt_offset, rtx_state.miss_sbt_stride, rtx_state.miss_sbt_size };
					vk::StridedDeviceAddressRegionKHR sbt_hit{ rtx_state.hit_sbt_offset, rtx_state.hit_sbt_stride, rtx_state.hit_sbt_size };
					vk::StridedDeviceAddressRegionKHR sbt_callable{ rtx_state.callable_sbt_offset, rtx_state.callable_sbt_stride, rtx_state.callable_sbt_size };

					cmd.traceRaysKHR(
					    sbt_ray_gen,
					    sbt_miss,
					    sbt_hit,
					    sbt_callable,
					    le_cmd->info.width,
					    le_cmd->info.height,
					    le_cmd->info.depth //
					);

				} break;
#endif
				case le::CommandType::eDispatch: {
					auto* le_cmd = static_cast<le::CommandDispatch*>( dataIt );

					// -- update descriptorsets via template if tainted
					bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

					if ( false == argumentsOk ) {
						break;
					}

					// --------| invariant: arguments were updated successfully

					if ( argumentState.setCount > 0 ) {

						cmd.bindDescriptorSets(
						    vk::PipelineBindPoint::eCompute,
						    currentPipelineLayout,
						    0,
						    argumentState.setCount,
						    descriptorSets,
						    argumentState.dynamicOffsetCount,
						    argumentState.dynamicOffsets.data() );
					}

					cmd.dispatch( le_cmd->info.groupCountX, le_cmd->info.groupCountY, le_cmd->info.groupCountZ );

				} break;
				case le::CommandType::eBufferMemoryBarrier: {
					auto*                   le_cmd = static_cast<le::CommandBufferMemoryBarrier*>( dataIt );
					vk::BufferMemoryBarrier bufferMemoryBarrier{};
					bufferMemoryBarrier
					    .setBuffer( frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer ) )
					    .setSize( le_cmd->info.range )
					    .setOffset( le_cmd->info.offset )
					    .setDstAccessMask( le_to_vk( le_cmd->info.dstAccessMask ) ) //
					    ;

					cmd.pipelineBarrier(
					    le_to_vk( le_cmd->info.srcStageMask ),
					    le_to_vk( le_cmd->info.dstStageMask ),
					    {}, {}, { bufferMemoryBarrier }, {} );

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
					auto* le_cmd = static_cast<le::CommandDrawIndexed*>( dataIt );

					// -- update descriptorsets via template if tainted
					bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

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

				case le::CommandType::eDrawMeshTasks: {
					auto* le_cmd = static_cast<le::CommandDrawMeshTasks*>( dataIt );

					// -- update descriptorsets via template if tainted
					bool argumentsOk = updateArguments( device, descriptorPool, argumentState, previousSetState, descriptorSets );

					if ( false == argumentsOk ) {
						break;
					}
#ifdef LE_FEATURE_MESH_SHADER_NV

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

					cmd.drawMeshTasksNV( le_cmd->info.taskCount, le_cmd->info.firstTask );
#else
					break;
#endif
				} break;

				case le::CommandType::eSetLineWidth: {
					auto* le_cmd = static_cast<le::CommandSetLineWidth*>( dataIt );
					cmd.setLineWidth( le_cmd->info.width );
				} break;

				case le::CommandType::eSetViewport: {
					auto* le_cmd = static_cast<le::CommandSetViewport*>( dataIt );
					// Since data for viewports *is stored inline*, we increment the typed pointer
					// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
					cmd.setViewport( le_cmd->info.firstViewport, le_cmd->info.viewportCount, reinterpret_cast<vk::Viewport*>( le_cmd + 1 ) );
				} break;

				case le::CommandType::eSetScissor: {
					auto* le_cmd = static_cast<le::CommandSetScissor*>( dataIt );
					// Since data for scissors *is stored inline*, we increment the typed pointer
					// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
					cmd.setScissor( le_cmd->info.firstScissor, le_cmd->info.scissorCount, reinterpret_cast<vk::Rect2D*>( le_cmd + 1 ) );
				} break;

				case le::CommandType::eSetPushConstantData: {
					if ( currentPipelineLayout ) {
						auto*                le_cmd               = static_cast<le::CommandSetPushConstantData*>( dataIt );
						vk::ShaderStageFlags active_shader_stages = vk::ShaderStageFlags( currentPipeline.layout_info.active_vk_shader_stages );
						cmd.pushConstants( currentPipelineLayout, active_shader_stages, 0, le_cmd->info.num_bytes, ( le_cmd + 1 ) ); // Note that we fetch inline data at (le_cmd + 1)
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
					if ( b->type == vk::DescriptorType::eStorageBufferDynamic ||
					     b->type == vk::DescriptorType::eUniformBufferDynamic ) {
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

					bindingData->imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
					bindingData->imageInfo.sampler     = foundTex->second.sampler;
					bindingData->imageInfo.imageView   = foundTex->second.imageView;
					bindingData->type                  = vk::DescriptorType::eCombinedImageSampler;

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
					bindingData.imageInfo.imageLayout = vk::ImageLayout::eGeneral;
					bindingData.imageInfo.imageView   = foundImgView->second;

					bindingData.type       = vk::DescriptorType::eStorageImage;
					bindingData.arrayIndex = uint32_t( le_cmd->info.array_index );

				} break;
#ifdef LE_FEATURE_RTX
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

					// fetch texture information based on texture id from command

					auto found_resource = frame.availableResources.find( le_cmd->info.tlas_id );
					if ( found_resource == frame.availableResources.end() ) {
						logger.error( "Could not find acceleration structure: '%s'. Ignoring top level acceleration structure binding command.", le_cmd->info.tlas_id->data->debug_name );
						break;
					}

					// ----------| invariant: image view has been found

					bindingData.accelerationStructureInfo.accelerationStructure = found_resource->second.as.tlas;
					bindingData.type                                            = vk::DescriptorType::eAccelerationStructureKHR;
					bindingData.arrayIndex                                      = uint32_t( le_cmd->info.array_index );

				} break;
#endif
				case le::CommandType::eBindIndexBuffer: {
					auto* le_cmd = static_cast<le::CommandBindIndexBuffer*>( dataIt );
					auto  buffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.buffer );
					cmd.bindIndexBuffer( buffer, le_cmd->info.offset, le_index_type_to_vk( le_cmd->info.indexType ) );
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
					vk::Buffer             vk_buffer    = frame_data_get_buffer_from_le_resource_id( frame, le_buffer );
					vertexInputBindings[ firstBinding ] = vk_buffer;

					for ( uint32_t b = 1; b != numBuffers; ++b ) {
						le_buf_resource_handle next_buffer = le_cmd->info.pBuffers[ b ];
						if ( next_buffer != le_buffer ) {
							le_buffer = next_buffer;
							vk_buffer = frame_data_get_buffer_from_le_resource_id( frame, le_buffer );
						}
						vertexInputBindings[ b + firstBinding ] = vk_buffer;
					}

					cmd.bindVertexBuffers( le_cmd->info.firstBinding, le_cmd->info.bindingCount, &vertexInputBindings[ firstBinding ], le_cmd->info.pOffsets );
				} break;

				case le::CommandType::eWriteToBuffer: {

					// Enqueue copy buffer command
					// TODO: we must sync this before the next read.
					auto* le_cmd = static_cast<le::CommandWriteToBuffer*>( dataIt );

					vk::BufferCopy region( le_cmd->info.src_offset, le_cmd->info.dst_offset, le_cmd->info.numBytes );

					auto srcBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.src_buffer_id );
					auto dstBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.dst_buffer_id );

					cmd.copyBuffer( srcBuffer, dstBuffer, 1, &region );

					break;
				}

				case le::CommandType::eWriteToImage: {

					// TODO: Use sync chain to sync
					// TODO: we can only write to linear images - we must find a way to make our image tiled

					auto* le_cmd = static_cast<le::CommandWriteToImage*>( dataIt );

					auto srcBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.src_buffer_id );
					auto dstImage  = frame_data_get_image_from_le_resource_id( frame, le_cmd->info.dst_image_id );

					// We define a range that covers all miplevels. this is useful as it allows us to transform
					// Image layouts in bulk, covering the full mip chain.
					vk::ImageSubresourceRange rangeAllRemainingMiplevels;
					rangeAllRemainingMiplevels
					    .setAspectMask( vk::ImageAspectFlagBits::eColor )
					    .setBaseMipLevel( le_cmd->info.dst_miplevel )
					    .setLevelCount( VK_REMAINING_MIP_LEVELS ) // we want all miplevels to be in transferDstOptimal.
					    .setBaseArrayLayer( le_cmd->info.dst_array_layer )
					    .setLayerCount( VK_REMAINING_ARRAY_LAYERS ); // we want the range to encompass all layers

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
						    { bufferTransferBarrier },          // buffer: host write -> transfer read
						    { imageLayoutToTransferDstOptimal } // image: prepare for transfer write
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
						    .setBaseArrayLayer( le_cmd->info.dst_array_layer )
						    .setLayerCount( 1 );

						vk::BufferImageCopy region;
						region
						    .setBufferOffset( 0 )                                       // buffer offset is 0, since staging buffer is a fresh, specially allocated buffer
						    .setBufferRowLength( 0 )                                    // 0 means tightly packed
						    .setBufferImageHeight( 0 )                                  // 0 means tightly packed
						    .setImageSubresource( std::move( imageSubresourceLayers ) ) // stored inline
						    .setImageOffset( { le_cmd->info.offset_x, le_cmd->info.offset_y, le_cmd->info.offset_z } )
						    .setImageExtent( { le_cmd->info.image_w, le_cmd->info.image_h, le_cmd->info.image_d } );

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
						    .setSubresourceRange( { vk::ImageAspectFlagBits::eColor, base_miplevel, 1, 0, 1 } );

						cmd.pipelineBarrier( vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, { prepareBlit } );

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

							vk::Offset3D offsetZero = { 0, 0, 0 };
							vk::Offset3D offsetSrc  = { srcImgWidth, srcImgHeight, 1 };
							vk::Offset3D offsetDst  = { dstImgWidth, dstImgHeight, 1 };
							region
							    .setSrcSubresource( { vk::ImageAspectFlagBits::eColor, srcMipLevel, 0, 1 } )
							    .setDstSubresource( { vk::ImageAspectFlagBits::eColor, dstMipLevel, 0, 1 } )
							    .setSrcOffsets( { offsetZero, offsetSrc } )
							    .setDstOffsets( { offsetZero, offsetDst } )
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

							cmd.pipelineBarrier( vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, { finishBlit } );

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
						    {},                                // buffers: nothing to do
						    { imageLayoutToShaderReadOptimal } // images: prepare for shader read
						);
					}

					break;
				}
#ifdef LE_FEATURE_RTX
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

						// Translate geometry info from internal format to vk::geometryKHR format.
						// We do this for each blas, which in turn may have an array of geometries.

						std::vector<vk::AccelerationStructureGeometryKHR> geometries;
						geometries.reserve( blas_info->geometries.size() );

						std::vector<vk::AccelerationStructureBuildRangeInfoKHR> build_ranges;
						build_ranges.reserve( blas_info->geometries.size() );

						for ( auto const& g : blas_info->geometries ) {

							// TODO: we may want to cache this - so that we don't have to lookup addresses more than once

							vk::Buffer vertex_buffer = frame_data_get_buffer_from_le_resource_id( frame, g.vertex_buffer );
							vk::Buffer index_buffer  = frame_data_get_buffer_from_le_resource_id( frame, g.index_buffer );

							vk::DeviceOrHostAddressConstKHR vertex_addr =
							    device.getBufferAddress( { vertex_buffer } ) + g.vertex_offset;

							vk::DeviceOrHostAddressConstKHR index_addr =
							    g.index_count
							        ? device.getBufferAddress( { index_buffer } ) + g.index_offset
							        : 0;

							vk::AccelerationStructureGeometryTrianglesDataKHR triangles_data{};
							triangles_data
							    .setVertexFormat( le_format_to_vk( g.vertex_format ) )
							    .setVertexData( vertex_addr )
							    .setMaxVertex( g.vertex_count - 1 ) // highest index of a vertex that will be accessed via build command
							    .setVertexStride( g.vertex_stride )
							    .setIndexType( le_index_type_to_vk( g.index_type ) )
							    .setIndexData( index_addr )
							    .setTransformData( {} ) // no transform data
							    ;

							vk::AccelerationStructureGeometryKHR geometry{};
							geometry
							    .setFlags( vk::GeometryFlagBitsKHR::eOpaque )
							    .setGeometryType( vk::GeometryTypeKHR::eTriangles )
							    .setGeometry( { triangles_data } );

							geometries.emplace_back( geometry );

							vk::AccelerationStructureBuildRangeInfoKHR build_range{};
							if ( g.index_count ) {
								// indexed geometry
								build_range
								    .setPrimitiveCount( g.index_count / 3 )
								    .setPrimitiveOffset( 0 )
								    .setFirstVertex( 0 )
								    .setTransformOffset( 0 );
							} else {
								// non-indexed geometry
								build_range.setPrimitiveCount( g.vertex_count / 3 )
								    .setPrimitiveOffset( 0 )
								    .setFirstVertex( 0 )
								    .setTransformOffset( 0 );
							}

							build_ranges.emplace_back( build_range );
						}

						vk::AccelerationStructureBuildRangeInfoKHR const* pBuildRangeInfos = build_ranges.data();

						vk::DeviceOrHostAddressKHR scratchData;
						//  We get the device address by querying from the buffer.
						scratchData = device.getBufferAddress( { scratchBuffer } );

						vk::AccelerationStructureBuildGeometryInfoKHR info;
						info
						    .setMode( vk::BuildAccelerationStructureModeKHR::eBuild )
						    .setType( vk::AccelerationStructureTypeKHR::eBottomLevel )
						    .setFlags( blas_info->flags )
						    .setSrcAccelerationStructure( nullptr )
						    .setDstAccelerationStructure( vk_acceleration_structure )
						    .setGeometryCount( uint32_t( geometries.size() ) )
						    .setGeometries( geometries )
						    .setScratchData( scratchData );

						cmd.buildAccelerationStructuresKHR( 1, &info, &pBuildRangeInfos );

						// Since the scratch buffer is reused across builds, we need a barrier to ensure one build
						// is finished before starting the next one

						vk::MemoryBarrier barrier(
						    vk::AccessFlagBits::eAccelerationStructureWriteKHR,                         // all writes must be visible ...
						    vk::AccessFlagBits::eAccelerationStructureReadKHR );                        // ... before the next read happens,
						cmd.pipelineBarrier( vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, // and the barrier is limited to the
						                     vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, // accelerationStructureBuild stage.
						                     vk::DependencyFlags(), { barrier }, {}, {} );

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

					vk::MemoryBarrier barrier( vk::AccessFlagBits::eTransferWrite,                   // All transfers must be visible ...
					                           vk::AccessFlagBits::eAccelerationStructureWriteKHR ); // ... before we can write to acceleration structures,

					cmd.pipelineBarrier( vk::PipelineStageFlagBits::eTransfer,                      // Writes from transfer ...
					                     vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR, // must be visible for accelerationStructureBuild stage.
					                     vk::DependencyFlags(), { barrier }, {}, {} );

					// instances information is encoded via buffer, but that buffer is also available as host memory,
					// because it is held in staging_buffer_mapped_memory...
					VkBuffer instanceBuffer = frame_data_get_buffer_from_le_resource_id( frame, le_cmd->info.staging_buffer_id );
					VkBuffer scratchBuffer  = frame_data_get_buffer_from_le_resource_id( frame, LE_RTX_SCRATCH_BUFFER_HANDLE );

					vk::DeviceOrHostAddressConstKHR instanceBufferDeviceAddress =
					    device.getBufferAddress( { instanceBuffer } ) + le_cmd->info.staging_buffer_offset;

					vk::AccelerationStructureGeometryKHR khr_instances_data;
					khr_instances_data.setGeometryType( vk::GeometryTypeKHR::eInstances );
					khr_instances_data.setFlags( vk::GeometryFlagBitsKHR::eOpaque );
					khr_instances_data.geometry           = vk::AccelerationStructureGeometryDataKHR{};
					khr_instances_data.geometry.instances = vk::AccelerationStructureGeometryInstancesDataKHR{};
					khr_instances_data.geometry.instances.setArrayOfPointers( false );
					khr_instances_data.geometry.instances.setData( instanceBufferDeviceAddress );

					// Take pointer to array of khr_instances - we will need one further indirection because reasons.

					//  we get the device address by querying from the buffer.
					vk::DeviceOrHostAddressKHR scratchData =
					    device.getBufferAddress( { scratchBuffer } );

					vk::AccelerationStructureBuildGeometryInfoKHR info{};
					info.setType( vk::AccelerationStructureTypeKHR::eTopLevel )
					    .setFlags( tlas_info->flags )
					    .setSrcAccelerationStructure( {} )
					    .setMode( vk::BuildAccelerationStructureModeKHR::eBuild )
					    .setDstAccelerationStructure( vk_acceleration_structure )
					    .setGeometries( khr_instances_data )
					    .setScratchData( scratchData );

					vk::AccelerationStructureBuildRangeInfoKHR buildOffsets{};
					buildOffsets
					    .setPrimitiveCount( tlas_info->instances_count ) // This is where we set the number of instances.
					    .setPrimitiveOffset( 0 )                         // spec states: must be a multiple of 16?!!
					    .setFirstVertex( 0 )
					    .setTransformOffset( 0 ) //
					    ;
					auto pBuildOffsets = &buildOffsets;

					cmd.buildAccelerationStructuresKHR( 1, &info, &pBuildOffsets );

					break;
				}
#endif // LE_FEATURE_RTX
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
		if ( pass.type == LE_RENDER_PASS_TYPE_DRAW && pass.renderPass ) {
			cmd.endRenderPass();
		}

		if ( should_insert_debug_labels ) {
			cmd.endDebugUtilsLabelEXT();
		}

		cmd.end();
	}

	// place command buffer in frame store so that it can be submitted.
	for ( auto&& c : cmdBufs ) {
		frame.commandBuffers.emplace_back( c );
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
    const LeShaderStageEnum&          moduleType,
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

static bool backend_dispatch_frame( le_backend_o* self, size_t frameIndex ) {

	auto& frame = self->mFrames[ frameIndex ];

	std::vector<::vk::PipelineStageFlags> wait_dst_stage_mask(
	    frame.swapchain_state.size(), { vk::PipelineStageFlagBits::eColorAttachmentOutput } );

	std::vector<vk::Semaphore> present_complete_semaphores;
	present_complete_semaphores.reserve( frame.swapchain_state.size() );

	std::vector<vk::Semaphore> render_complete_semaphores;
	render_complete_semaphores.reserve( frame.swapchain_state.size() );

	for ( auto const& swp : frame.swapchain_state ) {
		present_complete_semaphores.push_back( swp.presentComplete );
		render_complete_semaphores.push_back( swp.renderComplete );
	}

	vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount( uint32_t( present_complete_semaphores.size() ) )
	    .setPWaitSemaphores( present_complete_semaphores.data() )
	    .setPWaitDstStageMask( wait_dst_stage_mask.data() )
	    .setCommandBufferCount( uint32_t( frame.commandBuffers.size() ) )
	    .setPCommandBuffers( frame.commandBuffers.data() )
	    .setSignalSemaphoreCount( uint32( render_complete_semaphores.size() ) )
	    .setPSignalSemaphores( render_complete_semaphores.data() );

	auto queue = vk::Queue{ self->device->getDefaultGraphicsQueue() };

	queue.submit( { submitInfo }, frame.frameFence );

	using namespace le_swapchain_vk;

	bool overall_result = true;

	for ( size_t i = 0; i != self->swapchains.size(); i++ ) {

		bool result =
		    swapchain_i.present(
		        self->swapchains[ i ],
		        self->device->getDefaultGraphicsQueue(),
		        render_complete_semaphores[ i ],
		        &frame.swapchain_state[ i ].image_idx );

		frame.swapchain_state[ i ].present_successful = result;

		overall_result &= result;
	}

	return overall_result;
}

// ----------------------------------------------------------------------

le_rtx_blas_info_handle backend_create_rtx_blas_info( le_backend_o* self, le_rtx_geometry_t const* geometries, uint32_t geometries_count, LeBuildAccelerationStructureFlags const* flags ) {

	auto* blas_info = new le_rtx_blas_info_o{};

	// Copy geometry information
	blas_info->geometries.insert( blas_info->geometries.end(), geometries, geometries + geometries_count );

	// Store requested flags, but if no build flags requested, at least set the
	// allowUpdate flag so that primitive geometry may be updated.
	blas_info->flags = flags ? le_build_acceleration_structure_flags_to_vk( *flags ) : vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

	// Add to backend's kill list so that all infos associated to handles get cleaned up at the end.
	self->rtx_blas_info_kill_list.add_element( blas_info );

	return reinterpret_cast<le_rtx_blas_info_handle>( blas_info );
};

// ----------------------------------------------------------------------

le_rtx_tlas_info_handle backend_create_rtx_tlas_info( le_backend_o* self, uint32_t instances_count, LeBuildAccelerationStructureFlags const* flags ) {

	auto* tlas_info = new le_rtx_tlas_info_o{};

	// Copy geometry information
	tlas_info->instances_count = instances_count;

	// Store requested flags, but if no build flags requested, at least set the
	// allowUpdate flag so that instance information such as transforms may be set.
	tlas_info->flags =
	    flags
	        ? le_build_acceleration_structure_flags_to_vk( *flags )
	        : vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

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

	vk_backend_i.create                     = backend_create;
	vk_backend_i.destroy                    = backend_destroy;
	vk_backend_i.setup                      = backend_setup;
	vk_backend_i.get_num_swapchain_images   = backend_get_num_swapchain_images;
	vk_backend_i.reset_swapchain            = backend_reset_swapchain;
	vk_backend_i.reset_failed_swapchains    = backend_reset_failed_swapchains;
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
	vk_backend_i.get_swapchain_count    = backend_get_swapchain_count;
	vk_backend_i.get_swapchain_info     = backend_get_swapchain_info;

	vk_backend_i.create_rtx_blas_info = backend_create_rtx_blas_info;
	vk_backend_i.create_rtx_tlas_info = backend_create_rtx_tlas_info;

	auto& private_backend_i                  = api_i->private_backend_vk_i;
	private_backend_i.get_vk_device          = backend_get_vk_device;
	private_backend_i.get_vk_physical_device = backend_get_vk_physical_device;
	private_backend_i.get_le_device          = backend_get_le_device;
	private_backend_i.get_instance           = backend_get_instance;
	private_backend_i.allocate_image         = backend_allocate_image;
	private_backend_i.destroy_image          = backend_destroy_image;
	private_backend_i.allocate_buffer        = backend_allocate_buffer;
	private_backend_i.destroy_buffer         = backend_destroy_buffer;

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

#ifdef PLUGINS_DYNAMIC
	le_core_load_library_persistently( "libvulkan.so" );
#endif
}
