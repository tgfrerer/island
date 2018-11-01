#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

#include "pal_api_loader/hash_util.h"

#define LE_RESOURCE_LABEL_LENGTH 32 // (no-hotreload) set to zero to disable storing name (for debug printouts) with resource handles

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

constexpr le_resource_handle_t LE_RESOURCE( const char *const str, const LeResourceType tp ) noexcept {
        le_resource_handle_t handle{};
        handle.name_hash = hash_32_fnv1a_const( str );
        handle.meta.type = tp;

#if ( LE_RESOURCE_LABEL_LENGTH > 0 )
	auto c = str;
	int  i = 0;
	while ( *c != '\0' && i < LE_RESOURCE_LABEL_LENGTH ) {
	        handle.debug_name[ i++ ] = *c++;
	}
#endif
	return handle;
}

#ifdef LE_RESOURCE_LABEL_LENGTH
#	undef LE_RESOURCE_LABEL_LENGTH
#endif

struct LeResourceHandleIdentity {
        inline uint64_t operator()( const le_resource_handle_t &key_ ) const noexcept {
                return key_.handle_data;
        }
};

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

typedef uint32_t LeImageUsageFlags;
enum LeImageUsageFlagBits : LeImageUsageFlags {
        LE_IMAGE_USAGE_TRANSFER_SRC_BIT             = 0x00000001,
        LE_IMAGE_USAGE_TRANSFER_DST_BIT             = 0x00000002,
        LE_IMAGE_USAGE_SAMPLED_BIT                  = 0x00000004,
        LE_IMAGE_USAGE_STORAGE_BIT                  = 0x00000008, // load, store, atomic
        LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT         = 0x00000010,
        LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
        LE_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT     = 0x00000040,
        LE_IMAGE_USAGE_INPUT_ATTACHMENT_BIT         = 0x00000080,
        LE_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV    = 0x00000100,
        LE_IMAGE_USAGE_FLAG_BITS_MAX_ENUM           = 0x7FFFFFFF
};

typedef uint32_t LeBufferUsageFlags;
enum LeBufferUsageFlagBits : LeBufferUsageFlags {
        LE_BUFFER_USAGE_TRANSFER_SRC_BIT              = 0x00000001,
        LE_BUFFER_USAGE_TRANSFER_DST_BIT              = 0x00000002,
        LE_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT      = 0x00000004,
        LE_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT      = 0x00000008,
        LE_BUFFER_USAGE_UNIFORM_BUFFER_BIT            = 0x00000010,
        LE_BUFFER_USAGE_STORAGE_BUFFER_BIT            = 0x00000020,
        LE_BUFFER_USAGE_INDEX_BUFFER_BIT              = 0x00000040,
        LE_BUFFER_USAGE_VERTEX_BUFFER_BIT             = 0x00000080,
        LE_BUFFER_USAGE_INDIRECT_BUFFER_BIT           = 0x00000100,
        LE_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT = 0x00000200,
        LE_BUFFER_USAGE_RAYTRACING_BIT_NVX            = 0x00000400,
        LE_BUFFER_USAGE_FLAG_BITS_MAX_ENUM            = 0x7FFFFFFF
};

namespace le {
enum class ShaderType : uint32_t {
        // no default type for shader modules, you must specify a type
        eVert        = 0x00000001,
        eTessControl = 0x00000002,
        eTessEval    = 0x00000004,
        eGeom        = 0x00000008,
        eFrag        = 0x00000010,
        eAllGraphics = 0x0000001F,
        eCompute     = 0x00000020, // max needed space to cover this enum is 6 bit
};
}
struct LeShaderTypeEnum {
        le::ShaderType data;
        operator const le::ShaderType &() const {
                return data;
        }
        operator le::ShaderType &() {
                return data;
        }
};

