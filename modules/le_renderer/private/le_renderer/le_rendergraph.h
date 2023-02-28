#ifndef LE_RENDERGRAPH_H
#define LE_RENDERGRAPH_H

using ResourceField = std::bitset<LE_MAX_NUM_GRAPH_RESOURCES>; // Each bit represents a distinct resource
// ----------------------------------------------------------------------

namespace le {
using RWFlags = uint32_t;
enum class ResourceAccessFlagBits : RWFlags {
	eUndefined = 0x0,
	eRead      = 0x1 << 0,
	eWrite     = 0x1 << 1,
	eReadWrite = eRead | eWrite,
};

constexpr RWFlags operator|( ResourceAccessFlagBits const& lhs, ResourceAccessFlagBits const& rhs ) noexcept {
	return static_cast<const RWFlags>( static_cast<RWFlags>( lhs ) | static_cast<RWFlags>( rhs ) );
};

constexpr RWFlags operator|( RWFlags const& lhs, ResourceAccessFlagBits const& rhs ) noexcept {
	return static_cast<const RWFlags>( lhs | static_cast<RWFlags>( rhs ) );
};

constexpr RWFlags operator&( ResourceAccessFlagBits const& lhs, ResourceAccessFlagBits const& rhs ) noexcept {
	return static_cast<const RWFlags>( static_cast<RWFlags>( lhs ) & static_cast<RWFlags>( rhs ) );
};
} // namespace le

// ----------------------------------------------------------------------
static constexpr le::AccessFlags2 LE_ALL_READ_ACCESS_FLAGS =
    le::AccessFlagBits2::eIndirectCommandRead |
    le::AccessFlagBits2::eIndexRead |
    le::AccessFlagBits2::eVertexAttributeRead |
    le::AccessFlagBits2::eUniformRead |
    le::AccessFlagBits2::eInputAttachmentRead |
    le::AccessFlagBits2::eShaderRead |
    le::AccessFlagBits2::eColorAttachmentRead |
    le::AccessFlagBits2::eDepthStencilAttachmentRead |
    le::AccessFlagBits2::eTransferRead |
    le::AccessFlagBits2::eHostRead |
    le::AccessFlagBits2::eMemoryRead |
    le::AccessFlagBits2::eCommandPreprocessReadBitNv |
    le::AccessFlagBits2::eColorAttachmentReadNoncoherentBitExt |
    le::AccessFlagBits2::eConditionalRenderingReadBitExt |
    le::AccessFlagBits2::eAccelerationStructureReadBitKhr |
    le::AccessFlagBits2::eTransformFeedbackCounterReadBitExt |
    le::AccessFlagBits2::eFragmentDensityMapReadBitExt |
    le::AccessFlagBits2::eFragmentShadingRateAttachmentReadBitKhr |
    le::AccessFlagBits2::eShaderSampledRead |
    le::AccessFlagBits2::eShaderStorageRead |
    le::AccessFlagBits2::eVideoDecodeReadBitKhr |
    le::AccessFlagBits2::eVideoEncodeReadBitKhr |
    le::AccessFlagBits2::eInvocationMaskReadBitHuawei;

static constexpr le::AccessFlags2 LE_ALL_WRITE_ACCESS_FLAGS =
    le::AccessFlagBits2::eShaderWrite |
    le::AccessFlagBits2::eColorAttachmentWrite |
    le::AccessFlagBits2::eDepthStencilAttachmentWrite |
    le::AccessFlagBits2::eTransferWrite |
    le::AccessFlagBits2::eHostWrite |
    le::AccessFlagBits2::eMemoryWrite |
    le::AccessFlagBits2::eCommandPreprocessWriteBitNv |
    le::AccessFlagBits2::eAccelerationStructureWriteBitKhr |
    le::AccessFlagBits2::eTransformFeedbackWriteBitExt |
    le::AccessFlagBits2::eTransformFeedbackCounterWriteBitExt |
    le::AccessFlagBits2::eVideoDecodeWriteBitKhr |
    le::AccessFlagBits2::eVideoEncodeWriteBitKhr |
    le::AccessFlagBits2::eShaderStorageWrite //
    ;
