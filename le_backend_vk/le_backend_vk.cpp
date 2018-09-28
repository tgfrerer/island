#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h"

#include "le_backend_vk/util/spooky/SpookyV2.h" // for hashing pso state

#define VMA_USE_STL_CONTAINERS 1
#include "util/vk_mem_alloc/vk_mem_alloc.h" // for allocation

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include "le_swapchain_vk/le_swapchain_vk.h"

#include "pal_window/pal_window.h"

#include "pal_file_watcher/pal_file_watcher.h" // for watching shader source files

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_shader_compiler/le_shader_compiler.h"

#include "experimental/filesystem" // for parsing shader source file paths
#include <fstream>                 // for reading shader source files
#include <cstring>                 // for memcpy

#include <vector>
#include <unordered_map>
#include <forward_list>
#include <iostream>
#include <iomanip>
#include <list>
#include <set>
#include <atomic>

#include <memory>

#include "util/spirv-cross/spirv_cross.hpp"

#ifndef PRINT_DEBUG_MESSAGES
#	define PRINT_DEBUG_MESSAGES false
#endif

namespace std {
using namespace experimental;
}

// clang-format off
// FIXME: microsoft places members in bitfields low to high (LSB first)
// and gcc appears to do the same - sorting is based on this assumption, and we must somehow test for it when compiling.
struct le_shader_binding_info {
	union {
		struct{
		uint64_t dynamic_offset_idx :  8; // only used when binding pipeline
		uint64_t stage_bits         :  6; // vkShaderFlags : which stages this binding is used for (must be at least 6 bits wide)
		uint64_t range              : 27; // only used for ubos (sizeof ubo)
		uint64_t type               :  4; // vkDescriptorType descriptor type
		uint64_t count              :  8; // number of elements
		uint64_t binding            :  8; // |\                           : binding index within set
		uint64_t setIndex           :  3; // |/ keep together for sorting : set index [0..7]
		};
		uint64_t data;
	};

	uint64_t name_hash; // const_char_hash of parameter name as given in shader

	bool operator < ( le_shader_binding_info const & lhs ){
		return data < lhs.data;
	}
};
// clang-format on

struct le_descriptor_set_layout_t {
	std::vector<le_shader_binding_info> binding_info;                  // binding info for this set
	vk::DescriptorSetLayout             vk_descriptor_set_layout;      // vk object
	vk::DescriptorUpdateTemplate        vk_descriptor_update_template; // template used to update such a descriptorset based on descriptor data laid out in flat DescriptorData elements
};

struct le_pipeline_layout_info {
	uint64_t pipeline_layout_key  = 0;  // handle to pipeline layout
	uint64_t set_layout_keys[ 8 ] = {}; // maximum number of DescriptorSets is 8
	uint64_t set_layout_count     = 0;  // number of actually used DescriptorSetLayouts for this layout
};

struct le_pipeline_and_layout_info_t {
	vk::Pipeline            pipeline;
	le_pipeline_layout_info layout_info;
};

// Everything a possible vulkan descriptor binding might contain.
// Type of descriptor decides which values will be used.

struct DescriptorData {
	// NOTE: explore use of union of DescriptorImageInfo/DescriptorBufferInfo to tighten this up/simplify
	vk::Sampler        sampler       = nullptr;                                   // |
	vk::ImageView      imageView     = nullptr;                                   // | > keep in this order, so we can pass address for sampler as descriptorImageInfo
	vk::ImageLayout    imageLayout   = vk::ImageLayout::eShaderReadOnlyOptimal;   // |
	vk::DescriptorType type          = vk::DescriptorType::eUniformBufferDynamic; //
	vk::Buffer         buffer        = nullptr;                                   // |
	vk::DeviceSize     offset        = 0;                                         // | > keep in this order, as we can cast this to a DescriptorBufferInfo
	vk::DeviceSize     range         = VK_WHOLE_SIZE;                             // |
	uint32_t           bindingNumber = 0;                                         // <-- may be sparse, may repeat (for arrays of images bound to the same binding), but must increase monotonically (may only repeat or up over the series inside the samplerBindings vector).
	uint32_t           arrayIndex    = 0;                                         // <-- must be in sequence for array elements of same binding
};

struct le_shader_module_o {
	uint64_t                                         hash                = 0;     ///< hash taken from spirv code + filepath hash
	uint64_t                                         hash_file_path      = 0;     ///< hash taken from filepath (canonical)
	uint64_t                                         hash_pipelinelayout = 0;     ///< hash taken from descriptors over all sets
	std::vector<le_shader_binding_info>              bindings;                    ///< info for each binding, sorted asc.
	std::vector<uint32_t>                            spirv    = {};               ///< spirv source code for this module
	std::filesystem::path                            filepath = {};               ///< path to source file
	std::vector<std::string>                         vertexAttributeNames;        ///< (used for debug only) name for vertex attribute
	std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions; ///< descriptions gathered from reflection if shader type is vertex
	std::vector<vk::VertexInputBindingDescription>   vertexBindingDescriptions;   ///< descriptions gathered from reflection if shader type is vertex
	vk::ShaderModule                                 module = nullptr;
	LeShaderType                                     stage  = LeShaderType::eNone;
};

struct le_graphics_pipeline_state_o {

	uint64_t hash = 0; ///< hash of pipeline state, but not including shader modules

	le_shader_module_o *shaderModuleVert = nullptr;
	le_shader_module_o *shaderModuleFrag = nullptr;

	// TODO (pipeline) : -- add fields to pso object
	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{}; // TODO: this needs to be hashed.

	bool useExplicitVertexInputDescriptions = false;

	std::vector<vk::VertexInputBindingDescription>   explicitVertexBindingDescriptions;   // only used if explicitly told to, otherwise use from vertex shader reflection
	std::vector<vk::VertexInputAttributeDescription> explicitVertexAttributeDescriptions; // only used if explicitly told to, otherwise use from vertex shader reflection
};

struct AbstractPhysicalResource {
	enum Type : uint64_t {
		eUndefined = 0,
		eBuffer,
		eImage,
		eImageView,
		eSampler,
		eFramebuffer,
		eRenderPass,
	};
	union {
		uint64_t      asRawData;
		VkBuffer      asBuffer;
		VkImage       asImage;
		VkImageView   asImageView;
		VkSampler     asSampler;
		VkFramebuffer asFramebuffer;
		VkRenderPass  asRenderPass;
	};
	Type type;
};

/// \brief re-interpretation of resource handle type
/// \note we assume little-endian machine
struct LeResourceHandleMeta {
	enum FlagBits : uint8_t {
		eIsVirtual = 1u << 0,
	};

	union {
		struct {
			uint32_t id; // unique identifier
			struct {
				LeResourceType type;  // tells us type of resource
				uint8_t        flags; // composed of FlagBits
				uint8_t        index; // used for virtual buffers to refer to corresponding le_allocator index
				uint8_t        padding;
			};
		};
		uint64_t data;
	};
};

struct AttachmentInfo {
	LeResourceHandle      resource_id = nullptr; ///< which resource to look up for resource state
	vk::Format            format;
	vk::AttachmentLoadOp  loadOp;
	vk::AttachmentStoreOp storeOp;
	vk::ClearValue        clearValue;         ///< either color or depth clear value, only used if loadOp is eClear
	uint16_t              initialStateOffset; ///< state of resource before entering the renderpass
	uint16_t              finalStateOffset;   ///< state of resource after exiting the renderpass
};

struct Pass {

	AttachmentInfo attachments[ 16 ]; // maximum of 16 color output attachments
	uint16_t       numColorAttachments;
	uint16_t       numDepthStencilAttachments;

	LeRenderPassType type;

	vk::Framebuffer framebuffer;
	vk::RenderPass  renderPass;
	uint32_t        width;
	uint32_t        height;
	uint64_t        renderpassHash; ///< spooky hash of elements that could influence renderpass compatibility

	struct le_command_buffer_encoder_o *encoder;
};

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
};

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

	// ResourceState keeps track of the resource stage *before* a barrier
	struct ResourceState {
		vk::AccessFlags        visible_access; // which memory access must be be visible - if any of these are WRITE accesses, these must be made available(flushed) before next access
		vk::PipelineStageFlags write_stage;    // current or last stage at which write occurs
		vk::ImageLayout        layout;         // current layout (for images)
	};

	struct Texture {
		vk::Sampler   sampler;
		vk::ImageView imageView;
	};

	std::unordered_map<LeResourceHandle, Texture> textures; // non-owning, references to frame-local textures, cleared on frame fence.

	// With `syncChainTable` and image_attachment_info_o.syncState, we should
	// be able to create renderpasses. Each resource has a sync chain, and each attachment_info
	// has a struct which holds indices into the sync chain telling us where to look
	// up the sync state for a resource at different stages of renderpass construction.
	std::unordered_map<LeResourceHandle, std::vector<ResourceState>> syncChainTable;

	static_assert( sizeof( VkBuffer ) == sizeof( VkImageView ) && sizeof( VkBuffer ) == sizeof( VkImage ), "size of AbstractPhysicalResource components must be identical" );

	// Todo: clarify ownership of physical resources inside FrameData
	// Q: Does this table actually own the resources?
	// A: It must not: as it is used to map external resources as well.
	std::unordered_map<LeResourceHandle, AbstractPhysicalResource> physicalResources; // map from renderer resource id to physical resources - only contains resources this frame uses.

	/// \brief vk resources retained and destroyed with BackendFrameData
	std::forward_list<AbstractPhysicalResource> ownedResources;

	std::vector<Pass>               passes;
	std::vector<vk::DescriptorPool> descriptorPools; // one descriptor pool per pass

	/*

	  Each Frame has one allocation Pool from which all allocations for scratch buffers are drawn.

	  When creating encoders, each encoder has their own sub-allocator, each sub-allocator owns an
	  independent block of memory allcated from the frame pool. This way, encoders can work in their
	  own thread.

	 */

	std::unordered_map<LeResourceHandle, AllocatedResource> availableResources; // resources this frame may use
	std::unordered_map<LeResourceHandle, AllocatedResource> binnedResources;    // resources to delete when this frame comes round to clear()

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

	std::vector<le_shader_module_o *>                               shaderModules;         // OWNING. Stores all shader modules used in backend.
	std::unordered_map<std::string, std::set<le_shader_module_o *>> moduleDependencies;    // map 'canonical shader source file path' -> [shader modules]
	std::set<le_shader_module_o *>                                  modifiedShaderModules; // non-owning pointers to shader modules which need recompiling (used by file watcher)

	le_shader_compiler_o *shader_compiler   = nullptr;
	pal_file_watcher_o *  shaderFileWatcher = nullptr;

	// These resources are potentially in-flight, and may be used read-only
	// by more than one frame.
	std::vector<le_graphics_pipeline_state_o *>              PSOs;
	vk::PipelineCache                                        debugPipelineCache = nullptr;
	std::unordered_map<uint64_t, vk::Pipeline>               pipelineCache;
	std::unordered_map<uint64_t, le_pipeline_layout_info>    pipelineLayoutInfoCache;
	std::unordered_map<uint64_t, le_descriptor_set_layout_t> descriptorSetLayoutCache; // indexed by le_shader_bindings_info[] hash
	std::unordered_map<uint64_t, vk::PipelineLayout>         pipelineLayoutCache;      // indexed by hash of array of descriptorSetLayoutCache keys per pipeline layout

	VmaAllocator mAllocator = nullptr;

	uint32_t queueFamilyIndexGraphics = 0; // set during setup
	uint32_t queueFamilyIndexCompute  = 0; // set during setup

	std::atomic<uint32_t> resource_counter;                // we use this to make sure that each resource created will have an unique id.
	LeResourceHandle      backBufferImageHandle = nullptr; // opaque handle identifying the backbuffer image, initialised in setup()

	struct {
		std::unordered_map<LeResourceHandle, AllocatedResource> allocatedResources; // allocated resources, indexed by resource name hash
	} only_backend_allocate_resources_may_access;                                   // only acquire_physical_resources may read/write

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
}