namespace le {

enum class AttachmentStoreOp : uint32_t {
        eStore    = 0, // << most common case
        eDontCare = 1,
};

enum AttachmentLoadOp : uint32_t {
        eLoad     = 0,
        eClear    = 1, // << most common case
        eDontCare = 2,
};

enum class ImageType : uint32_t {
        e1D = 0,
        e2D = 1,
        e3D = 2,
};

enum class ImageTiling : uint32_t {
        eOptimal = 0,
        eLinear  = 1,
};

enum class SampleCountFlagBits {
        // Codegen start <vk::SampleCountFlagBits,VkSampleCountFlagBits>
        e1  = 0x00000001,
        e2  = 0x00000002,
        e4  = 0x00000004,
        e8  = 0x00000008,
        e16 = 0x00000010,
        e32 = 0x00000020,
        e64 = 0x00000040,
        // Codegen end <vk::SampleCountFlagBits,VkSampleCountFlagBits>
};

enum class Format : uint32_t {
        eUndefined,
        eR4G4UnormPack8,
        eR4G4B4A4UnormPack16,
        eB4G4R4A4UnormPack16,
        eR5G6B5UnormPack16,
        eB5G6R5UnormPack16,
        eR5G5B5A1UnormPack16,
        eB5G5R5A1UnormPack16,
        eA1R5G5B5UnormPack16,
        eR8Unorm,
        eR8Snorm,
        eR8Uscaled,
        eR8Sscaled,
        eR8Uint,
        eR8Sint,
        eR8Srgb,
        eR8G8Unorm,
        eR8G8Snorm,
        eR8G8Uscaled,
        eR8G8Sscaled,
        eR8G8Uint,
        eR8G8Sint,
        eR8G8Srgb,
        eR8G8B8Unorm,
        eR8G8B8Snorm,
        eR8G8B8Uscaled,
        eR8G8B8Sscaled,
        eR8G8B8Uint,
        eR8G8B8Sint,
        eR8G8B8Srgb,
        eB8G8R8Unorm,
        eB8G8R8Snorm,
        eB8G8R8Uscaled,
        eB8G8R8Sscaled,
        eB8G8R8Uint,
        eB8G8R8Sint,
        eB8G8R8Srgb,
        eR8G8B8A8Unorm,
        eR8G8B8A8Snorm,
        eR8G8B8A8Uscaled,
        eR8G8B8A8Sscaled,
        eR8G8B8A8Uint,
        eR8G8B8A8Sint,
        eR8G8B8A8Srgb,
        eB8G8R8A8Unorm,
        eB8G8R8A8Snorm,
        eB8G8R8A8Uscaled,
        eB8G8R8A8Sscaled,
        eB8G8R8A8Uint,
        eB8G8R8A8Sint,
        eB8G8R8A8Srgb,
        eA8B8G8R8UnormPack32,
        eA8B8G8R8SnormPack32,
        eA8B8G8R8UscaledPack32,
        eA8B8G8R8SscaledPack32,
        eA8B8G8R8UintPack32,
        eA8B8G8R8SintPack32,
        eA8B8G8R8SrgbPack32,
        eA2R10G10B10UnormPack32,
        eA2R10G10B10SnormPack32,
        eA2R10G10B10UscaledPack32,
        eA2R10G10B10SscaledPack32,
        eA2R10G10B10UintPack32,
        eA2R10G10B10SintPack32,
        eA2B10G10R10UnormPack32,
        eA2B10G10R10SnormPack32,
        eA2B10G10R10UscaledPack32,
        eA2B10G10R10SscaledPack32,
        eA2B10G10R10UintPack32,
        eA2B10G10R10SintPack32,
        eR16Unorm,
        eR16Snorm,
        eR16Uscaled,
        eR16Sscaled,
        eR16Uint,
        eR16Sint,
        eR16Sfloat,
        eR16G16Unorm,
        eR16G16Snorm,
        eR16G16Uscaled,
        eR16G16Sscaled,
        eR16G16Uint,
        eR16G16Sint,
        eR16G16Sfloat,
        eR16G16B16Unorm,
        eR16G16B16Snorm,
        eR16G16B16Uscaled,
        eR16G16B16Sscaled,
        eR16G16B16Uint,
        eR16G16B16Sint,
        eR16G16B16Sfloat,
        eR16G16B16A16Unorm,
        eR16G16B16A16Snorm,
        eR16G16B16A16Uscaled,
        eR16G16B16A16Sscaled,
        eR16G16B16A16Uint,
        eR16G16B16A16Sint,
        eR16G16B16A16Sfloat,
        eR32Uint,
        eR32Sint,
        eR32Sfloat,
        eR32G32Uint,
        eR32G32Sint,
        eR32G32Sfloat,
        eR32G32B32Uint,
        eR32G32B32Sint,
        eR32G32B32Sfloat,
        eR32G32B32A32Uint,
        eR32G32B32A32Sint,
        eR32G32B32A32Sfloat,
        eR64Uint,
        eR64Sint,
        eR64Sfloat,
        eR64G64Uint,
        eR64G64Sint,
        eR64G64Sfloat,
        eR64G64B64Uint,
        eR64G64B64Sint,
        eR64G64B64Sfloat,
        eR64G64B64A64Uint,
        eR64G64B64A64Sint,
        eR64G64B64A64Sfloat,
        eB10G11R11UfloatPack32,
        eE5B9G9R9UfloatPack32,
        eD16Unorm,
        eX8D24UnormPack32,
        eD32Sfloat,
        eS8Uint,
        eD16UnormS8Uint,
        eD24UnormS8Uint,
        eD32SfloatS8Uint,
        eBc1RgbUnormBlock,
        eBc1RgbSrgbBlock,
        eBc1RgbaUnormBlock,
        eBc1RgbaSrgbBlock,
        eBc2UnormBlock,
        eBc2SrgbBlock,
        eBc3UnormBlock,
        eBc3SrgbBlock,
        eBc4UnormBlock,
        eBc4SnormBlock,
        eBc5UnormBlock,
        eBc5SnormBlock,
        eBc6HUfloatBlock,
        eBc6HSfloatBlock,
        eBc7UnormBlock,
        eBc7SrgbBlock,
        eEtc2R8G8B8UnormBlock,
        eEtc2R8G8B8SrgbBlock,
        eEtc2R8G8B8A1UnormBlock,
        eEtc2R8G8B8A1SrgbBlock,
        eEtc2R8G8B8A8UnormBlock,
        eEtc2R8G8B8A8SrgbBlock,
        eEacR11UnormBlock,
        eEacR11SnormBlock,
        eEacR11G11UnormBlock,
        eEacR11G11SnormBlock,
        eAstc4x4UnormBlock,
        eAstc4x4SrgbBlock,
        eAstc5x4UnormBlock,
        eAstc5x4SrgbBlock,
        eAstc5x5UnormBlock,
        eAstc5x5SrgbBlock,
        eAstc6x5UnormBlock,
        eAstc6x5SrgbBlock,
        eAstc6x6UnormBlock,
        eAstc6x6SrgbBlock,
        eAstc8x5UnormBlock,
        eAstc8x5SrgbBlock,
        eAstc8x6UnormBlock,
        eAstc8x6SrgbBlock,
        eAstc8x8UnormBlock,
        eAstc8x8SrgbBlock,
        eAstc10x5UnormBlock,
        eAstc10x5SrgbBlock,
        eAstc10x6UnormBlock,
        eAstc10x6SrgbBlock,
        eAstc10x8UnormBlock,
        eAstc10x8SrgbBlock,
        eAstc10x10UnormBlock,
        eAstc10x10SrgbBlock,
        eAstc12x10UnormBlock,
        eAstc12x10SrgbBlock,
        eAstc12x12UnormBlock,
        eAstc12x12SrgbBlock,
        eG8B8G8R8422Unorm,
        eG8B8G8R8422UnormKHR,
        eB8G8R8G8422Unorm,
        eB8G8R8G8422UnormKHR,
        eG8B8R83Plane420Unorm,
        eG8B8R83Plane420UnormKHR,
        eG8B8R82Plane420Unorm,
        eG8B8R82Plane420UnormKHR,
        eG8B8R83Plane422Unorm,
        eG8B8R83Plane422UnormKHR,
        eG8B8R82Plane422Unorm,
        eG8B8R82Plane422UnormKHR,
        eG8B8R83Plane444Unorm,
        eG8B8R83Plane444UnormKHR,
        eR10X6UnormPack16,
        eR10X6UnormPack16KHR,
        eR10X6G10X6Unorm2Pack16,
        eR10X6G10X6Unorm2Pack16KHR,
        eR10X6G10X6B10X6A10X6Unorm4Pack16,
        eR10X6G10X6B10X6A10X6Unorm4Pack16KHR,
        eG10X6B10X6G10X6R10X6422Unorm4Pack16,
        eG10X6B10X6G10X6R10X6422Unorm4Pack16KHR,
        eB10X6G10X6R10X6G10X6422Unorm4Pack16,
        eB10X6G10X6R10X6G10X6422Unorm4Pack16KHR,
        eG10X6B10X6R10X63Plane420Unorm3Pack16,
        eG10X6B10X6R10X63Plane420Unorm3Pack16KHR,
        eG10X6B10X6R10X62Plane420Unorm3Pack16,
        eG10X6B10X6R10X62Plane420Unorm3Pack16KHR,
        eG10X6B10X6R10X63Plane422Unorm3Pack16,
        eG10X6B10X6R10X63Plane422Unorm3Pack16KHR,
        eG10X6B10X6R10X62Plane422Unorm3Pack16,
        eG10X6B10X6R10X62Plane422Unorm3Pack16KHR,
        eG10X6B10X6R10X63Plane444Unorm3Pack16,
        eG10X6B10X6R10X63Plane444Unorm3Pack16KHR,
        eR12X4UnormPack16,
        eR12X4UnormPack16KHR,
        eR12X4G12X4Unorm2Pack16,
        eR12X4G12X4Unorm2Pack16KHR,
        eR12X4G12X4B12X4A12X4Unorm4Pack16,
        eR12X4G12X4B12X4A12X4Unorm4Pack16KHR,
        eG12X4B12X4G12X4R12X4422Unorm4Pack16,
        eG12X4B12X4G12X4R12X4422Unorm4Pack16KHR,
        eB12X4G12X4R12X4G12X4422Unorm4Pack16,
        eB12X4G12X4R12X4G12X4422Unorm4Pack16KHR,
        eG12X4B12X4R12X43Plane420Unorm3Pack16,
        eG12X4B12X4R12X43Plane420Unorm3Pack16KHR,
        eG12X4B12X4R12X42Plane420Unorm3Pack16,
        eG12X4B12X4R12X42Plane420Unorm3Pack16KHR,
        eG12X4B12X4R12X43Plane422Unorm3Pack16,
        eG12X4B12X4R12X43Plane422Unorm3Pack16KHR,
        eG12X4B12X4R12X42Plane422Unorm3Pack16,
        eG12X4B12X4R12X42Plane422Unorm3Pack16KHR,
        eG12X4B12X4R12X43Plane444Unorm3Pack16,
        eG12X4B12X4R12X43Plane444Unorm3Pack16KHR,
        eG16B16G16R16422Unorm,
        eG16B16G16R16422UnormKHR,
        eB16G16R16G16422Unorm,
        eB16G16R16G16422UnormKHR,
        eG16B16R163Plane420Unorm,
        eG16B16R163Plane420UnormKHR,
        eG16B16R162Plane420Unorm,
        eG16B16R162Plane420UnormKHR,
        eG16B16R163Plane422Unorm,
        eG16B16R163Plane422UnormKHR,
        eG16B16R162Plane422Unorm,
        eG16B16R162Plane422UnormKHR,
        eG16B16R163Plane444Unorm,
        eG16B16R163Plane444UnormKHR,
        ePvrtc12BppUnormBlockIMG,
        ePvrtc14BppUnormBlockIMG,
        ePvrtc22BppUnormBlockIMG,
        ePvrtc24BppUnormBlockIMG,
        ePvrtc12BppSrgbBlockIMG,
        ePvrtc14BppSrgbBlockIMG,
        ePvrtc22BppSrgbBlockIMG,
        ePvrtc24BppSrgbBlockIMG,
};

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

static inline constexpr bool operator==( const Extent3D &lhs, const Extent3D &rhs ) noexcept {
        return ( lhs.width == rhs.width && lhs.height == rhs.height && lhs.depth == rhs.depth );
}

static inline constexpr bool operator!=( const Extent3D &lhs, const Extent3D &rhs ) noexcept {
        return !( lhs == rhs );
}

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
                le::Format           format;  // leave at 0 (undefined) to use format of image referenced by `imageId`
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

// FIXME: this struct is over-specified and pierces abstraction boundaries.
struct LeImageAttachmentInfo {

