#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

#include "pal_api_loader/hash_util.h"

#define LE_RESOURCE_LABEL_LENGTH 0 // (no-hotreload) set to zero to disable storing name (for debug printouts) with resource handles

enum class LeResourceType : uint8_t {
        eUndefined = 0,
        eBuffer,
        eImage,
        eTexture,
};

struct le_resource_handle_t {

        enum FlagBits : uint8_t {
                eIsVirtual = 1u << 0,
        };

	union Meta {
	        struct {
		        LeResourceType type;
			uint8_t        index;
			uint8_t        flags;
			uint8_t        padding;
		};
		uint32_t meta_data;
	};

	union {
		struct {
		        uint32_t name_hash;
			Meta     meta;
		};

#if ( LE_RESOURCE_LABEL_LENGTH == 0 )
		// We define an alias `debug_name`, which is the hash of the debug name
		// if the name was not stored with the resource handle.
		struct {
		        uint32_t debug_name;
			Meta     debug_meta;
		};
#endif
		uint64_t handle_data;

	}; // end union handle_data

#if ( LE_RESOURCE_LABEL_LENGTH > 0 )
	char debug_name[ LE_RESOURCE_LABEL_LENGTH ]; // FIXME: this is unsafe for reload, we cannot assume that string constants will be stored at the same location after reload...
#endif

	inline operator uint64_t() {
	        return handle_data;
	}
};

static inline bool operator==( le_resource_handle_t const &lhs, le_resource_handle_t const &rhs ) noexcept {
        return lhs.handle_data == rhs.handle_data;
}

static inline bool operator!=( le_resource_handle_t const &lhs, le_resource_handle_t const &rhs ) noexcept {
        return !( lhs == rhs );
}

struct LeResourceHandleIdentity {
        inline auto operator()( const le_resource_handle_t &key_ ) const noexcept {
                return key_.handle_data;
        }
};

constexpr le_resource_handle_t LE_RESOURCE( const char *const str, const LeResourceType tp ) noexcept {
        le_resource_handle_t handle{};
        handle.name_hash = hash_32_fnv1a_const( str );
        handle.meta.type = tp;

#if ( LE_RESOURCE_LABEL_LENGTH > 0 )
	auto   c = str;
	int i = 0;
	while ( *c != '\0' && i < LE_RESOURCE_LABEL_LENGTH ) {
	        handle.debug_name[ i++ ] = *c++;
	}
#endif
	return handle;
}

#ifdef LE_RESOURCE_LABEL_LENGTH
#	undef LE_RESOURCE_LABEL_LENGTH
#endif

constexpr le_resource_handle_t LE_IMG_RESOURCE( const char *const str ) noexcept {
        return LE_RESOURCE( str, LeResourceType::eImage );
}

constexpr le_resource_handle_t LE_TEX_RESOURCE( const char *const str ) noexcept {
        return LE_RESOURCE( str, LeResourceType::eTexture );
}

constexpr le_resource_handle_t LE_BUF_RESOURCE( const char *const str ) noexcept {
        return LE_RESOURCE( str, LeResourceType::eBuffer );
}

struct LeBufferWriteRegion {
	uint32_t width;
	uint32_t height;
};

enum LeRenderPassType : uint32_t {
        LE_RENDER_PASS_TYPE_UNDEFINED = 0,
        LE_RENDER_PASS_TYPE_DRAW      = 1, // << most common case, should be 0
        LE_RENDER_PASS_TYPE_TRANSFER  = 2,
        LE_RENDER_PASS_TYPE_COMPUTE   = 3,
};

enum LeAttachmentStoreOp : uint32_t {
        LE_ATTACHMENT_STORE_OP_STORE    = 0, // << most common case
        LE_ATTACHMENT_STORE_OP_DONTCARE = 1,
};

enum LeAttachmentLoadOp : uint32_t {
        LE_ATTACHMENT_LOAD_OP_CLEAR    = 0, // << most common case
        LE_ATTACHMENT_LOAD_OP_LOAD     = 1,
        LE_ATTACHMENT_LOAD_OP_DONTCARE = 2,
};

enum class LeShaderType : uint64_t {
        eNone        = 0, // no default type for shader modules, you must specify a type
        eVert        = 0x00000001,
        eTessControl = 0x00000002,
        eTessEval    = 0x00000004,
        eGeom        = 0x00000008,
        eFrag        = 0x00000010,
        eAllGraphics = 0x0000001F,
        eCompute     = 0x00000020, // max needed space to cover this enum is 6 bit
};

typedef int LeFormat_t; // we're declaring this as a placeholder for image format enum

namespace le {
struct Viewport {
        float x;
        float y;
        float width;
        float height;
        float minDepth;
        float maxDepth;
};

struct Rect2D {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
};
struct Extent3D {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
};

} // namespace le