// ----------------------------------------------------------------------

static inline const vk::ClearValue &le_to_vk( const LeClearValue &lhs ) {
	static_assert( sizeof( vk::ClearValue ) == sizeof( LeClearValue ), "clear value must be of equal size" );
	return reinterpret_cast<const vk::ClearValue &>( lhs );
}

// ----------------------------------------------------------------------
template <typename T>
static constexpr typename std::underlying_type<T>::type enumToNum( const T &enumVal ) {
	return static_cast<typename std::underlying_type<T>::type>( enumVal );
};

// ----------------------------------------------------------------------

static inline bool is_depth_stencil_format( vk::Format format_ ) {
	return ( format_ >= vk::Format::eD16Unorm && format_ <= vk::Format::eD32SfloatS8Uint );
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path &file_path, bool *success ) {

	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		std::cerr << "Unable to open file: " << std::filesystem::canonical( file_path ) << std::endl
		          << std::flush;
		*success = false;
		return contents;
	}

	//	std::cout << "OK Opened file:" << std::filesystem::canonical( file_path ) << std::endl
	//	          << std::flush;

	// ----------| invariant: file is open

	auto endOfFilePos = file.tellg();

	if ( endOfFilePos > 0 ) {
		fileSize = size_t( endOfFilePos );
	} else {
		*success = false;
		return contents;
	}

	// ----------| invariant: file has some bytes to read
	contents.resize( fileSize );

	file.seekg( 0, std::ios::beg );
	file.read( contents.data(), endOfFilePos );
	file.close();

	*success = true;
	return contents;
}

// ----------------------------------------------------------------------

static bool check_is_data_spirv( const void *raw_data, size_t data_size ) {

	struct SpirVHeader {
		uint32_t magic; // Spirv magic number
		union {         // Spir-V version number, bytes (high to low): [0x00, major, minor, 0x00]
			struct {
				uint8_t padding_low;
				uint8_t version_minor;
				uint8_t version_major;
				uint8_t padding_hi;
			};
			uint32_t version_number;
		};
		uint32_t gen_magic; // Generator magic number
		uint32_t bound;     //
		uint32_t reserved;  // Reserved
	} file_header;

	if ( data_size < sizeof( SpirVHeader ) ) {
		// Ahem, file not even contains a file header, what were you thinking?
		return false;
	}

	// ----------| invariant: file contains enough bytes for a valid file header

	static const uint32_t SPIRV_MAGIC = 0x07230203; // magic number for spir-v files, see: <https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html#_a_id_physicallayout_a_physical_layout_of_a_spir_v_module_and_instruction>

	memcpy( &file_header, raw_data, sizeof( file_header ) );

	if ( file_header.magic == SPIRV_MAGIC ) {
		return true;
	} else {
		// invalid file header for spir-v file
		//		std::cerr << "ERROR: Invalid header for SPIR-V file detected." << std::endl
		//		          << std::flush;
		return false;
	}
}

// ----------------------------------------------------------------------

/// \brief translate a binary blob into spirv code if possible
/// \details Blob may be raw spirv data, or glsl data
static void backend_translate_to_spirv_code( le_backend_o *self, void *raw_data, size_t numBytes, LeShaderType moduleType, const char *original_file_name, std::vector<uint32_t> &spirvCode, std::set<std::string> &includesSet ) {

	if ( check_is_data_spirv( raw_data, numBytes ) ) {
		spirvCode.resize( numBytes / 4 );
		memcpy( spirvCode.data(), raw_data, numBytes );
	} else {
		// Data is not spirv - is it glsl, perhaps?

		using namespace le_shader_compiler; // for compiler_i

		auto compileResult = compiler_i.compile_source( self->shader_compiler, static_cast<const char *>( raw_data ), numBytes, moduleType, original_file_name );

		if ( compiler_i.get_result_success( compileResult ) == true ) {
			const char *addr;
			size_t      res_sz;
			compiler_i.get_result_bytes( compileResult, &addr, &res_sz );
			spirvCode.resize( res_sz / 4 );
			memcpy( spirvCode.data(), addr, res_sz );

			// -- grab a list of includes which this compilation unit depends on:

			const char *pStr;
			size_t      strSz = 0;

			while ( compiler_i.get_result_includes( compileResult, &pStr, &strSz ) ) {
				// -- update set of includes for this module
				includesSet.emplace( pStr, strSz );
			}
		}

		// release compile result object
		compiler_i.release_result( compileResult );
	}
}

// flags all modules which are affected by a change in shader_source_file_path,
// and adds them to a set of shader modules wich need to be recompiled.
static void backend_flag_affected_modules_for_source_path( le_backend_o *self, const char *shader_source_file_path ) {
	// find all modules from dependencies set
	// insert into list of modified modules.

	if ( 0 == self->moduleDependencies.count( shader_source_file_path ) ) {
		// -- no matching dependencies.
		std::cout << "Shader code update detected, but no modules using shader source file: " << shader_source_file_path << std::endl
		          << std::flush;
		return;
	}

	// ---------| invariant: at least one module depends on this shader source file.

	auto const &moduleDependencies = self->moduleDependencies[ shader_source_file_path ];

	// -- add all affected modules to the set of modules which depend on this shader source file.

	for ( auto const &m : moduleDependencies ) {
		self->modifiedShaderModules.insert( m );
	}
};

// ----------------------------------------------------------------------

static void backend_set_module_dependencies_for_watched_file( le_backend_o *self, le_shader_module_o *module, std::set<std::string> &sourcePaths ) {

	// To be able to tell quick which modules need to be recompiled if a source file changes,
	// we build a table from source file -> array of modules

	for ( const auto &s : sourcePaths ) {

		// if no previous entry for this source path existed, we must insert a watch for this path
		// the watch will call a backend method which figures out how many modules were affected.
		if ( 0 == self->moduleDependencies.count( s ) ) {
			// this is the first time this file appears on our radar. Let's create a file watcher for it.
			static auto &file_watcher_i = *Registry::getApi<pal_file_watcher_i>();

			pal_file_watcher_watch_settings settings;
			settings.filePath           = s.c_str();
			settings.callback_user_data = self;
			settings.callback_fun       = []( const char *path, void *user_data ) -> bool {
				auto backend = static_cast<le_backend_o *>( user_data );
				// call a method on backend to tell it that the file path has changed.
				// backend to figure out which modules are affected.
				backend_flag_affected_modules_for_source_path( backend, path );
				return true;
			};
			file_watcher_i.add_watch( self->shaderFileWatcher, settings );
		}

		std::cout << std::hex << module << " : " << s << std::endl
		          << std::flush;

		self->moduleDependencies[ s ].insert( module );
	}
}

// ----------------------------------------------------------------------
/// \returns stride (in Bytes) for a given spirv type object
static uint32_t spirv_type_get_stride( const spirv_cross::SPIRType &spir_type ) {
	// NOTE: spir_type.width is given in bits
	return ( spir_type.width / 8 ) * spir_type.vecsize * spir_type.columns;
}

// clang-format off
// ----------------------------------------------------------------------
/// \returns corresponding vk::Format for a given spirv type object
static vk::Format spirv_type_get_vk_format( const spirv_cross::SPIRType &spirv_type ) {

	if ( spirv_type.columns != 1 ){
		assert(false); // columns must be 1 for a vkFormat
		return vk::Format::eUndefined;
	}

	// ----------| invariant: columns == 1

	switch ( spirv_type.basetype ) {
	case spirv_cross::SPIRType::Float:
		switch ( spirv_type.vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Sfloat;
		case 3: return vk::Format::eR32G32B32Sfloat;
		case 2: return vk::Format::eR32G32Sfloat;
		case 1: return vk::Format::eR32Sfloat;
		}
	    break;
	case spirv_cross::SPIRType::Half:
		switch ( spirv_type.vecsize ) {
		case 4: return vk::Format::eR16G16B16A16Sfloat;
		case 3: return vk::Format::eR16G16B16Sfloat;
		case 2: return vk::Format::eR16G16Sfloat;
		case 1: return vk::Format::eR16Sfloat;
		}
	    break;
	case spirv_cross::SPIRType::Int:
		switch ( spirv_type.vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Sint;
		case 3: return vk::Format::eR32G32B32Sint;
		case 2: return vk::Format::eR32G32Sint;
		case 1: return vk::Format::eR32Sint;
		}
	    break;
	case spirv_cross::SPIRType::UInt:
		switch ( spirv_type.vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Uint;
		case 3: return vk::Format::eR32G32B32Uint;
		case 2: return vk::Format::eR32G32Uint;
		case 1: return vk::Format::eR32Uint;
		}
	    break;
	case spirv_cross::SPIRType::Char:
		switch ( spirv_type.vecsize ) {
		case 4: return vk::Format::eR8G8B8A8Unorm;
		case 3: return vk::Format::eR8G8B8Unorm;
		case 2: return vk::Format::eR8G8Unorm;
		case 1: return vk::Format::eR8Unorm;
		}
	    break;
	default:
		assert(false); // format not covered by switch case.
	break;
	}

	assert(false); // something went wrong.
	return vk::Format::eUndefined;
}
// clang-format on

// ----------------------------------------------------------------------