        static constexpr LeClearValue DefaultClearValueColor        = {{{{{0.f, 0.f, 0.f, 0.f}}}}};
        static constexpr LeClearValue DefaultClearValueDepthStencil = {{{{{1.f, 0}}}}};

	le::AttachmentLoadOp  loadOp     = le::AttachmentLoadOp::eClear;  //
	le::AttachmentStoreOp storeOp    = le::AttachmentStoreOp::eStore; //
	LeClearValue          clearValue = DefaultClearValueColor;        // only used if loadOp == clear

	le_resource_handle_t resource_id{}; // (private - do not set) handle given to this attachment
};

static constexpr LeImageAttachmentInfo LeDepthAttachmentInfo() {
        auto info       = LeImageAttachmentInfo{};
        info.clearValue = LeImageAttachmentInfo::DefaultClearValueDepthStencil;
        return info;
}

// ----------------------------------------------------------------------
/// Specifies the intended usage for a resource.
///
/// It is the backend's responsibility to provide a concrete implementation
/// which matches the specified intent.
///
/// \brief Use ImageInfoBuilder, and BufferInfoBuilder to build `resource_info_t`
struct le_resource_info_t {

        struct Image {
                uint32_t                flags;       // creation flags
                le::ImageType           imageType;   // enum vk::ImageType
                le::Format              format;      // enum vk::Format
                le::Extent3D            extent;      //
                uint32_t                mipLevels;   //
                uint32_t                arrayLayers; //
                le::SampleCountFlagBits samples;     // enum VkSampleCountFlagBits (NOT bitfield)
                le::ImageTiling         tiling;      // enum VkImageTiling
                LeImageUsageFlags       usage;       // usage flags (LeImageUsageFlags : uint32_t)
        };

	struct Buffer {
	        uint32_t           size;
		LeBufferUsageFlags usage; // usage flags (LeBufferUsageFlags : uint32_t)
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