static constexpr le::AccessFlags2 LE_ALL_IMAGE_IMPLIED_WRITE_ACCESS_FLAGS =
    le::AccessFlagBits2::eShaderSampledRead | //
    le::AccessFlagBits2::eShaderRead |        // shader read is a potential read/write operation, as it might imply a layout transform
    le::AccessFlagBits2::eShaderStorageRead   // this might mean a read/write in case we are accessing an image as it might imply a layout transform
    ;

// ----------------------------------------------------------------------

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

struct Node {
	ResourceField       reads               = 0;
	ResourceField       writes              = 0;
	le::RootPassesField root_nodes_affinity = 0;       // association of node with root node(s) - each bit represents a root node, if set, this pass contributes to that particular root node
	bool                is_root             = false;   // whether this node is a root node
	bool                is_contributing     = false;   // whether this node contributes to a root node
	char const*         debug_name          = nullptr; // non-owning pointer to char[256]
};

// these are some sanity checks for le_renderer_types
static_assert( sizeof( le::CommandHeader ) == sizeof( uint64_t ), "Size of le::CommandHeader must be 64bit" );

struct ExecuteCallbackInfo {
	le_renderer_api::pfn_renderpass_execute_t fn        = nullptr;
	void*                                     user_data = nullptr;
};

struct le_renderpass_o {

	le::QueueFlagBits       type         = le::QueueFlagBits{};         // Requirements for a queue to which this pass can be submitted.
	uint32_t                ref_count    = 0;                           // reference count (we're following an intrusive shared pointer pattern)
	uint64_t                id           = 0;                           // hash of name
	uint32_t                width        = 0;                           // < width  in pixels, must be identical for all attachments, default:0 means current frame.swapchainWidth
	uint32_t                height       = 0;                           // < height in pixels, must be identical for all attachments, default:0 means current frame.swapchainHeight
	le::SampleCountFlagBits sample_count = le::SampleCountFlagBits::e1; // < SampleCount for all attachments.

	uint32_t            is_root = false;      // Whether pass *must* be processed
	le::RootPassesField root_passes_affinity; // Association of this renderpass with one or more root passes that it contributes to -
	                                          // this needs to be communicated to backend, so that you may create queue submissions
	                                          // by filtering via root_passes_affinity_masks

	std::vector<le_resource_handle> resources;                  // all resources used in this pass, contains info about resource type
	std::vector<le::RWFlags>        resources_read_write_flags; // TODO: get rid of this: we can use resources_access_flags instead. read/write flags for all resources, in sync with resources
	std::vector<le::AccessFlags2>   resources_access_flags;     // first read | last write access for each resource used in this pass

	std::vector<le_image_attachment_info_t> imageAttachments;    // settings for image attachments (may be color/or depth)
	std::vector<le_img_resource_handle>     attachmentResources; // kept in sync with imageAttachments, one resource per attachment

	std::vector<le_texture_handle>       textureIds;   // imageSampler resource infos
	std::vector<le_image_sampler_info_t> textureInfos; // kept in sync with texture id: info for corresponding texture id

	le_renderer_api::pfn_renderpass_setup_t callbackSetup            = nullptr;
	void*                                   setup_callback_user_data = nullptr;
	std::vector<ExecuteCallbackInfo>        executeCallbacks;

	le_command_buffer_encoder_o* encoder = nullptr;
	char                         debugName[ 256 ];
};

// ----------------------------------------------------------------------

struct le_rendergraph_o : NoCopy, NoMove {
	std::vector<le_renderpass_o*>    passes;                     //
	std::vector<le_resource_handle>  declared_resources_id;      // | pre-declared resources (declared via module)
	std::vector<le_resource_info_t>  declared_resources_info;    // | pre-declared resources (declared via module)
	std::vector<le::RootPassesField> root_passes_affinity_masks; // vector of masks, one per distinct subgraph within the rendergraph,
	                                                             // each mask represents a filter: passes whose root_passes_affinity
	                                                             // match via OR are contributing to the distinct tree whose key it was tested against.
	                                                             // Each entry represents a distinct tree which can be submitted as a
	                                                             // separate (and resource-isolated) queue submission.
	                                                             //
	std::vector<char const*> root_debug_names;                   // not owning: pointers to debug_names for root passes held within passes, in same order as RootPassesField indices
};
#endif
