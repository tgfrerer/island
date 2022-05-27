#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

#include "le_module_loader/hash_util.h"

// Wraps an enum of `enum_name` in a struct with `struct_name` so
// that it can be opaquely passed around, then unwrapped.
#define LE_WRAP_ENUM_IN_STRUCT( enum_name, struct_name ) \
	struct struct_name {                                 \
		enum_name data;                                  \
		          operator const enum_name&() const {    \
            return data;                       \
		}                                                \
		operator enum_name&() {                          \
			return data;                                 \
		}                                                \
	}

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

static inline bool operator==( le_resource_handle_t const& lhs, le_resource_handle_t const& rhs ) noexcept {
	return lhs.handle_data == rhs.handle_data;
}

static inline bool operator!=( le_resource_handle_t const& lhs, le_resource_handle_t const& rhs ) noexcept {
	return !( lhs == rhs );
}

constexpr le_resource_handle_t LE_RESOURCE( const char* const str, const LeResourceType tp ) noexcept {
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
	inline uint64_t operator()( const le_resource_handle_t& key_ ) const noexcept {
		return key_.handle_data;
	}
};

constexpr le_resource_handle_t LE_IMG_RESOURCE( const char* const str ) noexcept {
	return LE_RESOURCE( str, LeResourceType::eImage );
}

constexpr le_resource_handle_t LE_TEX_RESOURCE( const char* const str ) noexcept {
	return LE_RESOURCE( str, LeResourceType::eTexture );
}

constexpr le_resource_handle_t LE_BUF_RESOURCE( const char* const str ) noexcept {
	return LE_RESOURCE( str, LeResourceType::eBuffer );
}

struct LeBufferWriteRegion {
	uint32_t width;
	uint32_t height;
};

enum LeRenderPassType : uint32_t {
	LE_RENDER_PASS_TYPE_UNDEFINED = 0,
	le::RenderPassType::eDraw      = 1, // << most common case, should be 0
	le::RenderPassType::eTransfer  = 2,
	le::RenderPassType::eCompute   = 3,
};

typedef uint32_t LeImageUsageFlags;
// Codegen <VkImageUsageFlagBits, LeImageUsageFlags, c>
enum LeImageUsageFlagBits : LeImageUsageFlags {
	LE_IMAGE_USAGE_TRANSFER_SRC_BIT             = 0x00000001,
	LE_IMAGE_USAGE_TRANSFER_DST_BIT             = 0x00000002,
	LE_IMAGE_USAGE_SAMPLED_BIT                  = 0x00000004,
	LE_IMAGE_USAGE_STORAGE_BIT                  = 0x00000008,
	LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT         = 0x00000010,
	LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
	LE_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT     = 0x00000040,
	LE_IMAGE_USAGE_INPUT_ATTACHMENT_BIT         = 0x00000080,
	LE_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV    = 0x00000100,
	LE_IMAGE_USAGE_FLAG_BITS_MAX_ENUM           = 0x7FFFFFFF,
};
// Codegen </VkImageUsageFlagBits>

typedef uint32_t LeBufferUsageFlags;

// Codegen <VkBufferUsageFlagBits, LeBufferUsageFlags, c>
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
	LE_BUFFER_USAGE_FLAG_BITS_MAX_ENUM            = 0x7FFFFFFF,
};
// Codegen </VkBufferUsageFlagBits>

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

LE_WRAP_ENUM_IN_STRUCT( le::ShaderType, LeShaderTypeEnum );