enum LeAccessFlagBits : uint32_t {
        eLeAccessFlagBitUndefined  = 0x0,
        eLeAccessFlagBitRead       = 0x1 << 0,
        eLeAccessFlagBitWrite      = 0x1 << 1,
        eLeAccessFlagBitsReadWrite = eLeAccessFlagBitRead | eLeAccessFlagBitWrite,
};

typedef uint32_t LeAccessFlags;

struct LeTextureInfo {
        struct SamplerInfo {
                int minFilter; // enum VkFilter
                int magFilter; // enum VkFilter
                               // TODO: add clamp clamp modes etc.
        };
        struct ImageViewInfo {
                le_resource_handle_t imageId; // le image resource id
                int                  format;  // enum VkFormat, leave at 0 (undefined) to use format of image referenced by `imageId`
        };
        SamplerInfo   sampler;
        ImageViewInfo imageView;
};

struct LeClearColorValue {
        union {
                float    float32[ 4 ];
                int32_t  int32[ 4 ];
                uint32_t uint32[ 4 ];
        };
};

struct LeClearDepthStencilValue {
        float    depth;
        uint32_t stencil;
};

struct LeClearValue {
        union {
                LeClearColorValue        color;
                LeClearDepthStencilValue depthStencil;
        };
};

struct LeImageAttachmentInfo {

        static constexpr LeClearValue DefaultClearValueColor        = {{{{{0.f, 0.f, 0.f, 0.f}}}}};
        static constexpr LeClearValue DefaultClearValueDepthStencil = {{{{{1.f, 0}}}}};

	LeAccessFlags       access_flags = eLeAccessFlagBitWrite;        // read, write or readwrite (default is write)
	LeAttachmentLoadOp  loadOp       = LE_ATTACHMENT_LOAD_OP_CLEAR;  //
	LeAttachmentStoreOp storeOp      = LE_ATTACHMENT_STORE_OP_STORE; //
	LeClearValue        clearValue   = DefaultClearValueColor;       // only used if loadOp == clear

	LeFormat_t           format{};      // if format is not given it will be automatically derived from attached image format
	le_resource_handle_t resource_id{}; // (private - do not set) handle given to this attachment
	uint64_t             source_id{};   // (private - do not set) hash name of writer/creator renderpass

	char debugName[ 32 ]{};
};

static constexpr LeImageAttachmentInfo LeDepthAttachmentInfo(){
    auto res = LeImageAttachmentInfo();
    res.access_flags = eLeAccessFlagBitWrite;
    res.loadOp = LE_ATTACHMENT_LOAD_OP_CLEAR;
    res.storeOp = LE_ATTACHMENT_STORE_OP_STORE;
    res.clearValue = LeImageAttachmentInfo::DefaultClearValueDepthStencil;
    return res;
}

struct le_resource_info_t {

        struct Image {
                uint32_t     flags;       // creation flags
                uint32_t     imageType;   // enum vk::ImageType
                int32_t      format;      // enum vk::Format
                le::Extent3D extent;      //
                uint32_t     mipLevels;   //
                uint32_t     arrayLayers; //
                uint32_t     samples;     // enum VkSampleCountFlagBits
                uint32_t     tiling;      // enum VkImageTiling
                uint32_t     usage;       // usage flags
                uint32_t     sharingMode; // enum vkSharingMode
        };

	struct Buffer {
	        uint32_t size;
		uint32_t usage; // e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
	};

	LeResourceType type;
	union {
	        Buffer buffer;
		Image  image;
	};
};

/// \note This struct assumes a little endian machine for sorting
struct le_vertex_input_attribute_description {

        // Note that we store the log2 of the number of Bytes needed to store values of a type
        // in the least significant two bits, so that we can say: numBytes =  1 << (type & 0b11);
        enum Type : uint8_t {
                eChar   = ( 0 << 2 ) | 0,
                eUChar  = ( 1 << 2 ) | 0,
                eShort  = ( 2 << 2 ) | 1,
                eUShort = ( 3 << 2 ) | 1,
                eInt    = ( 4 << 2 ) | 2,
                eUInt   = ( 5 << 2 ) | 2,
                eHalf   = ( 6 << 2 ) | 1, // 16 bit float type
                eFloat  = ( 7 << 2 ) | 2, // 32 bit float type
        };

	union {
	        struct {
		        uint8_t  location;       /// 0..32 shader attribute location
			uint8_t  binding;        /// 0..32 binding slot
			uint16_t binding_offset; /// 0..65565 offset for this location within binding (careful: must not be larger than maxVertexInputAttributeOffset [0.0x7ff])
			Type     type;           /// base type for attribute
			uint8_t  vecsize;        /// 0..7 number of elements of base type
			uint8_t  isNormalised;   /// whether this input comes pre-normalized
		};
		uint64_t raw_data = 0;
	};
};

struct le_vertex_input_binding_description {
        enum INPUT_RATE : uint8_t {
                ePerVertex   = 0,
                ePerInstance = 1,
        };