static void shader_module_update_reflection( le_shader_module_o *module ) {

	std::vector<le_shader_binding_info>              bindings;                    // <- gets stored in module at end
	std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions; // <- gets stored in module at end
	std::vector<vk::VertexInputBindingDescription>   vertexBindingDescriptions;   // <- gets stored in module at end
	std::vector<std::string>                         vertexAttributeNames;        // <- gets stored in module at end

	static_assert( sizeof( le_shader_binding_info ) == sizeof( uint64_t ) * 2, "Shader binding info must be the same size as 2 * uint64_t" );

	spirv_cross::Compiler compiler( module->spirv );

	// The SPIR-V is now parsed, and we can perform reflection on it.
	spirv_cross::ShaderResources resources = compiler.get_shader_resources();

	{ // -- find out max number of bindings
		size_t bindingsCount = resources.uniform_buffers.size() +
		                       resources.storage_buffers.size() +
		                       resources.storage_images.size() +
		                       resources.sampled_images.size();

		bindings.reserve( bindingsCount );
	}

	// If this shader module represents a vertex shader, get
	// stage_inputs, as these represent vertex shader inputs.
	if ( module->stage == LeShaderType::eVert ) {

		uint32_t location = 0; // shader location qualifier mapped to binding number

		// NOTE:  resources.stage_inputs means inputs to this shader stage
		//		  resources.stage_outputs means outputs from this shader stage.
		vertexAttributeDescriptions.reserve( resources.stage_inputs.size() );
		vertexBindingDescriptions.reserve( resources.stage_inputs.size() );
		vertexAttributeNames.reserve( resources.stage_inputs.size() );

		// NOTE: we assume that stage_inputs are ordered ASC by location
		for ( auto const &stageInput : resources.stage_inputs ) {

			if ( compiler.get_decoration_bitset( stageInput.id ).get( spv::DecorationLocation ) ) {
				location = compiler.get_decoration( stageInput.id, spv::DecorationLocation );
			}

			auto const &attributeType = compiler.get_type( stageInput.type_id );

			// We create one binding description for each attribute description,
			// which means that vertex input is assumed to be not interleaved.
			//
			// User may override reflection-generated vertex input by explicitly
			// specifying vertex input when creating pipeline.

			vk::VertexInputAttributeDescription inputAttributeDescription{};
			vk::VertexInputBindingDescription   vertexBindingDescription{};

			inputAttributeDescription
			    .setLocation( location )                                // by default, we assume one buffer per location
			    .setBinding( location )                                 // by default, we assume one buffer per location
			    .setFormat( spirv_type_get_vk_format( attributeType ) ) // best guess, derived from spirv_type
			    .setOffset( 0 );                                        // non-interleaved means offset must be 0

			vertexBindingDescription
			    .setBinding( location )
			    .setInputRate( vk::VertexInputRate::eVertex )
			    .setStride( spirv_type_get_stride( attributeType ) );

			vertexAttributeDescriptions.emplace_back( std::move( inputAttributeDescription ) );
			vertexBindingDescriptions.emplace_back( std::move( vertexBindingDescription ) );
			vertexAttributeNames.emplace_back( stageInput.name );

			++location;
		}

		// store vertex input info with module

		module->vertexAttributeDescriptions = std::move( vertexAttributeDescriptions );
		module->vertexBindingDescriptions   = std::move( vertexBindingDescriptions );
		module->vertexAttributeNames        = std::move( vertexAttributeNames );
	}

	// -- Get all sampled images in the shader
	for ( auto const &resource : resources.sampled_images ) {
		// TODO: how do we deal with arrays of images?
		// it is well possible that spirv cross reports each image individually, giving it an index.
		le_shader_binding_info info{};

		info.setIndex   = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
		info.binding    = compiler.get_decoration( resource.id, spv::DecorationBinding );
		info.type       = enumToNum( vk::DescriptorType::eCombinedImageSampler ); // Note: sampled_images corresponds to combinedImageSampler, separate_[image|sampler] corresponds to image, and sampler being separate
		info.stage_bits = enumToNum( module->stage );
		info.count      = 1;
		info.name_hash  = hash_64_fnv1a( resource.name.c_str() );

		bindings.emplace_back( std::move( info ) );
	}

	// -- Get all uniform buffers in shader
	for ( auto const &resource : resources.uniform_buffers ) {
		le_shader_binding_info info{};

		info.setIndex   = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
		info.binding    = compiler.get_decoration( resource.id, spv::DecorationBinding );
		info.type       = enumToNum( vk::DescriptorType::eUniformBufferDynamic );
		info.count      = 1;
		info.stage_bits = enumToNum( module->stage );
		info.name_hash  = hash_64_fnv1a( resource.name.c_str() );
		info.range      = compiler.get_declared_struct_size( compiler.get_type( resource.type_id ) );

		bindings.emplace_back( std::move( info ) );
	}

	// -- Get all storage buffers in shader
	for ( auto &resource : resources.storage_buffers ) {
		le_shader_binding_info info{};

		info.setIndex   = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
		info.binding    = compiler.get_decoration( resource.id, spv::DecorationBinding );
		info.type       = enumToNum( vk::DescriptorType::eStorageBufferDynamic );
		info.count      = 1;
		info.stage_bits = enumToNum( module->stage );

		bindings.emplace_back( std::move( info ) );
	}

	// Sort bindings - this makes it easier for us to link shader stages together
	std::sort( bindings.begin(), bindings.end() ); // we're sorting shader bindings by set, binding ASC

	// -- calculate hash over bindings
	module->hash_pipelinelayout = SpookyHash::Hash64( bindings.data(), sizeof( le_shader_binding_info ) * bindings.size(), 0 );

	// -- store bindings with module
	module->bindings = std::move( bindings );
}

// ----------------------------------------------------------------------
/// \brief create vulkan shader module based on file path
static le_shader_module_o *backend_create_shader_module( le_backend_o *self, char const *path, LeShaderType moduleType ) {

	// This method gets called through the renderer - it is assumed during the setup stage.

	bool loadSuccessful = false;
	auto raw_file_data  = load_file( path, &loadSuccessful ); // returns a raw byte vector

	if ( !loadSuccessful ) {
		return nullptr;
	}

	// ---------| invariant: load was successful

	// We use the canonical path to store a fingerprint of the file
	auto     canonical_path_as_string = std::filesystem::canonical( path ).string();
	uint64_t file_path_hash           = SpookyHash::Hash64( canonical_path_as_string.data(), canonical_path_as_string.size(), 0x0 );

	// -- Make sure the file contains spir-v code.

	std::vector<uint32_t> spirv_code;
	std::set<std::string> includesSet = {{canonical_path_as_string}}; // let first element be the source file path

	backend_translate_to_spirv_code( self, raw_file_data.data(), raw_file_data.size(), moduleType, path, spirv_code, includesSet );

	// FIXME: we need to check spirv code is ok, that compilation succeeded.

	le_shader_module_o *module = new le_shader_module_o{};

	module->stage          = moduleType;
	module->filepath       = canonical_path_as_string;
	module->hash_file_path = file_path_hash;
	module->hash           = SpookyHash::Hash64( spirv_code.data(), spirv_code.size() * sizeof( uint32_t ), module->hash_file_path );

	{
		// -- Check if module is already present in render module cache.

		auto found_module = std::find_if( self->shaderModules.begin(), self->shaderModules.end(), [module]( const le_shader_module_o *m ) -> bool {
			return module->hash == m->hash;
		} );

		// -- If module found in cache, return cached module, discard local module

		if ( found_module != self->shaderModules.end() ) {
			delete module;
			return *found_module;
		}
	}

	// ---------| invariant: no previous module with this hash exists

	module->spirv = std::move( spirv_code );

	{
		// -- create vulkan shader object
		vk::Device device = self->device->getVkDevice();
		// flags must be 0 (reserved for future use), size is given in bytes
		vk::ShaderModuleCreateInfo createInfo( vk::ShaderModuleCreateFlags(), module->spirv.size() * sizeof( uint32_t ), module->spirv.data() );

		module->module = device.createShaderModule( createInfo );
	}

	shader_module_update_reflection( module );

	// -- retain module in renderer
	self->shaderModules.push_back( module );

	// -- add all source files for this file to the list of watched
	//    files that point back to this module
	backend_set_module_dependencies_for_watched_file( self, module, includesSet );

	return module;
}

// ----------------------------------------------------------------------

static void backend_shader_module_update( le_backend_o *self, le_shader_module_o *module ) {

	// Shader module needs updating if shader code has changed.
	// if this happens, a new vulkan object for the module must be crated.

	// The module must be locked for this, as we need exclusive access just in case the module is
	// in use by the frame recording thread, which may want to create pipelines.
	//
	// Vulkan Lifetimes require us only to keep module alive for as long as a pipeline is being
	// generated from it. This means we "only" need to protect against any threads which might be
	// creating pipelines.

	// -- get module spirv code
	bool loadSuccessful = false;
	auto source_text    = load_file( module->filepath, &loadSuccessful );

	if ( !loadSuccessful ) {
		// file could not be loaded. bail out.
		return;
	}

	std::vector<uint32_t> spirv_code;
	std::set<std::string> includesSet;

	backend_translate_to_spirv_code( self, source_text.data(), source_text.size(), module->stage, module->filepath.c_str(), spirv_code, includesSet );

	if ( spirv_code.empty() ) {
		// no spirv code available, bail out.
		return;
	}

	// -- check spirv code hash against module spirv hash
	uint64_t hash_of_module = SpookyHash::Hash64( spirv_code.data(), spirv_code.size() * sizeof( uint32_t ), module->hash_file_path );

	if ( hash_of_module == module->hash ) {
		// spirv code identical, no update needed, bail out.
		return;
	}

	// -- update module hash
	module->hash = hash_of_module;

	// -- update additional include paths, if necessary.
	backend_set_module_dependencies_for_watched_file( self, module, includesSet );

	// ---------| Invariant: new spir-v code detected.

	// -- if hash doesn't match, delete old vk module, create new vk module
	vk::Device device = self->device->getVkDevice();

	// -- store new spir-v code
	module->spirv = std::move( spirv_code );

	// -- update bindings via spirv-cross, and update bindings hash
	shader_module_update_reflection( module );

	// -- delete old vulkan shader module object
	// Q: Should we rather defer deletion? In case that this module is in use?
	// A: Not really - according to spec module must only be alife while pipeline is being compiled.
	//    If we can guarantee that no other process is using this module at the moment to compile a
	//    Pipeline, we can safely delete it.
	device.destroyShaderModule( module->module );
	module->module = nullptr;

	// -- create new vulkan shader module object
	vk::ShaderModuleCreateInfo createInfo( vk::ShaderModuleCreateFlags(), module->spirv.size() * sizeof( uint32_t ), module->spirv.data() );
	module->module = device.createShaderModule( createInfo );
}

// ----------------------------------------------------------------------
// this method is called via renderer::update - before frame processing.
static void backend_update_shader_modules( le_backend_o *self ) {

	// -- find out which shader modules have been tainted
	static auto &file_watcher_i = *Registry::getApi<pal_file_watcher_i>();

	// this will call callbacks on any watched file objects as a side effect
	// callbacks will modify le_backend->modifiedShaderModules
	file_watcher_i.poll_notifications( self->shaderFileWatcher );

	// -- update only modules which have been tainted

	for ( auto &s : self->modifiedShaderModules ) {
		backend_shader_module_update( self, s );
	}

	self->modifiedShaderModules.clear();
}

// ----------------------------------------------------------------------