namespace le {

// Codegen <VkAttachmentStoreOp, uint32_t>
enum class AttachmentStoreOp : uint32_t {
	eStore      = 0,
	eDontCare   = 1,
	eBeginRange = eStore,
	eEndRange   = eDontCare,
	eMaxEnum    = 0x7FFFFFFF,
};
// Codegen </VkAttachmentStoreOp>

// Codegen <VkAttachmentLoadOp, uint32_t>
enum class AttachmentLoadOp : uint32_t {
	eLoad       = 0,
	eClear      = 1,
	eDontCare   = 2,
	eBeginRange = eLoad,
	eEndRange   = eDontCare,
	eMaxEnum    = 0x7FFFFFFF,
};
// Codegen </VkAttachmentLoadOp>

// Codegen <VkImageType, uint32_t>
enum class ImageType : uint32_t {
	e1D         = 0,
	e2D         = 1,
	e3D         = 2,
	eBeginRange = e1D,
	eEndRange   = e3D,
	eMaxEnum    = 0x7FFFFFFF,
};
// Codegen </VkImageType>

static const char* to_str( const AttachmentStoreOp& lhs ) {
	switch ( lhs ) {
	case AttachmentStoreOp::eStore:
		return "Store";
	case AttachmentStoreOp::eDontCare:
		return "DontCare";
	}
	return "";
}

static const char* to_str( const AttachmentLoadOp& lhs ) {
	switch ( lhs ) {
	case AttachmentLoadOp::eLoad:
		return "Load";
	case AttachmentLoadOp::eClear:
		return "Clear";
	case AttachmentLoadOp::eDontCare:
		return "DontCare";
	}
	return "";
}

static const char* to_str( const ImageType& lhs ) {
	switch ( lhs ) {
	case ImageType::e1D:
		return "1D";
	case ImageType::e2D:
		return "2D";
	case ImageType::e3D:
		return "3D";
	}
	return "";
}
// Codegen <VkSampleCountFlagBits, uint32_t>
enum class SampleCountFlagBits : uint32_t {
	e1               = 0x00000001,
	e2               = 0x00000002,
	e4               = 0x00000004,
	e8               = 0x00000008,
	e16              = 0x00000010,
	e32              = 0x00000020,
	e64              = 0x00000040,
	eFlagBitsMaxEnum = 0x7FFFFFFF,
};
// Codegen </VkSampleCountFlagBits>

// Codegen <VkSampleCountFlagBits, uint32_t>
enum class SampleCountFlagBits : uint32_t {
	e1               = 0x00000001,
	e2               = 0x00000002,
	e4               = 0x00000004,
	e8               = 0x00000008,
	e16              = 0x00000010,
	e32              = 0x00000020,
	e64              = 0x00000040,
	eFlagBitsMaxEnum = 0x7FFFFFFF,
};
// Codegen </VkSampleCountFlagBits>

// Codegen <VkFormat, uint32_t>
enum class Format : uint32_t {
	eUndefined                               = 0,
	eR4G4UnormPack8                          = 1,
	eR4G4B4A4UnormPack16                     = 2,
	eB4G4R4A4UnormPack16                     = 3,
	eR5G6B5UnormPack16                       = 4,
	eB5G6R5UnormPack16                       = 5,
	eR5G5B5A1UnormPack16                     = 6,
	eB5G5R5A1UnormPack16                     = 7,
	eA1R5G5B5UnormPack16                     = 8,
	eR8Unorm                                 = 9,
	eR8Snorm                                 = 10,
	eR8Uscaled                               = 11,
	eR8Sscaled                               = 12,
	eR8Uint                                  = 13,
	eR8Sint                                  = 14,
	eR8Srgb                                  = 15,
	eR8G8Unorm                               = 16,
	eR8G8Snorm                               = 17,
	eR8G8Uscaled                             = 18,
	eR8G8Sscaled                             = 19,
	eR8G8Uint                                = 20,
	eR8G8Sint                                = 21,
	eR8G8Srgb                                = 22,
	eR8G8B8Unorm                             = 23,
	eR8G8B8Snorm                             = 24,
	eR8G8B8Uscaled                           = 25,
	eR8G8B8Sscaled                           = 26,
	eR8G8B8Uint                              = 27,
	eR8G8B8Sint                              = 28,
	eR8G8B8Srgb                              = 29,
	eB8G8R8Unorm                             = 30,
	eB8G8R8Snorm                             = 31,
	eB8G8R8Uscaled                           = 32,
	eB8G8R8Sscaled                           = 33,
	eB8G8R8Uint                              = 34,
	eB8G8R8Sint                              = 35,
	eB8G8R8Srgb                              = 36,
	eR8G8B8A8Unorm                           = 37,
	eR8G8B8A8Snorm                           = 38,
	eR8G8B8A8Uscaled                         = 39,
	eR8G8B8A8Sscaled                         = 40,
	eR8G8B8A8Uint                            = 41,
	eR8G8B8A8Sint                            = 42,
	eR8G8B8A8Srgb                            = 43,
	eB8G8R8A8Unorm                           = 44,
	eB8G8R8A8Snorm                           = 45,
	eB8G8R8A8Uscaled                         = 46,
	eB8G8R8A8Sscaled                         = 47,
	eB8G8R8A8Uint                            = 48,
	eB8G8R8A8Sint                            = 49,
	eB8G8R8A8Srgb                            = 50,
	eA8B8G8R8UnormPack32                     = 51,
	eA8B8G8R8SnormPack32                     = 52,
	eA8B8G8R8UscaledPack32                   = 53,
	eA8B8G8R8SscaledPack32                   = 54,
	eA8B8G8R8UintPack32                      = 55,
	eA8B8G8R8SintPack32                      = 56,
	eA8B8G8R8SrgbPack32                      = 57,
	eA2R10G10B10UnormPack32                  = 58,
	eA2R10G10B10SnormPack32                  = 59,
	eA2R10G10B10UscaledPack32                = 60,
	eA2R10G10B10SscaledPack32                = 61,
	eA2R10G10B10UintPack32                   = 62,
	eA2R10G10B10SintPack32                   = 63,
	eA2B10G10R10UnormPack32                  = 64,
	eA2B10G10R10SnormPack32                  = 65,
	eA2B10G10R10UscaledPack32                = 66,
	eA2B10G10R10SscaledPack32                = 67,
	eA2B10G10R10UintPack32                   = 68,
	eA2B10G10R10SintPack32                   = 69,
	eR16Unorm                                = 70,
	eR16Snorm                                = 71,
	eR16Uscaled                              = 72,
	eR16Sscaled                              = 73,
	eR16Uint                                 = 74,
	eR16Sint                                 = 75,
	eR16Sfloat                               = 76,
	eR16G16Unorm                             = 77,
	eR16G16Snorm                             = 78,
	eR16G16Uscaled                           = 79,
	eR16G16Sscaled                           = 80,
	eR16G16Uint                              = 81,
	eR16G16Sint                              = 82,
	eR16G16Sfloat                            = 83,
	eR16G16B16Unorm                          = 84,
	eR16G16B16Snorm                          = 85,
	eR16G16B16Uscaled                        = 86,
	eR16G16B16Sscaled                        = 87,
	eR16G16B16Uint                           = 88,
	eR16G16B16Sint                           = 89,
	eR16G16B16Sfloat                         = 90,
	eR16G16B16A16Unorm                       = 91,
	eR16G16B16A16Snorm                       = 92,
	eR16G16B16A16Uscaled                     = 93,
	eR16G16B16A16Sscaled                     = 94,
	eR16G16B16A16Uint                        = 95,
	eR16G16B16A16Sint                        = 96,
	eR16G16B16A16Sfloat                      = 97,
	eR32Uint                                 = 98,
	eR32Sint                                 = 99,
	eR32Sfloat                               = 100,
	eR32G32Uint                              = 101,
	eR32G32Sint                              = 102,
	eR32G32Sfloat                            = 103,
	eR32G32B32Uint                           = 104,
	eR32G32B32Sint                           = 105,
	eR32G32B32Sfloat                         = 106,
	eR32G32B32A32Uint                        = 107,
	eR32G32B32A32Sint                        = 108,
	eR32G32B32A32Sfloat                      = 109,
	eR64Uint                                 = 110,
	eR64Sint                                 = 111,
	eR64Sfloat                               = 112,
	eR64G64Uint                              = 113,
	eR64G64Sint                              = 114,
	eR64G64Sfloat                            = 115,
	eR64G64B64Uint                           = 116,
	eR64G64B64Sint                           = 117,
	eR64G64B64Sfloat                         = 118,
	eR64G64B64A64Uint                        = 119,
	eR64G64B64A64Sint                        = 120,
	eR64G64B64A64Sfloat                      = 121,
	eB10G11R11UfloatPack32                   = 122,
	eE5B9G9R9UfloatPack32                    = 123,
	eD16Unorm                                = 124,
	eX8D24UnormPack32                        = 125,
	eD32Sfloat                               = 126,
	eS8Uint                                  = 127,
	eD16UnormS8Uint                          = 128,
	eD24UnormS8Uint                          = 129,
	eD32SfloatS8Uint                         = 130,
	eBc1RgbUnormBlock                        = 131,
	eBc1RgbSrgbBlock                         = 132,
	eBc1RgbaUnormBlock                       = 133,
	eBc1RgbaSrgbBlock                        = 134,
	eBc2UnormBlock                           = 135,
	eBc2SrgbBlock                            = 136,
	eBc3UnormBlock                           = 137,
	eBc3SrgbBlock                            = 138,
	eBc4UnormBlock                           = 139,
	eBc4SnormBlock                           = 140,
	eBc5UnormBlock                           = 141,
	eBc5SnormBlock                           = 142,
	eBc6HUfloatBlock                         = 143,
	eBc6HSfloatBlock                         = 144,
	eBc7UnormBlock                           = 145,
	eBc7SrgbBlock                            = 146,
	eEtc2R8G8B8UnormBlock                    = 147,
	eEtc2R8G8B8SrgbBlock                     = 148,
	eEtc2R8G8B8A1UnormBlock                  = 149,
	eEtc2R8G8B8A1SrgbBlock                   = 150,
	eEtc2R8G8B8A8UnormBlock                  = 151,
	eEtc2R8G8B8A8SrgbBlock                   = 152,
	eEacR11UnormBlock                        = 153,
	eEacR11SnormBlock                        = 154,
	eEacR11G11UnormBlock                     = 155,
	eEacR11G11SnormBlock                     = 156,
	eAstc4x4UnormBlock                       = 157,
	eAstc4x4SrgbBlock                        = 158,
	eAstc5x4UnormBlock                       = 159,
	eAstc5x4SrgbBlock                        = 160,
	eAstc5x5UnormBlock                       = 161,
	eAstc5x5SrgbBlock                        = 162,
	eAstc6x5UnormBlock                       = 163,
	eAstc6x5SrgbBlock                        = 164,
	eAstc6x6UnormBlock                       = 165,
	eAstc6x6SrgbBlock                        = 166,
	eAstc8x5UnormBlock                       = 167,
	eAstc8x5SrgbBlock                        = 168,
	eAstc8x6UnormBlock                       = 169,
	eAstc8x6SrgbBlock                        = 170,
	eAstc8x8UnormBlock                       = 171,
	eAstc8x8SrgbBlock                        = 172,
	eAstc10x5UnormBlock                      = 173,
	eAstc10x5SrgbBlock                       = 174,
	eAstc10x6UnormBlock                      = 175,
	eAstc10x6SrgbBlock                       = 176,
	eAstc10x8UnormBlock                      = 177,
	eAstc10x8SrgbBlock                       = 178,
	eAstc10x10UnormBlock                     = 179,
	eAstc10x10SrgbBlock                      = 180,
	eAstc12x10UnormBlock                     = 181,
	eAstc12x10SrgbBlock                      = 182,
	eAstc12x12UnormBlock                     = 183,
	eAstc12x12SrgbBlock                      = 184,
	eG8B8G8R8422Unorm                        = 1000156000,
	eB8G8R8G8422Unorm                        = 1000156001,
	eG8B8R83Plane420Unorm                    = 1000156002,
	eG8B8R82Plane420Unorm                    = 1000156003,
	eG8B8R83Plane422Unorm                    = 1000156004,
	eG8B8R82Plane422Unorm                    = 1000156005,
	eG8B8R83Plane444Unorm                    = 1000156006,
	eR10x6UnormPack16                        = 1000156007,
	eR10x6G10x6Unorm2Pack16                  = 1000156008,
	eR10x6G10x6B10x6A10x6Unorm4Pack16        = 1000156009,
	eG10x6B10x6G10x6R10x6422Unorm4Pack16     = 1000156010,
	eB10x6G10x6R10x6G10x6422Unorm4Pack16     = 1000156011,
	eG10x6B10x6R10x63Plane420Unorm3Pack16    = 1000156012,
	eG10x6B10x6R10x62Plane420Unorm3Pack16    = 1000156013,
	eG10x6B10x6R10x63Plane422Unorm3Pack16    = 1000156014,
	eG10x6B10x6R10x62Plane422Unorm3Pack16    = 1000156015,
	eG10x6B10x6R10x63Plane444Unorm3Pack16    = 1000156016,
	eR12x4UnormPack16                        = 1000156017,
	eR12x4G12x4Unorm2Pack16                  = 1000156018,
	eR12x4G12x4B12x4A12x4Unorm4Pack16        = 1000156019,
	eG12x4B12x4G12x4R12x4422Unorm4Pack16     = 1000156020,
	eB12x4G12x4R12x4G12x4422Unorm4Pack16     = 1000156021,
	eG12x4B12x4R12x43Plane420Unorm3Pack16    = 1000156022,
	eG12x4B12x4R12x42Plane420Unorm3Pack16    = 1000156023,
	eG12x4B12x4R12x43Plane422Unorm3Pack16    = 1000156024,
	eG12x4B12x4R12x42Plane422Unorm3Pack16    = 1000156025,
	eG12x4B12x4R12x43Plane444Unorm3Pack16    = 1000156026,
	eG16B16G16R16422Unorm                    = 1000156027,
	eB16G16R16G16422Unorm                    = 1000156028,
	eG16B16R163Plane420Unorm                 = 1000156029,
	eG16B16R162Plane420Unorm                 = 1000156030,
	eG16B16R163Plane422Unorm                 = 1000156031,
	eG16B16R162Plane422Unorm                 = 1000156032,
	eG16B16R163Plane444Unorm                 = 1000156033,
	ePvrtc12BppUnormBlockImg                 = 1000054000,
	ePvrtc14BppUnormBlockImg                 = 1000054001,
	ePvrtc22BppUnormBlockImg                 = 1000054002,
	ePvrtc24BppUnormBlockImg                 = 1000054003,
	ePvrtc12BppSrgbBlockImg                  = 1000054004,
	ePvrtc14BppSrgbBlockImg                  = 1000054005,
	ePvrtc22BppSrgbBlockImg                  = 1000054006,
	ePvrtc24BppSrgbBlockImg                  = 1000054007,
	eG8B8G8R8422UnormKhr                     = eG8B8G8R8422Unorm,
	eB8G8R8G8422UnormKhr                     = eB8G8R8G8422Unorm,
	eG8B8R83Plane420UnormKhr                 = eG8B8R83Plane420Unorm,
	eG8B8R82Plane420UnormKhr                 = eG8B8R82Plane420Unorm,
	eG8B8R83Plane422UnormKhr                 = eG8B8R83Plane422Unorm,
	eG8B8R82Plane422UnormKhr                 = eG8B8R82Plane422Unorm,
	eG8B8R83Plane444UnormKhr                 = eG8B8R83Plane444Unorm,
	eR10x6UnormPack16Khr                     = eR10x6UnormPack16,
	eR10x6G10x6Unorm2Pack16Khr               = eR10x6G10x6Unorm2Pack16,
	eR10x6G10x6B10x6A10x6Unorm4Pack16Khr     = eR10x6G10x6B10x6A10x6Unorm4Pack16,
	eG10x6B10x6G10x6R10x6422Unorm4Pack16Khr  = eG10x6B10x6G10x6R10x6422Unorm4Pack16,
	eB10x6G10x6R10x6G10x6422Unorm4Pack16Khr  = eB10x6G10x6R10x6G10x6422Unorm4Pack16,
	eG10x6B10x6R10x63Plane420Unorm3Pack16Khr = eG10x6B10x6R10x63Plane420Unorm3Pack16,
	eG10x6B10x6R10x62Plane420Unorm3Pack16Khr = eG10x6B10x6R10x62Plane420Unorm3Pack16,
	eG10x6B10x6R10x63Plane422Unorm3Pack16Khr = eG10x6B10x6R10x63Plane422Unorm3Pack16,
	eG10x6B10x6R10x62Plane422Unorm3Pack16Khr = eG10x6B10x6R10x62Plane422Unorm3Pack16,
	eG10x6B10x6R10x63Plane444Unorm3Pack16Khr = eG10x6B10x6R10x63Plane444Unorm3Pack16,
	eR12x4UnormPack16Khr                     = eR12x4UnormPack16,
	eR12x4G12x4Unorm2Pack16Khr               = eR12x4G12x4Unorm2Pack16,
	eR12x4G12x4B12x4A12x4Unorm4Pack16Khr     = eR12x4G12x4B12x4A12x4Unorm4Pack16,
	eG12x4B12x4G12x4R12x4422Unorm4Pack16Khr  = eG12x4B12x4G12x4R12x4422Unorm4Pack16,
	eB12x4G12x4R12x4G12x4422Unorm4Pack16Khr  = eB12x4G12x4R12x4G12x4422Unorm4Pack16,
	eG12x4B12x4R12x43Plane420Unorm3Pack16Khr = eG12x4B12x4R12x43Plane420Unorm3Pack16,
	eG12x4B12x4R12x42Plane420Unorm3Pack16Khr = eG12x4B12x4R12x42Plane420Unorm3Pack16,
	eG12x4B12x4R12x43Plane422Unorm3Pack16Khr = eG12x4B12x4R12x43Plane422Unorm3Pack16,
	eG12x4B12x4R12x42Plane422Unorm3Pack16Khr = eG12x4B12x4R12x42Plane422Unorm3Pack16,
	eG12x4B12x4R12x43Plane444Unorm3Pack16Khr = eG12x4B12x4R12x43Plane444Unorm3Pack16,
	eG16B16G16R16422UnormKhr                 = eG16B16G16R16422Unorm,
	eB16G16R16G16422UnormKhr                 = eB16G16R16G16422Unorm,
	eG16B16R163Plane420UnormKhr              = eG16B16R163Plane420Unorm,
	eG16B16R162Plane420UnormKhr              = eG16B16R162Plane420Unorm,
	eG16B16R163Plane422UnormKhr              = eG16B16R163Plane422Unorm,
	eG16B16R162Plane422UnormKhr              = eG16B16R162Plane422Unorm,
	eG16B16R163Plane444UnormKhr              = eG16B16R163Plane444Unorm,
	eBeginRange                              = eUndefined,
	eEndRange                                = eAstc12x12SrgbBlock,
	eMaxEnum                                 = 0x7FFFFFFF,
};
// Codegen </VkFormat>

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

static inline constexpr bool operator==( const Extent3D& lhs, const Extent3D& rhs ) noexcept {
	return ( lhs.width == rhs.width && lhs.height == rhs.height && lhs.depth == rhs.depth );
}

static inline constexpr bool operator!=( const Extent3D& lhs, const Extent3D& rhs ) noexcept {
	return !( lhs == rhs );
}

} // namespace le