	union {
	        struct {
		        uint8_t    binding;    /// binding slot 0..32(==MAX_ATTRIBUTE_BINDINGS)
			INPUT_RATE input_rate; /// per-vertex (0) or per-instance (1)
			uint16_t   stride;     /// per-vertex or per-instance stride in bytes (must be smaller than maxVertexInputBindingStride = [0x800])
		};
		uint32_t raw_data;
	};
};

namespace le {

enum class CommandType : uint32_t {
	eDrawIndexed,
	eDraw,
	eSetLineWidth,
	eSetViewport,
	eSetScissor,
	eSetArgumentUbo,
	eSetArgumentTexture,
	eBindIndexBuffer,
	eBindVertexBuffers,
	eBindPipeline,
	eWriteToBuffer,
	eWriteToImage,
};

struct CommandHeader {
	union {
		struct {
			CommandType type : 32; // type of recorded command
			uint64_t    size : 32; // number of bytes this command occupies in a tightly packed array
		};
		uint64_t u64all;
	} info;
};

struct CommandDrawIndexed {
	CommandHeader header = {{{CommandType::eDrawIndexed, sizeof( CommandDrawIndexed )}}};
	struct {
		uint32_t indexCount;
		uint32_t instanceCount;
		uint32_t firstIndex;
		int32_t  vertexOffset;
		uint32_t firstInstance;
		uint32_t reserved; // padding
	} info;
};

struct CommandDraw {
	CommandHeader header = {{{CommandType::eDraw, sizeof( CommandDraw )}}};
	struct {
		uint32_t vertexCount;
		uint32_t instanceCount;
		uint32_t firstVertex;
		uint32_t firstInstance;
	} info;
};

struct CommandSetViewport {
	CommandHeader header = {{{CommandType::eSetViewport, sizeof( CommandSetViewport )}}};
	struct {
		uint32_t firstViewport;
		uint32_t viewportCount;
	} info;
};

struct CommandSetScissor {
	CommandHeader header = {{{CommandType::eSetScissor, sizeof( CommandSetScissor )}}};
	struct {
		uint32_t firstScissor;
		uint32_t scissorCount;
	} info;
};

struct CommandSetArgumentUbo {
	CommandHeader header = {{{CommandType::eSetArgumentUbo, sizeof( CommandSetArgumentUbo )}}};
	struct {
	        uint64_t             argument_name_id; // const_char_hash id of argument name
		le_resource_handle_t buffer_id;        // id of buffer that holds data
		uint32_t             offset;           // offset into buffer
		uint32_t             range;            // size of argument data in bytes
	} info;
};

struct CommandSetArgumentTexture {
	CommandHeader header = {{{CommandType::eSetArgumentTexture, sizeof( CommandSetArgumentTexture )}}};
	struct {
	        uint64_t             argument_name_id; // const_char_hash id of argument name
		le_resource_handle_t texture_id;       // texture id, hash of texture name
		uint64_t             array_index;      // argument array index (default is 0)
	} info;
};

struct CommandSetLineWidth {
	CommandHeader header = {{{CommandType::eSetLineWidth, sizeof( CommandSetLineWidth )}}};
	struct {
		float    width;
		uint32_t reserved; // padding
	} info;
};

struct CommandBindVertexBuffers {
	CommandHeader header = {{{CommandType::eBindVertexBuffers, sizeof( CommandBindVertexBuffers )}}};
	struct {
	        uint32_t              firstBinding;
		uint32_t              bindingCount;
		le_resource_handle_t *pBuffers;
		uint64_t *            pOffsets;
	} info;
};

struct CommandBindIndexBuffer {
	CommandHeader header = {{{CommandType::eBindIndexBuffer, sizeof( CommandBindIndexBuffer )}}};
	struct {
	        le_resource_handle_t buffer; // buffer id
		uint64_t             offset;
		uint64_t             indexType;
	} info;
};

struct CommandBindPipeline {
	CommandHeader header = {{{CommandType::eBindPipeline, sizeof( CommandBindPipeline )}}};
	struct {
	        uint64_t psoHash;
	} info;
};

struct CommandWriteToBuffer {
	CommandHeader header = {{{CommandType::eWriteToBuffer, sizeof( CommandWriteToBuffer )}}};
	struct {
	        le_resource_handle_t src_buffer_id; // le buffer id of scratch buffer
		le_resource_handle_t dst_buffer_id; // which resource to write to
		uint64_t             src_offset;    // offset in scratch buffer where to find source data
		uint64_t             dst_offset;    // offset where to write to in target resource
		uint64_t             numBytes;      // number of bytes

	} info;
};

struct CommandWriteToImage {
	CommandHeader header = {{{CommandType::eWriteToImage, sizeof( CommandWriteToImage )}}};
	struct {
	        le_resource_handle_t src_buffer_id; // le buffer id of scratch buffer
		le_resource_handle_t dst_image_id;  // which resource to write to
		uint64_t             src_offset;    // offset in scratch buffer where to find source data
		uint64_t             numBytes;      // number of bytes
		LeBufferWriteRegion  dst_region;    // which part of the image to write to

	} info;
};

} // namespace le

#endif