static le_backend_o *backend_create( le_backend_vk_settings_t *settings ) {
	auto self = new le_backend_o; // todo: leDevice must have been introduced here...

	self->settings = *settings;

	self->instance = std::make_unique<le::Instance>( self->settings.requestedExtensions, self->settings.numRequestedExtensions );
	self->device   = std::make_unique<le::Device>( *self->instance );

	// -- create shader compiler

	static auto &shader_compiler_i = Registry::getApi<le_shader_compiler_api>()->compiler_i;
	self->shader_compiler          = shader_compiler_i.create();

	// -- create file watcher for shader files so that changes can be detected
	static auto &file_watcher_i = *Registry::getApi<pal_file_watcher_i>();
	self->shaderFileWatcher     = file_watcher_i.create();

	return self;
}

// ----------------------------------------------------------------------

static void backend_destroy( le_backend_o *self ) {

	using namespace le_shader_compiler;

	if ( self->shaderFileWatcher ) {
		// -- destroy file watcher
		static auto &file_watcher_i = *Registry::getApi<pal_file_watcher_i>();
		file_watcher_i.destroy( self->shaderFileWatcher );
		self->shaderFileWatcher = nullptr;
	}

	if ( self->shader_compiler ) {
		// -- destroy shader compiler
		compiler_i.destroy( self->shader_compiler );
		self->shader_compiler = nullptr;
	}

	vk::Device device = self->device->getVkDevice();

	// -- destroy any pipeline state objects
	for ( auto &pPso : self->PSOs ) {
		delete ( pPso );
	}
	self->PSOs.clear();

	// -- destroy retained shader modules

	for ( auto &s : self->shaderModules ) {
		if ( s->module ) {
			device.destroyShaderModule( s->module );
			s->module = nullptr;
		}
		delete ( s );
	}
	self->shaderModules.clear();

	// -- destroy renderpasses

	// -- destroy descriptorSetLayouts

	std::cout << "Destroying " << self->descriptorSetLayoutCache.size() << " DescriptorSetLayouts" << std::endl
	          << std::flush;
	for ( auto &p : self->descriptorSetLayoutCache ) {
		device.destroyDescriptorSetLayout( p.second.vk_descriptor_set_layout );
		device.destroyDescriptorUpdateTemplate( p.second.vk_descriptor_update_template );
	}

	// -- destroy pipelineLayouts
	std::cout << "Destroying " << self->pipelineLayoutCache.size() << " PipelineLayouts" << std::endl
	          << std::flush;
	for ( auto &l : self->pipelineLayoutCache ) {
		device.destroyPipelineLayout( l.second );
	}

	for ( auto &p : self->pipelineCache ) {
		device.destroyPipeline( p.second );
	}
	self->pipelineCache.clear();

	if ( self->debugPipelineCache ) {
		device.destroyPipelineCache( self->debugPipelineCache );
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

static void backend_create_swapchain( le_backend_o *self, le_swapchain_vk_settings_o *swapchainSettings_ ) {

	assert( self->window );

	le_swapchain_vk_settings_o tmpSwapchainSettings;

	tmpSwapchainSettings.imagecount_hint                = 3;
	tmpSwapchainSettings.presentmode_hint               = le::Swapchain::Presentmode::eImmediate;
	tmpSwapchainSettings.width_hint                     = self->window->getSurfaceWidth();
	tmpSwapchainSettings.height_hint                    = self->window->getSurfaceHeight();
	tmpSwapchainSettings.vk_device                      = self->device->getVkDevice();
	tmpSwapchainSettings.vk_physical_device             = self->device->getVkPhysicalDevice();
	tmpSwapchainSettings.vk_surface                     = self->window->getVkSurfaceKHR();
	tmpSwapchainSettings.vk_graphics_queue_family_index = self->device->getDefaultGraphicsQueueFamilyIndex();

	self->swapchain            = std::make_unique<le::Swapchain>( &tmpSwapchainSettings );
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

static uint64_t graphics_pso_get_pipeline_layout_hash( le_graphics_pipeline_state_o const *pso ) {
	uint64_t pipeline_layout_hash_data[ 2 ];
	pipeline_layout_hash_data[ 0 ] = pso->shaderModuleVert->hash_pipelinelayout;
	pipeline_layout_hash_data[ 1 ] = pso->shaderModuleFrag->hash_pipelinelayout;
	return SpookyHash::Hash64( pipeline_layout_hash_data, sizeof( pipeline_layout_hash_data ), 0 );
}
// ----------------------------------------------------------------------
// Returns bindings vector associated with a pso, based on the pso's combined bindings,
// and the pso's hash_pipeline_layouts
// currently, we assume bindings to be non-sparse, but it's possible that sparse bindings
// are allowed by the standard. let's check.
static std::vector<le_shader_binding_info> graphics_pso_create_bindings_list( le_graphics_pipeline_state_o const *pso ) {

	std::vector<le_shader_binding_info> combined_bindings;

	// create union of bindings from vert and frag shader
	// we assume these bindings are sorted.

	// TODO: optimise: we only need to re-calculate bindings when
	// the shader pipelinelayout has changed.

	size_t maxNumBindings = pso->shaderModuleVert->bindings.size() +
	                        pso->shaderModuleFrag->bindings.size();

	// -- make space for the full number of bindings
	// note that there could be more bindings than that

	combined_bindings.reserve( maxNumBindings );

	auto vBinding = pso->shaderModuleVert->bindings.begin();
	auto fBinding = pso->shaderModuleFrag->bindings.begin();

	// create a bitmask which compares only setIndex and binding number form a binding

	uint64_t sort_mask = 0;
	{
		le_shader_binding_info info{};
		info.binding  = ~info.binding;
		info.setIndex = ~info.setIndex;
		sort_mask     = info.data;
	}

	for ( size_t i = 0; i != maxNumBindings; ++i ) {

		// Find the lowest binding, and push it back to the
		// vector of combined bindings

		if ( fBinding == pso->shaderModuleFrag->bindings.end() &&
		     vBinding == pso->shaderModuleVert->bindings.end() ) {
			// no more bindings left to process...
			break;
		} else if ( fBinding == pso->shaderModuleFrag->bindings.end() &&
		            vBinding != pso->shaderModuleVert->bindings.end() ) {
			combined_bindings.emplace_back( *vBinding );
			vBinding++;
		} else if ( vBinding == pso->shaderModuleVert->bindings.end() &&
		            fBinding != pso->shaderModuleFrag->bindings.end() ) {
			combined_bindings.emplace_back( *fBinding );
			fBinding++;
		} else if ( ( vBinding->data & sort_mask ) == ( fBinding->data & sort_mask ) ) {

			// -- Check that bindings mentioned in both modules have same (or compatible) properties

			uint64_t compare_mask = 0;
			{
				// switch on elements which we want to compare against
				le_shader_binding_info info{};
				info.type     = ~info.type;
				info.binding  = ~info.binding;
				info.setIndex = ~info.setIndex;
				compare_mask  = info.data;
			}

			bool bindingDataIsConsistent = ( vBinding->data & compare_mask ) == ( fBinding->data & compare_mask );
			bool bindingNameIsConsistent = ( vBinding->name_hash == fBinding->name_hash );

			// elements captured by compare mask must be identical
			if ( bindingDataIsConsistent ) {

				if ( !bindingNameIsConsistent ) {
					// This is not tragic, but we need to flag up that this binding is not
					// consistently named in case this hints at a bigger issue.

					std::cout << "Warning: Inconsistent name in Set: " << vBinding->setIndex << ", for binding: " << vBinding->binding << std::endl
					          << "\t shader vert: " << pso->shaderModuleVert->filepath << std::endl
					          << "\t shader frag: " << pso->shaderModuleFrag->filepath << std::endl
					          << "Using name given in VERTEX stage for this binding." << std::endl
					          << std::flush;
				}

				if ( vBinding->type == enumToNum( vk::DescriptorType::eUniformBuffer ) ||
				     vBinding->type == enumToNum( vk::DescriptorType::eUniformBufferDynamic ) ) {
					// If we're dealing with a buffer type, we must check ranges
					// TODO: if one of them has range == 0, that means this shader stage can be ignored
					// If they have not the same range, that means we need to take the largest range of them both
					vBinding->range = std::max( vBinding->range, fBinding->range );
				}

				// -- combine stage bits so that descriptor will be available for both
				// stages.
				vBinding->stage_bits |= fBinding->stage_bits;

				// if count is not identical,that's not that bad, we adjust to larger of the two
				vBinding->count = std::max( vBinding->count, fBinding->count );

				combined_bindings.emplace_back( *vBinding );

			} else {

				std::cerr << "ERROR: Shader binding mismatch in set: " << vBinding->setIndex
				          << ", binding: " << vBinding->binding << std::endl
				          << "\t shader vert: " << pso->shaderModuleVert->filepath << std::endl
				          << "\t shader frag: " << pso->shaderModuleFrag->filepath << std::endl
				          << std::flush;
				assert( false ); // abandon all hope.
			};

			vBinding++;
			fBinding++;
		} else if ( vBinding->data < fBinding->data ) {
			combined_bindings.emplace_back( *vBinding );
			vBinding++;
		} else if ( vBinding->data > fBinding->data ) {
			combined_bindings.emplace_back( *fBinding );
			fBinding++;
		}
	}

	return combined_bindings;
}

// ----------------------------------------------------------------------

static inline vk::VertexInputRate vk_input_rate_from_le_input_rate( const le_vertex_input_binding_description::INPUT_RATE &input_rate ) {
	switch ( input_rate ) {
	case ( le_vertex_input_binding_description::ePerInstance ):
	    return vk::VertexInputRate::eInstance;
	case ( le_vertex_input_binding_description::ePerVertex ):
	    return vk::VertexInputRate::eVertex;
	}
}

// clang-format off
/// \returns corresponding vk::Format for a given le_input_attribute_description struct
static inline vk::Format vk_format_from_le_vertex_input_attribute_description(const le_vertex_input_attribute_description* d){

	if ( d->vecsize == 0 || d->vecsize > 4 ){
		assert(false); // vecsize must be between 1 and 4
		return vk::Format::eUndefined;
	}

	switch ( d->type ) {
	case le_vertex_input_attribute_description::eFloat:
		switch ( d->vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Sfloat;
		case 3: return vk::Format::eR32G32B32Sfloat;
		case 2: return vk::Format::eR32G32Sfloat;
		case 1: return vk::Format::eR32Sfloat;
		}
	    break;
	case le_vertex_input_attribute_description::eHalf:
		switch ( d->vecsize ) {
		case 4: return vk::Format::eR16G16B16A16Sfloat;
		case 3: return vk::Format::eR16G16B16Sfloat;
		case 2: return vk::Format::eR16G16Sfloat;
		case 1: return vk::Format::eR16Sfloat;
		}
	    break;
	case le_vertex_input_attribute_description::eShort:
		if (d->isNormalised){
			switch ( d->vecsize ) {
			case 4: return vk::Format::eR16G16B16A16Unorm;
			case 3: return vk::Format::eR16G16B16Unorm;
			case 2: return vk::Format::eR16G16Unorm;
			case 1: return vk::Format::eR16Unorm;
			}
		}else{
			switch ( d->vecsize ) {
			case 4: return vk::Format::eR16G16B16A16Uint;
			case 3: return vk::Format::eR16G16B16Uint;
			case 2: return vk::Format::eR16G16Uint;
			case 1: return vk::Format::eR16Uint;
			}
		}
	    break;
	case le_vertex_input_attribute_description::eInt:
		switch ( d->vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Sint;
		case 3: return vk::Format::eR32G32B32Sint;
		case 2: return vk::Format::eR32G32Sint;
		case 1: return vk::Format::eR32Sint;
		}
	    break;
	case le_vertex_input_attribute_description::eUInt:
		switch ( d->vecsize ) {
		case 4: return vk::Format::eR32G32B32A32Uint;
		case 3: return vk::Format::eR32G32B32Uint;
		case 2: return vk::Format::eR32G32Uint;
		case 1: return vk::Format::eR32Uint;
		}
	    break;
	case le_vertex_input_attribute_description::eChar:
		if (d->isNormalised){
			switch ( d->vecsize ) {
			case 4: return vk::Format::eR8G8B8A8Unorm;
			case 3: return vk::Format::eR8G8B8Unorm;
			case 2: return vk::Format::eR8G8Unorm;
			case 1: return vk::Format::eR8Unorm;
			}
		} else {
			switch ( d->vecsize ) {
			case 4: return vk::Format::eR8G8B8A8Uint;
			case 3: return vk::Format::eR8G8B8Uint;
			case 2: return vk::Format::eR8G8Uint;
			case 1: return vk::Format::eR8Uint;
			}
		}
	    break;
	}

	assert(false); // abandon all hope
	return vk::Format::eUndefined;
}
// clang-format on

// ----------------------------------------------------------------------

static le_graphics_pipeline_state_o *backend_create_grapics_pipeline_state_object( le_backend_o *self, le_graphics_pipeline_create_info_t const *info ) {
	auto pso = new ( le_graphics_pipeline_state_o );

	// -- add shader modules to pipeline
	//
	// (shader modules are backend objects)
	pso->shaderModuleFrag = info->shader_module_frag;
	pso->shaderModuleVert = info->shader_module_vert;

	if ( info->vertex_input_attribute_descriptions &&
	     info->vertex_input_binding_descriptions &&
	     info->vertex_input_attribute_descriptions_count &&
	     info->vertex_input_binding_descriptions_count ) {

		// create vertex input binding descriptions

		for ( auto const *b = info->vertex_input_binding_descriptions;
		      b != info->vertex_input_binding_descriptions + info->vertex_input_binding_descriptions_count;
		      b++ ) {

			vk::VertexInputBindingDescription bindingDescription;
			bindingDescription
			    .setBinding( b->binding )
			    .setStride( b->stride )
			    .setInputRate( vk_input_rate_from_le_input_rate( b->input_rate ) );

			pso->explicitVertexBindingDescriptions.emplace_back( std::move( bindingDescription ) );
		}

		// create vertex input attribute descriptions
		for ( auto const *a = info->vertex_input_attribute_descriptions;
		      a != info->vertex_input_attribute_descriptions + info->vertex_input_attribute_descriptions_count;
		      a++ ) {
			vk::VertexInputAttributeDescription attributeDescription;
			attributeDescription
			    .setLocation( a->location )
			    .setBinding( a->binding )
			    .setFormat( vk_format_from_le_vertex_input_attribute_description( a ) )
			    .setOffset( a->binding_offset );

			pso->explicitVertexAttributeDescriptions.emplace_back( std::move( attributeDescription ) );
		}

		pso->useExplicitVertexInputDescriptions = true;
	}

	// TODO (pipeline): -- initialise pso based on pipeline info

	if ( info->rasterizationState ) {
		// copy rasterisation state if available, otherwise use our own
		pso->rasterizationInfo = *info->rasterizationState;
	} else {
		vk::PipelineRasterizationStateCreateInfo defaultRasterizationState;
		defaultRasterizationState
		    .setDepthClampEnable( VK_FALSE )
		    .setRasterizerDiscardEnable( VK_FALSE )
		    .setPolygonMode( ::vk::PolygonMode::eFill )
		    .setCullMode( ::vk::CullModeFlagBits::eNone )
		    .setFrontFace( ::vk::FrontFace::eCounterClockwise )
		    .setDepthBiasEnable( VK_FALSE )
		    .setDepthBiasConstantFactor( 0.f )
		    .setDepthBiasClamp( 0.f )
		    .setDepthBiasSlopeFactor( 1.f )
		    .setLineWidth( 1.f );
		pso->rasterizationInfo = std::move( defaultRasterizationState );
	}

	// -- calculate hash based on contents of pipeline state object

	// TODO: -- calculate hash for pipeline state based on create_info (state that's not related to shaders)
	// create_info will contain state like blend, polygon mode, culling etc.
	pso->hash = 0x0;

	self->PSOs.push_back( pso );
	return pso;
}

// ----------------------------------------------------------------------
// via called via decoder / produce_frame -
static vk::PipelineLayout backend_get_pipeline_layout( le_backend_o *self, le_graphics_pipeline_state_o const *pso ) {

	uint64_t pipelineLayoutHash = graphics_pso_get_pipeline_layout_hash( pso );

	auto foundLayout = self->pipelineLayoutCache.find( pipelineLayoutHash );

	if ( foundLayout != self->pipelineLayoutCache.end() ) {
		return foundLayout->second;
	} else {
		std::cerr << "ERROR: Could not find pipeline layout with hash: " << std::hex << pipelineLayoutHash << std::endl
		          << std::flush;
		assert( false );
		return nullptr;
	}
}

// ----------------------------------------------------------------------
static vk::Pipeline backend_create_pipeline( le_backend_o *self, le_graphics_pipeline_state_o const *pso, const Pass &pass, uint32_t subpass ) {

	vk::Device vkDevice = self->device->getVkDevice();

	std::array<vk::PipelineShaderStageCreateInfo, 2> pipelineStages;
	pipelineStages[ 0 ]
	    .setFlags( {} ) // must be 0 - "reserved for future use"
	    .setStage( vk::ShaderStageFlagBits::eVertex )
	    .setModule( pso->shaderModuleVert->module )
	    .setPName( "main" )
	    .setPSpecializationInfo( nullptr );
	pipelineStages[ 1 ]
	    .setFlags( {} ) // must be 0 - "reserved for future use"
	    .setStage( vk::ShaderStageFlagBits::eFragment )
	    .setModule( pso->shaderModuleFrag->module )
	    .setPName( "main" )
	    .setPSpecializationInfo( nullptr );

	std::vector<vk::VertexInputBindingDescription> const *  vertexBindingDescriptions;        // where to get data from
	std::vector<vk::VertexInputAttributeDescription> const *vertexInputAttributeDescriptions; // how it feeds into the shader's vertex inputs

	if ( pso->useExplicitVertexInputDescriptions ) {
		// use vertex input schema based on explicit user input
		// which was stored in `backend_create_grapics_pipeline_state_object`
		vertexBindingDescriptions        = &pso->explicitVertexBindingDescriptions;
		vertexInputAttributeDescriptions = &pso->explicitVertexAttributeDescriptions;
	} else {
		// Default: use vertex input schema based on shader reflection
		vertexBindingDescriptions        = &pso->shaderModuleVert->vertexBindingDescriptions;
		vertexInputAttributeDescriptions = &pso->shaderModuleVert->vertexAttributeDescriptions;
	}

	vk::PipelineVertexInputStateCreateInfo vertexInputStageInfo;
	vertexInputStageInfo
	    .setFlags( vk::PipelineVertexInputStateCreateFlags() )
	    .setVertexBindingDescriptionCount( uint32_t( vertexBindingDescriptions->size() ) )
	    .setPVertexBindingDescriptions( vertexBindingDescriptions->data() )
	    .setVertexAttributeDescriptionCount( uint32_t( vertexInputAttributeDescriptions->size() ) )
	    .setPVertexAttributeDescriptions( vertexInputAttributeDescriptions->data() );

	// fetch vk::PipelineLayout for this pso
	auto pipelineLayout = backend_get_pipeline_layout( self, pso );

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
	inputAssemblyState
	    .setTopology( ::vk::PrimitiveTopology::eTriangleList )
	    .setPrimitiveRestartEnable( VK_FALSE );

	vk::PipelineTessellationStateCreateInfo tessellationState;
	tessellationState
	    .setPatchControlPoints( 3 );

	// viewport and scissor are tracked as dynamic states, so this object
	// will not get used.
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState
	    .setViewportCount( 1 )
	    .setPViewports( nullptr )
	    .setScissorCount( 1 )
	    .setPScissors( nullptr );

	vk::PipelineMultisampleStateCreateInfo multisampleState;
	multisampleState
	    .setRasterizationSamples( ::vk::SampleCountFlagBits::e1 )
	    .setSampleShadingEnable( VK_FALSE )
	    .setMinSampleShading( 0.f )
	    .setPSampleMask( nullptr )
	    .setAlphaToCoverageEnable( VK_FALSE )
	    .setAlphaToOneEnable( VK_FALSE );

	vk::StencilOpState stencilOpState;
	stencilOpState
	    .setFailOp( ::vk::StencilOp::eKeep )
	    .setPassOp( ::vk::StencilOp::eKeep )
	    .setDepthFailOp( ::vk::StencilOp::eKeep )
	    .setCompareOp( ::vk::CompareOp::eNever )
	    .setCompareMask( 0 )
	    .setWriteMask( 0 )
	    .setReference( 0 );

	vk::PipelineDepthStencilStateCreateInfo depthStencilState;
	depthStencilState
	    .setDepthTestEnable( VK_TRUE )
	    .setDepthWriteEnable( VK_TRUE )
	    .setDepthCompareOp( ::vk::CompareOp::eLessOrEqual )
	    .setDepthBoundsTestEnable( VK_FALSE )
	    .setStencilTestEnable( VK_FALSE )
	    .setFront( stencilOpState )
	    .setBack( stencilOpState )
	    .setMinDepthBounds( 0.f )
	    .setMaxDepthBounds( 0.f );

	//
	// We must match blend attachment states with number of attachments for
	// the current renderpass - each attachment may have their own blend state:
	//
	static std::array<vk::PipelineColorBlendAttachmentState, 16> blendAttachmentStates{};

	// NOTE: Blend modes should be an attribute of the current renderpass rather than the pipeline.
	// this because the blend mode is dependent on the number of attachments.
	// - but blend mode may change within a renderpass...

	for ( size_t i = 0; i != pass.numColorAttachments; ++i ) {
		blendAttachmentStates[ i ]
		    .setBlendEnable( VK_TRUE )
		    .setColorBlendOp( ::vk::BlendOp::eAdd )
		    .setAlphaBlendOp( ::vk::BlendOp::eAdd )
		    .setSrcColorBlendFactor( ::vk::BlendFactor::eSrcAlpha )
		    .setDstColorBlendFactor( ::vk::BlendFactor::eOneMinusSrcAlpha )
		    .setSrcAlphaBlendFactor( ::vk::BlendFactor::eOne )
		    .setDstAlphaBlendFactor( ::vk::BlendFactor::eZero )
		    .setColorWriteMask(
		        ::vk::ColorComponentFlagBits::eR |
		        ::vk::ColorComponentFlagBits::eG |
		        ::vk::ColorComponentFlagBits::eB |
		        ::vk::ColorComponentFlagBits::eA );
	}

	vk::PipelineColorBlendStateCreateInfo colorBlendState;
	colorBlendState
	    .setLogicOpEnable( VK_FALSE )
	    .setLogicOp( ::vk::LogicOp::eClear )
	    .setAttachmentCount( pass.numColorAttachments )
	    .setPAttachments( blendAttachmentStates.data() )
	    .setBlendConstants( {{0.f, 0.f, 0.f, 0.f}} );

	std::array<vk::DynamicState, 3> dynamicStates = {{
	    vk::DynamicState::eScissor,
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eLineWidth,
	}};

	vk::PipelineDynamicStateCreateInfo dynamicState;
	dynamicState
	    .setDynamicStateCount( dynamicStates.size() )
	    .setPDynamicStates( dynamicStates.data() );

	// setup pipeline
	vk::GraphicsPipelineCreateInfo gpi;
	gpi
	    .setFlags( vk::PipelineCreateFlagBits::eAllowDerivatives )
	    .setStageCount( uint32_t( pipelineStages.size() ) )
	    .setPStages( pipelineStages.data() )
	    .setPVertexInputState( &vertexInputStageInfo )
	    .setPInputAssemblyState( &inputAssemblyState )
	    .setPTessellationState( nullptr )
	    .setPViewportState( &viewportState )
	    .setPRasterizationState( &pso->rasterizationInfo )
	    .setPMultisampleState( &multisampleState )
	    .setPDepthStencilState( &depthStencilState )
	    .setPColorBlendState( &colorBlendState )
	    .setPDynamicState( &dynamicState )
	    .setLayout( pipelineLayout )
	    .setRenderPass( pass.renderPass ) // must be a valid renderpass.
	    .setSubpass( subpass )
	    .setBasePipelineHandle( nullptr )
	    .setBasePipelineIndex( 0 ) // -1 signals not to use a base pipeline index
	    ;

	auto pipeline = vkDevice.createGraphicsPipeline( self->debugPipelineCache, gpi );
	return pipeline;
}

// ----------------------------------------------------------------------
// returns hash key for given bindings, creates and retains new vkDescriptorSetLayout inside backend if necessary
static uint64_t backend_produce_descriptor_set_layout( le_backend_o *self, std::vector<le_shader_binding_info> const &bindings, vk::DescriptorSetLayout *layout ) {

	// -- calculate hash based on le_shader_binding_infos for this set
	uint64_t set_layout_hash = SpookyHash::Hash64( bindings.data(), bindings.size() * sizeof( le_shader_binding_info ), 0 );

	auto foundLayout = self->descriptorSetLayoutCache.find( set_layout_hash );

	if ( foundLayout == self->descriptorSetLayoutCache.end() ) {

		// layout was not found in cache, we must create vk objects.

		vk::Device device = self->device->getVkDevice();

		std::vector<vk::DescriptorSetLayoutBinding> vk_bindings;

		vk_bindings.reserve( bindings.size() );

		for ( const auto &b : bindings ) {
			vk::DescriptorSetLayoutBinding binding{};
			binding.setBinding( b.binding )
			    .setDescriptorType( vk::DescriptorType( b.type ) )
			    .setDescriptorCount( b.count )
			    .setStageFlags( vk::ShaderStageFlags( b.stage_bits ) )
			    .setPImmutableSamplers( nullptr );
			vk_bindings.emplace_back( std::move( binding ) );
		}

		vk::DescriptorSetLayoutCreateInfo setLayoutInfo;
		setLayoutInfo
		    .setFlags( vk::DescriptorSetLayoutCreateFlags() )
		    .setBindingCount( uint32_t( vk_bindings.size() ) )
		    .setPBindings( vk_bindings.data() );

		*layout = device.createDescriptorSetLayout( setLayoutInfo );

		// -- Create descriptorUpdateTemplate
		//
		// The template needs to be created so that data for a vk::DescriptorSet
		// can be read from a vector of tightly packed
		// DescriptorData elements.
		//

		vk::DescriptorUpdateTemplate updateTemplate;
		{
			std::vector<vk::DescriptorUpdateTemplateEntry> entries;

			entries.reserve( bindings.size() );

			size_t base_offset = 0; // offset in bytes into DescriptorData vector, assuming vector is tightly packed.
			for ( const auto &b : bindings ) {
				vk::DescriptorUpdateTemplateEntry entry;

				auto descriptorType = vk::DescriptorType( b.type );

				entry.setDstBinding( b.binding );
				entry.setDescriptorCount( b.count );
				entry.setDescriptorType( descriptorType );
				entry.setDstArrayElement( 0 ); // starting element at this binding to update - always 0

				// set offset based on type of binding, so that template reads from correct data

				switch ( descriptorType ) {
				case vk::DescriptorType::eSampler:
				case vk::DescriptorType::eCombinedImageSampler:
				case vk::DescriptorType::eSampledImage:
				case vk::DescriptorType::eStorageImage:
				case vk::DescriptorType::eUniformTexelBuffer:
				case vk::DescriptorType::eStorageTexelBuffer:
				case vk::DescriptorType::eInputAttachment:

					// TODO: find out what descriptorData an InputAttachment expects, if it is really done with an imageInfo
					entry.setOffset( base_offset + offsetof( DescriptorData, sampler ) ); // point to first element of ImageInfo
				    break;
				case vk::DescriptorType::eUniformBuffer:
				case vk::DescriptorType::eStorageBuffer:
				case vk::DescriptorType::eUniformBufferDynamic:
				case vk::DescriptorType::eStorageBufferDynamic:
					entry.setOffset( base_offset + offsetof( DescriptorData, buffer ) ); // point to first element of BufferInfo
				    break;
				}

				entry.setStride( sizeof( DescriptorData ) );

				entries.emplace_back( std::move( entry ) );

				base_offset += sizeof( DescriptorData );
			}

			vk::DescriptorUpdateTemplateCreateInfo info;
			info
			    .setFlags( {} ) // no flags for now
			    .setDescriptorUpdateEntryCount( uint32_t( entries.size() ) )
			    .setPDescriptorUpdateEntries( entries.data() )
			    .setTemplateType( vk::DescriptorUpdateTemplateType::eDescriptorSet )
			    .setDescriptorSetLayout( *layout )
			    .setPipelineBindPoint( {} ) // ignored for this template type
			    .setPipelineLayout( {} )    // ignored for this template type
			    .setSet( 0 )                // ignored for this template type
			    ;

			updateTemplate = device.createDescriptorUpdateTemplate( info );
		}

		le_descriptor_set_layout_t le_layout_info;
		le_layout_info.vk_descriptor_set_layout      = *layout;
		le_layout_info.binding_info                  = bindings;
		le_layout_info.vk_descriptor_update_template = updateTemplate;

		self->descriptorSetLayoutCache[ set_layout_hash ] = std::move( le_layout_info );
	} else {

		// layout was found in cache.

		*layout = foundLayout->second.vk_descriptor_set_layout;
	}

	return set_layout_hash;
}

// ----------------------------------------------------------------------

static le_pipeline_layout_info backend_produce_pipeline_layout_info( le_backend_o *self, le_graphics_pipeline_state_o const *pso ) {
	le_pipeline_layout_info info{};

	std::vector<le_shader_binding_info> combined_bindings = graphics_pso_create_bindings_list( pso );

	// -- Create array of DescriptorSetLayouts
	std::array<vk::DescriptorSetLayout, 8> vkLayouts{};
	{

		// -- create one vkDescriptorSetLayout for each set in bindings

		std::vector<std::vector<le_shader_binding_info>> sets;

		// Split combined bindings at set boundaries
		uint32_t set_id = 0;
		for ( auto it = combined_bindings.begin(); it != combined_bindings.end(); ) {

			// Find next element with different set id
			auto itN = std::find_if( it, combined_bindings.end(), [&set_id]( const le_shader_binding_info &el ) -> bool {
				return el.setIndex != set_id;
			} );

			sets.emplace_back( it, itN );

			// If we're not at the end, get the setIndex for the next set,
			if ( itN != combined_bindings.end() ) {
				assert( set_id + 1 == itN->setIndex ); // we must enforce that sets are non-sparse.
				set_id = itN->setIndex;
			}

			it = itN;
		}

		info.set_layout_count = uint32_t( sets.size() );
		assert( sets.size() <= 8 );

		for ( size_t i = 0; i != sets.size(); ++i ) {
			info.set_layout_keys[ i ] = backend_produce_descriptor_set_layout( self, sets[ i ], &vkLayouts[ i ] );
		}
	}

	info.pipeline_layout_key = graphics_pso_get_pipeline_layout_hash( pso );

	// -- Attempt to find this pipelineLayout from cache, if we can't find one, we create and retain it.

	auto found_pl = self->pipelineLayoutCache.find( info.pipeline_layout_key );

	if ( found_pl == self->pipelineLayoutCache.end() ) {

		vk::Device                   device = self->device->getVkDevice();
		vk::PipelineLayoutCreateInfo layoutCreateInfo;
		layoutCreateInfo
		    .setFlags( vk::PipelineLayoutCreateFlags() ) // "reserved for future use"
		    .setSetLayoutCount( uint32_t( info.set_layout_count ) )
		    .setPSetLayouts( vkLayouts.data() )
		    .setPushConstantRangeCount( 0 )
		    .setPPushConstantRanges( nullptr );

		// create vkPipelineLayout and store it in cache.
		self->pipelineLayoutCache[ info.pipeline_layout_key ] = device.createPipelineLayout( layoutCreateInfo );
	}

	return info;
}

/// \brief Creates - or loads a pipeline from cache based on current pipeline state
/// \note this method may lock the pipeline cache and is therefore costly.
// TODO: Ensure there are no races around this method
//
// + Only the command buffer recording slice of a frame shall be able to modify the cache
//   the cache must be exclusively accessed through this method
//
// + Access to this method must be sequential - no two frames may access this method
//   at the same time.
static le_pipeline_and_layout_info_t backend_produce_pipeline( le_backend_o *self, le_graphics_pipeline_state_o const *pso, const Pass &pass, uint32_t subpass ) {

	le_pipeline_and_layout_info_t pipeline_and_layout_info = {};

	// -- 1. get pipeline layout info for a pipeline with these bindings
	// we try to fetch it from the cache first, if it doesn't exist, we must create it, and add it to the cache.

	uint64_t pipeline_layout_hash = graphics_pso_get_pipeline_layout_hash( pso );

	auto pl = self->pipelineLayoutInfoCache.find( pipeline_layout_hash );

	if ( pl == self->pipelineLayoutInfoCache.end() ) {

		// this will also create vulkan objects for pipeline layout / descriptor set layout and cache them
		pipeline_and_layout_info.layout_info = backend_produce_pipeline_layout_info( self, pso );

		// store in cache
		self->pipelineLayoutInfoCache[ pipeline_layout_hash ] = pipeline_and_layout_info.layout_info;
	} else {
		pipeline_and_layout_info.layout_info = pl->second;
	}

	// -- 2. get vk pipeline object
	// we try to fetch it from the cache first, if it doesn't exist, we must create it, and add it to the cache.

	uint64_t pso_renderpass_hash_data[ 4 ] = {};

	pso_renderpass_hash_data[ 0 ] = pso->hash;                   // TODO: Hash for PSO state - must have been updated before recording phase started
	pso_renderpass_hash_data[ 1 ] = pso->shaderModuleVert->hash; // Module state - may have been recompiled, hash must be current
	pso_renderpass_hash_data[ 2 ] = pso->shaderModuleFrag->hash; // Module state - may have been recompiled, hash must be current
	pso_renderpass_hash_data[ 3 ] = pass.renderpassHash;         // Hash for *compatible* renderpass

	// -- create combined hash for pipeline, renderpass

	uint64_t pipeline_hash = SpookyHash::Hash64( pso_renderpass_hash_data, sizeof( pso_renderpass_hash_data ), pipeline_layout_hash );

	// -- look up if pipeline with this hash already exists in cache
	auto p = self->pipelineCache.find( pipeline_hash );

	if ( p == self->pipelineCache.end() ) {

		// -- if not, create pipeline in pipeline cache and store / retain it
		pipeline_and_layout_info.pipeline = backend_create_pipeline( self, pso, pass, subpass );

		std::cout << "New VK Pipeline created: 0x" << std::hex << pipeline_hash << std::endl
		          << std::flush;

		self->pipelineCache[ pipeline_hash ] = pipeline_and_layout_info.pipeline;
	} else {
		// -- else return pipeline found in hash map
		pipeline_and_layout_info.pipeline = p->second;
	}

	return pipeline_and_layout_info;
}

// ----------------------------------------------------------------------
/// \brief create a unique resource handle
/// \details Resource handles are used as names, but they might have information associated with them
static LeResourceHandle backend_declare_resource( le_backend_o *self, LeResourceType type ) {
	uint32_t             resourceId = self->resource_counter++; // Resource counter is atomic, so resource ids will be unique.
	LeResourceHandleMeta handle{};
	handle.id   = resourceId;
	handle.type = type;
	static_assert( sizeof( LeResourceHandle ) == sizeof( uint64_t ), "LeResourceHandle must be 64bit of size" );
	static_assert( sizeof( LeResourceHandleMeta ) == sizeof( uint64_t ), "LeResourceMeta must be 64bit of size" );

	return reinterpret_cast<LeResourceHandle>( handle.data );
}

// ----------------------------------------------------------------------
static LeResourceHandle backend_declare_resource_virtual_buffer( le_backend_o *self, uint8_t index ) {
	LeResourceHandle resource{}; // virtual resources all have the same id, 0, which means they are not part of the regular roster of resources...

	{
		auto &r = reinterpret_cast<LeResourceHandleMeta &>( resource );

		r.index = index;
		r.type  = LeResourceType::eBuffer;
		r.flags = LeResourceHandleMeta::FlagBits::eIsVirtual;
	}

	static_assert( sizeof( LeResourceHandleMeta ) == sizeof( LeResourceHandle ), "LeResourceHandle and LeResourceHandleMeta must be of equal size" );
	return resource;
}

// ----------------------------------------------------------------------
static LeResourceHandle backend_get_backbuffer_resource( le_backend_o *self ) {
	return self->backBufferImageHandle;
}

// ----------------------------------------------------------------------

static void backend_setup( le_backend_o *self ) {

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
			// find memory index for scratch buffer

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

		// let's create a pool for each Frame, so that each frame can create sub-allocators
		// when it creates command buffers for each frame.
	}

	assert( vkDevice ); // device must come from somewhere! It must have been introduced to backend before, or backend must create device used by everyone else...

	for ( size_t i = 0; i != frameCount; ++i ) {

		// -- Set up per-frame resources

		BackendFrameData frameData{};

		frameData.frameFence               = vkDevice.createFence( {} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = vkDevice.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = vkDevice.createSemaphore( {} );
		frameData.commandPool              = vkDevice.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->device->getDefaultGraphicsQueueFamilyIndex()} );

		// -- set up an allocation pool for each frame

		{
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

	vk::PipelineCacheCreateInfo pipelineCacheInfo;
	pipelineCacheInfo
	    .setFlags( vk::PipelineCacheCreateFlags() ) // "reserved for future use"
	    .setInitialDataSize( 0 )
	    .setPInitialData( nullptr );

	self->debugPipelineCache = vkDevice.createPipelineCache( pipelineCacheInfo );

	self->resource_counter      = 1;                                                        // initialize resource counter to 1, 0 means invalid not initialised resource.
	self->backBufferImageHandle = backend_declare_resource( self, LeResourceType::eImage ); // initialize backbuffer image handle
}

// ----------------------------------------------------------------------

static void frame_track_resource_state( BackendFrameData &frame, le_renderpass_o **ppPasses, size_t numRenderPasses, const vk::Format &swapchainImageFormat, const LeResourceHandle &backbufferImageHandle ) {

	// track resource state

	// we should mark persistent resources which are not frame-local with special flags, so that they
	// come with an initial element in their sync chain, this element signals their last (frame-crossing) state
	// this naturally applies to "backbuffer", for example.

	// a pipeline barrier is defined as a combination of execution dependency and
	// memory dependency.
	// An EXECUTION DEPENDENCY tells us which stage needs to be complete (srcStage) before another named stage (dstStage) may execute.
	// A MEMORY DEPENDECY tells us which memory needs to be made available/flushed (srcAccess) after srcStage
	// before another memory can be made visible/invalidated (dstAccess) before dstStage

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
			std::cout << "warning: no reference to backbuffer found in renderpasses" << std::flush;
		}
	}

	// * sync state: ready to enter renderpass: colorattachmentOutput=visible *

	// Renderpass implicit sync (per resource):
	// + enter renderpass : INITIAL LAYOUT (layout must match)
	// + layout transition if initial layout and attachment reference layout differ for subpass [ attachment memory is automatically made AVAILABLE | see Spec 6.1.1]
	//   [layout transition happens-before any LOAD OPs: source: amd open source driver | https://github.com/GPUOpen-Drivers/xgl/blob/aa330d8e9acffb578c88193e4abe017c8fe15426/icd/api/renderpass/renderpass_builder.cpp#L819]
	// + load/clear op (executed using INITIAL LAYOUT once before first use per-resource) [ attachment memory must be AVAILABLE ]
	// + enter subpass
	// + command execution [attachment memory must be VISIBLE ]
	// + store op
	// + exit subpass : final layout
	// + exit renderpass
	// + layout transform (if final layout differs)

	static auto const &renderpass_i = Registry::getApi<le_renderer_api>()->le_renderpass_i;

	frame.passes.reserve( numRenderPasses );

	// TODO: move pass creation to its own method.

	for ( auto pass = ppPasses; pass != ppPasses + numRenderPasses; pass++ ) {

		Pass currentPass{};
		currentPass.type = renderpass_i.get_type( *pass );

		currentPass.width = renderpass_i.get_width( *pass );
		if ( currentPass.width == 0 ) {
			// if zero was chosen this means to use the default extents values for a
			// renderpass, which is to use the frame's current swapchain extents.
			currentPass.width = frame.swapchainWidth;
		}

		currentPass.height = renderpass_i.get_height( *pass );
		if ( currentPass.height == 0 ) {
			// if zero was chosen this means to use the default extents values for a
			// renderpass, which is to use the frame's current swapchain extents.
			currentPass.height = frame.swapchainHeight;
		}

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

				switch ( imageAttachment->access_flags ) {
				case eLeAccessFlagBitsReadWrite:
					// resource.loadOp must be LOAD

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
				    break;

				case eLeAccessFlagBitWrite:
					// resource.loadOp must be either CLEAR / or DONT_CARE
					beforeFirstUse.write_stage    = isDepthStencil ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eColorAttachmentOutput;
					beforeFirstUse.visible_access = vk::AccessFlagBits( 0 );
					beforeFirstUse.layout         = vk::ImageLayout::eUndefined; // override to undefined to invalidate attachment which will be cleared.
				    break;

				case eLeAccessFlagBitRead:
				    break;
				}

				currentAttachment->initialStateOffset = uint16_t( syncChain.size() );
				syncChain.emplace_back( std::move( beforeFirstUse ) ); // attachment initial state for a renderpass - may be loaded/cleared on first use
				                                                       // * sync state: ready for load/store *
			}

			{
				// track resource state before subpass

				auto &previousSyncState = syncChain.back();
				auto  beforeSubpass{previousSyncState};

				if ( imageAttachment->access_flags == eLeAccessFlagBitsReadWrite ) {
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

				} else if ( imageAttachment->access_flags & eLeAccessFlagBitRead ) {
				} else if ( imageAttachment->access_flags & eLeAccessFlagBitWrite ) {

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

	static const auto &le_encoder_api = ( *Registry::getApi<le_renderer_api>() ).le_command_buffer_encoder_i;

	for ( auto &f : frame.passes ) {
		if ( f.encoder ) {
			le_encoder_api.destroy( f.encoder );
			f.encoder = nullptr;
		}
	}
	frame.passes.clear();

	device.resetCommandPool( frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources );

	return true;
};

// ----------------------------------------------------------------------

static void backend_create_renderpasses( BackendFrameData &frame, vk::Device &device ) {

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

#ifndef NDEBUG
			if ( attachment->resource_id == nullptr ) {
				std::cerr << "[ FATAL ] Use of undeclared resource handle. Did you forget to declare this resource handle with the renderer?" << std::endl
				          << std::flush;
			}
			assert( attachment->resource_id != nullptr ); // resource id must not be zero: did you forget to declare this resource with the renderer via renderer->declareResource?
#endif

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
				std::cout << "attachment: " << std::hex << attachment->resource_id << std::endl;
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
					// field, which want to ignore, since it makes no difference for render pass compatibility.

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

	static auto const &renderpass_i = Registry::getApi<le_renderer_api>()->le_renderpass_i;

	frame.syncChainTable.clear();

	for ( auto *pPass = passes; pPass != passes + numRenderPasses; pPass++ ) {

		LeResourceHandle const *pResources   = nullptr;
		size_t                  numResources = 0;

		// add all read resources to pass
		renderpass_i.get_read_resources( *pPass, &pResources, &numResources );
		for ( auto it = pResources; it != pResources + numResources; ++it ) {
			frame.syncChainTable.insert( {*it, {BackendFrameData::ResourceState{}}} );
		}

		// add all write resources to pass
		renderpass_i.get_write_resources( *pPass, &pResources, &numResources );
		for ( auto it = pResources; it != pResources + numResources; ++it ) {
			frame.syncChainTable.insert( {*it, {BackendFrameData::ResourceState{}}} );
		}

		// createResources are a subset of write resources,
		// so by adding write resources these were already added.
	}
}

// ----------------------------------------------------------------------

/// \brief fetch vk::Buffer from encoder index if transient, otherwise, fetch from frame available resources.
static inline vk::Buffer frame_data_get_buffer_from_le_resource_id( const BackendFrameData &frame, const LeResourceHandle &resourceId ) {
	auto &resourceMeta = reinterpret_cast<LeResourceHandleMeta const &>( resourceId );

	assert( resourceMeta.type == LeResourceType::eBuffer ); // resource type must be buffer

	if ( resourceMeta.flags & LeResourceHandleMeta::FlagBits::eIsVirtual ) {
		return frame.allocatorBuffers[ resourceMeta.index ];
	} else {

		return frame.availableResources.at( resourceId ).asBuffer;
	}
}

// ----------------------------------------------------------------------
static inline vk::Image frame_data_get_image_from_le_resource_id( const BackendFrameData &frame, const LeResourceHandle &resourceId ) {

	auto &resourceMeta = reinterpret_cast<LeResourceHandleMeta const &>( resourceId );
	assert( resourceMeta.type == LeResourceType::eImage ); // resource type must be image

	return frame.availableResources.at( resourceId ).asImage;
}

// ----------------------------------------------------------------------
static inline VkFormat frame_data_get_image_format_from_resource_id( BackendFrameData const &frame, const LeResourceHandle &resourceId ) {

	auto &resourceMeta = reinterpret_cast<LeResourceHandleMeta const &>( resourceId );
	assert( resourceMeta.type == LeResourceType::eImage ); // resource type must be image

	return frame.availableResources.at( resourceId ).info.imageInfo.format;
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

	static auto const &renderpass_i = Registry::getApi<le_renderer_api>()->le_renderpass_i;

	std::unordered_map<LeResourceHandle, ResourceInfo> declaredResources;

	// -- iterate over all passes
	for ( le_renderpass_o **rp = passes; rp != passes + numRenderPasses; rp++ ) {

		LeResourceHandle const *  pCreateResourceIds = nullptr;
		le_resource_info_t const *pResourceInfos     = nullptr;
		size_t                    numCreateResources = 0;

		// -- iterate over all resource declarations in this pass
		renderpass_i.get_create_resources( *rp, &pCreateResourceIds, &pResourceInfos, &numCreateResources );

		for ( size_t i = 0; i != numCreateResources; ++i ) {

			le_resource_info_t const &createInfo = pResourceInfos[ i ];     // Resource descriptor (from renderpass)
			LeResourceHandle const &  resourceId = pCreateResourceIds[ i ]; // Hash of resource name

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
				imgInfoRef.extent.width          = ci.extent.width != 0 ? ci.extent.width : frame.swapchainWidth;
				imgInfoRef.extent.height         = ci.extent.height != 0 ? ci.extent.height : frame.swapchainHeight;
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

			auto it = declaredResources.emplace( resourceId, std::move( resourceCreateInfo ) );

			if ( it.second == false ) {
				assert( false ); // resource was re-declared, this must not happen
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

		LeResourceHandle const &resourceId           = r.first;  // hash of resource name
		ResourceInfo const &    declaredResourceInfo = r.second; // current resourceInfo

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

		const LeResourceHandle *textureIds     = nullptr;
		size_t                  textureIdCount = 0;
		renderpass_i.get_texture_ids( *p, &textureIds, &textureIdCount );

		const LeTextureInfo *textureInfos     = nullptr;
		size_t               textureInfoCount = 0;
		renderpass_i.get_texture_infos( *p, &textureInfos, &textureInfoCount );

		assert( textureIdCount == textureInfoCount ); // texture info and -id count must be identical, as there is a 1:1 relationship

		for ( size_t i = 0; i != textureIdCount; i++ ) {

			// -- find out if texture with this name has already been alloacted.
			// -- if not, allocate

			const LeResourceHandle textureId = textureIds[ i ];

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

	static auto const &allocator_i = Registry::getApi<le_backend_vk_api>()->le_allocator_linear_i;

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

			LeResourceHandle res = backend_declare_resource_virtual_buffer( self, uint8_t( i ) );

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
			    .setPQueueFamilyIndices( &self->queueFamilyIndexGraphics ); // TODO: use compute queue for compute passes
			bufferCreateInfo = bufferInfoProxy;
		}

		auto result = vmaCreateBuffer( self->mAllocator, &bufferCreateInfo, &createInfo, &buffer, &allocation, &allocationInfo );

		assert( result == VK_SUCCESS ); // todo: deal with failed allocation

		// Create a new allocator - note that we assume an alignment of 256 bytes
		le_allocator_o *allocator = allocator_i.create( &allocationInfo, 256 );

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

	static auto const &le_encoder_api = ( *Registry::getApi<le_renderer_api>() ).le_command_buffer_encoder_i;
	static auto const &vk_device_i    = ( *Registry::getApi<le_backend_vk_api>() ).vk_device_i;

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

		vk::PipelineLayout             currentPipelineLayout;
		std::vector<vk::DescriptorSet> descriptorSets; // currently bound descriptorSets

		auto updateArguments = []( const vk::Device &device, const vk::DescriptorPool &descriptorPool_, const ArgumentState &argumentState_, std::vector<vk::DescriptorSet> &descriptorSets_ ) {
			// -- allocate descriptors from descriptorpool based on set layout info

			if ( argumentState_.setCount == 0 ) {
				descriptorSets_.clear();
				return;
			}

			// ----------| invariant: there are descriptorSets to allocate

			vk::DescriptorSetAllocateInfo allocateInfo;
			allocateInfo.setDescriptorPool( descriptorPool_ )
			    .setDescriptorSetCount( uint32_t( argumentState_.setCount ) )
			    .setPSetLayouts( argumentState_.layouts.data() );

			// -- allocate some descriptorSets based on current layout
			descriptorSets_ = device.allocateDescriptorSets( allocateInfo );

			// -- write data from descriptorSetData into freshly allocated DescriptorSets
			for ( size_t setId = 0; setId != argumentState_.setCount; ++setId ) {

				// FIXME: If argumentState contains invalid information (for example if an uniform has not been set yet)
				// this will lead to SEGFAULT. You must ensure that argumentState contains valid information.

				device.updateDescriptorSetWithTemplate( descriptorSets_[ setId ], argumentState_.updateTemplates[ setId ], argumentState_.setData[ setId ].data() );
			}
		};

		if ( pass.encoder ) {
			le_encoder_api.get_encoded_data( pass.encoder, &commandStream, &dataSize, &numCommands );
		} else {
			// std::cout << "WARNING: pass does not have valid encoder.";
		}

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

						// -- potentially compile and create pipeline here, based on current pass and subpass
						currentPipeline = backend_produce_pipeline( self, le_cmd->info.pso, pass, subpassIndex );

						// -- grab current pipeline layout from cache
						currentPipelineLayout = self->pipelineLayoutCache[ currentPipeline.layout_info.pipeline_layout_key ];

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
								auto const &setLayoutInfo  = self->descriptorSetLayoutCache.at( set_layout_key );

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
					updateArguments( device, descriptorPool, argumentState, descriptorSets );

					if ( argumentState.setCount > 0 ) {

						cmd.bindDescriptorSets( vk::PipelineBindPoint::eGraphics,
						                        currentPipelineLayout,
						                        0,
						                        argumentState.setCount,
						                        descriptorSets.data(),
						                        argumentState.dynamicOffsetCount,
						                        argumentState.dynamicOffsets.data() );
					}

					cmd.draw( le_cmd->info.vertexCount, le_cmd->info.instanceCount, le_cmd->info.firstVertex, le_cmd->info.firstInstance );
				} break;

				case le::CommandType::eDrawIndexed: {
					auto *le_cmd = static_cast<le::CommandDrawIndexed *>( dataIt );

					// -- update descriptorsets via template if tainted
					updateArguments( device, descriptorPool, argumentState, descriptorSets );

					if ( argumentState.setCount > 0 ) {

						cmd.bindDescriptorSets( vk::PipelineBindPoint::eGraphics,
						                        currentPipelineLayout,
						                        0,
						                        argumentState.setCount,
						                        descriptorSets.data(),
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
					// NOTE: since data for viewports *is stored inline*, we could also increase the typed pointer
					// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
					cmd.setViewport( le_cmd->info.firstViewport, le_cmd->info.viewportCount, reinterpret_cast<vk::Viewport *>( le_cmd + 1 ) );
				} break;

				case le::CommandType::eSetScissor: {
					auto *le_cmd = static_cast<le::CommandSetScissor *>( dataIt );
					// NOTE: since data for scissors *is stored inline*, we could also increase the typed pointer
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
					bindingData.range  = le_cmd->info.range;

					// if binding is in fact a dynamic binding, set the corresponding dynamic offset
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

				} // switch header.info.type

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

ISL_API_ATTR void register_le_backend_vk_api( void *api_ ) {
	auto  le_backend_vk_api_i = static_cast<le_backend_vk_api *>( api_ );
	auto &vk_backend_i        = le_backend_vk_api_i->vk_backend_i;

	vk_backend_i.create                                = backend_create;
	vk_backend_i.destroy                               = backend_destroy;
	vk_backend_i.setup                                 = backend_setup;
	vk_backend_i.create_window_surface                 = backend_create_window_surface;
	vk_backend_i.create_swapchain                      = backend_create_swapchain;
	vk_backend_i.get_num_swapchain_images              = backend_get_num_swapchain_images;
	vk_backend_i.reset_swapchain                       = backend_reset_swapchain;
	vk_backend_i.get_transient_allocators              = backend_get_transient_allocators;
	vk_backend_i.poll_frame_fence                      = backend_poll_frame_fence;
	vk_backend_i.clear_frame                           = backend_clear_frame;
	vk_backend_i.acquire_physical_resources            = backend_acquire_physical_resources;
	vk_backend_i.process_frame                         = backend_process_frame;
	vk_backend_i.dispatch_frame                        = backend_dispatch_frame;
	vk_backend_i.create_shader_module                  = backend_create_shader_module;
	vk_backend_i.update_shader_modules                 = backend_update_shader_modules;
	vk_backend_i.create_graphics_pipeline_state_object = backend_create_grapics_pipeline_state_object;
	vk_backend_i.declare_resource                      = backend_declare_resource;
	vk_backend_i.get_backbuffer_resource               = backend_get_backbuffer_resource;

	// register/update submodules inside this plugin

	register_le_device_vk_api( api_ );
	register_le_instance_vk_api( api_ );
	register_le_allocator_linear_api( api_ );

	auto &le_instance_vk_i = le_backend_vk_api_i->vk_instance_i;

	if ( le_backend_vk_api_i->cUniqueInstance != nullptr ) {
		le_instance_vk_i.post_reload_hook( le_backend_vk_api_i->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