enum LeResourceAccessFlagBits : uint32_t {
	eLeResourceAccessFlagBitUndefined  = 0x0,
	eLeResourceAccessFlagBitRead       = 0x1 << 0,
	eLeResourceAccessFlagBitWrite      = 0x1 << 1,
	eLeResourceAccessFlagBitsReadWrite = eLeResourceAccessFlagBitRead | eLeResourceAccessFlagBitWrite,
};

typedef uint32_t LeAccessFlags;

struct LeTextureInfo {
	struct SamplerInfo {
		int minFilter; // enum VkFilter
		int magFilter; // enum VkFilter
		               // TODO: add clamp clamp modes etc.
	};
	struct le_image_view_info_t {
		le_resource_handle_t imageId; // le image resource id
		le::Format           format;  // leave at 0 (undefined) to use format of image referenced by `imageId`
	};
	SamplerInfo          sampler;
	le_image_view_info_t imageView;
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
struct le_image_attachment_info_t {

	static constexpr LeClearValue DefaultClearValueColor        = { { { { { 0.f, 0.f, 0.f, 0.f } } } } };
	static constexpr LeClearValue DefaultClearValueDepthStencil = { { { { { 1.f, 0 } } } } };

	le::AttachmentLoadOp  loadOp     = le::AttachmentLoadOp::eClear;  //
	le::AttachmentStoreOp storeOp    = le::AttachmentStoreOp::eStore; //
	LeClearValue          clearValue = DefaultClearValueColor;        // only used if loadOp == clear

	le_resource_handle_t resource_id{}; // (private - do not set) handle given to this attachment
};

static constexpr le_image_attachment_info_t LeDepthAttachmentInfo() {
	auto info       = le_image_attachment_info_t{};
	info.clearValue = le_image_attachment_info_t::DefaultClearValueDepthStencil;
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
	CommandHeader header = { { { CommandType::eDrawIndexed, sizeof( CommandDrawIndexed ) } } };
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
	CommandHeader header = { { { CommandType::eDraw, sizeof( CommandDraw ) } } };
	struct {
		uint32_t vertexCount;
		uint32_t instanceCount;
		uint32_t firstVertex;
		uint32_t firstInstance;
	} info;
};

struct CommandSetViewport {
	CommandHeader header = { { { CommandType::eSetViewport, sizeof( CommandSetViewport ) } } };
	struct {
		uint32_t firstViewport;
		uint32_t viewportCount;
	} info;
};

struct CommandSetScissor {
	CommandHeader header = { { { CommandType::eSetScissor, sizeof( CommandSetScissor ) } } };
	struct {
		uint32_t firstScissor;
		uint32_t scissorCount;
	} info;
};

struct CommandSetArgumentUbo {
	CommandHeader header = { { { CommandType::eSetArgumentUbo, sizeof( CommandSetArgumentUbo ) } } };
	struct {
		uint64_t             argument_name_id; // const_char_hash id of argument name
		le_resource_handle_t buffer_id;        // id of buffer that holds data
		uint32_t             offset;           // offset into buffer
		uint32_t             range;            // size of argument data in bytes
	} info;
};

struct CommandSetArgumentTexture {
	CommandHeader header = { { { CommandType::eSetArgumentTexture, sizeof( CommandSetArgumentTexture ) } } };
	struct {
		uint64_t             argument_name_id; // const_char_hash id of argument name
		le_resource_handle_t texture_id;       // texture id, hash of texture name
		uint64_t             array_index;      // argument array index (default is 0)
	} info;
};

struct CommandSetLineWidth {
	CommandHeader header = { { { CommandType::eSetLineWidth, sizeof( CommandSetLineWidth ) } } };
	struct {
		float    width;
		uint32_t reserved; // padding
	} info;
};

struct CommandBindVertexBuffers {
	CommandHeader header = { { { CommandType::eBindVertexBuffers, sizeof( CommandBindVertexBuffers ) } } };
	struct {
		uint32_t              firstBinding;
		uint32_t              bindingCount;
		le_resource_handle_t* pBuffers;
		uint64_t*             pOffsets;
	} info;
};

struct CommandBindIndexBuffer {
	CommandHeader header = { { { CommandType::eBindIndexBuffer, sizeof( CommandBindIndexBuffer ) } } };
	struct {
		le_resource_handle_t buffer; // buffer id
		uint64_t             offset;
		uint64_t             indexType;
	} info;
};

struct CommandBindPipeline {
	CommandHeader header = { { { CommandType::eBindPipeline, sizeof( CommandBindPipeline ) } } };
	struct {
		uint64_t psoHash;
	} info;
};

struct CommandWriteToBuffer {
	CommandHeader header = { { { CommandType::eWriteToBuffer, sizeof( CommandWriteToBuffer ) } } };
	struct {
		le_resource_handle_t src_buffer_id; // le buffer id of scratch buffer
		le_resource_handle_t dst_buffer_id; // which resource to write to
		uint64_t             src_offset;    // offset in scratch buffer where to find source data
		uint64_t             dst_offset;    // offset where to write to in target resource
		uint64_t             numBytes;      // number of bytes

	} info;
};

struct CommandWriteToImage {
	CommandHeader header = { { { CommandType::eWriteToImage, sizeof( CommandWriteToImage ) } } };
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
