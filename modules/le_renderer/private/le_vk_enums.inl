#pragma once
//
// *** THIS FILE WAS AUTO-GENERATED - DO NOT EDIT ***
//
// See ./scripts/codegen/gen_le_enums.py for details.
//

#include <stdint.h>

namespace le {

// ----------------------------------------------------------------------

using AccessFlags = uint32_t;
enum class AccessFlagBits : AccessFlags {
	eNone                                    = 0,
	eIndirectCommandRead                     = 0x00000001, // Controls coherency of indirect command reads
	eIndexRead                               = 0x00000002, // Controls coherency of index reads
	eVertexAttributeRead                     = 0x00000004, // Controls coherency of vertex attribute reads
	eUniformRead                             = 0x00000008, // Controls coherency of uniform buffer reads
	eInputAttachmentRead                     = 0x00000010, // Controls coherency of input attachment reads
	eShaderRead                              = 0x00000020, // Controls coherency of shader reads
	eShaderWrite                             = 0x00000040, // Controls coherency of shader writes
	eColorAttachmentRead                     = 0x00000080, // Controls coherency of color attachment reads
	eColorAttachmentWrite                    = 0x00000100, // Controls coherency of color attachment writes
	eDepthStencilAttachmentRead              = 0x00000200, // Controls coherency of depth/stencil attachment reads
	eDepthStencilAttachmentWrite             = 0x00000400, // Controls coherency of depth/stencil attachment writes
	eTransferRead                            = 0x00000800, // Controls coherency of transfer reads
	eTransferWrite                           = 0x00001000, // Controls coherency of transfer writes
	eHostRead                                = 0x00002000, // Controls coherency of host reads
	eHostWrite                               = 0x00004000, // Controls coherency of host writes
	eMemoryRead                              = 0x00008000, // Controls coherency of memory reads
	eMemoryWrite                             = 0x00010000, // Controls coherency of memory writes
	eCommandPreprocessReadBitNv              = 0x00020000,
	eCommandPreprocessWriteBitNv             = 0x00040000,
	eColorAttachmentReadNoncoherentBitExt    = 0x00080000,
	eConditionalRenderingReadBitExt          = 0x00100000, // read access flag for reading conditional rendering predicate
	eAccelerationStructureReadBitKhr         = 0x00200000,
	eAccelerationStructureWriteBitKhr        = 0x00400000,
	eFragmentShadingRateAttachmentReadBitKhr = 0x00800000,
	eFragmentDensityMapReadBitExt            = 0x01000000,
	eTransformFeedbackWriteBitExt            = 0x02000000,
	eTransformFeedbackCounterReadBitExt      = 0x04000000,
	eTransformFeedbackCounterWriteBitExt     = 0x08000000,
	eAccelerationStructureReadBitNv          = eAccelerationStructureReadBitKhr,
	eAccelerationStructureWriteBitNv         = eAccelerationStructureWriteBitKhr,
	eShadingRateImageReadBitNv               = eFragmentShadingRateAttachmentReadBitKhr,
	eNoneKhr                                 = eNone,
};

constexpr AccessFlags operator|( AccessFlagBits const& lhs, AccessFlagBits const& rhs ) noexcept {
	return static_cast<const AccessFlags>( static_cast<AccessFlags>( lhs ) | static_cast<AccessFlags>( rhs ) );
};

constexpr AccessFlags operator|( AccessFlags const& lhs, AccessFlagBits const& rhs ) noexcept {
	return static_cast<const AccessFlags>( lhs | static_cast<AccessFlags>( rhs ) );
};

constexpr AccessFlags operator&( AccessFlagBits const& lhs, AccessFlagBits const& rhs ) noexcept {
	return static_cast<const AccessFlags>( static_cast<AccessFlags>( lhs ) & static_cast<AccessFlags>( rhs ) );
};

// ----------------------------------------------------------------------

enum class AttachmentLoadOp : uint32_t {
	eLoad     = 0,
	eClear    = 1,
	eDontCare = 2,
	eNoneExt  = 1000400000,
};

static constexpr char const* to_str( const AttachmentLoadOp& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Load";
		case          1: return "Clear";
		case          2: return "DontCare";
		case 1000400000: return "NoneExt";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class AttachmentStoreOp : uint32_t {
	eStore    = 0,
	eDontCare = 1,
	eNone     = 1000301000,
	eNoneKhr  = eNone,
	eNoneQcom = eNone,
	eNoneExt  = eNone,
};

static constexpr char const* to_str( const AttachmentStoreOp& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Store";
		case          1: return "DontCare";
		case 1000301000: return "None";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class BlendFactor : uint32_t {
	eZero                  = 0,
	eOne                   = 1,
	eSrcColor              = 2,
	eOneMinusSrcColor      = 3,
	eDstColor              = 4,
	eOneMinusDstColor      = 5,
	eSrcAlpha              = 6,
	eOneMinusSrcAlpha      = 7,
	eDstAlpha              = 8,
	eOneMinusDstAlpha      = 9,
	eConstantColor         = 10,
	eOneMinusConstantColor = 11,
	eConstantAlpha         = 12,
	eOneMinusConstantAlpha = 13,
	eSrcAlphaSaturate      = 14,
	eSrc1Color             = 15,
	eOneMinusSrc1Color     = 16,
	eSrc1Alpha             = 17,
	eOneMinusSrc1Alpha     = 18,
};

static constexpr char const* to_str( const BlendFactor& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Zero";
		case          1: return "One";
		case          2: return "SrcColor";
		case          3: return "OneMinusSrcColor";
		case          4: return "DstColor";
		case          5: return "OneMinusDstColor";
		case          6: return "SrcAlpha";
		case          7: return "OneMinusSrcAlpha";
		case          8: return "DstAlpha";
		case          9: return "OneMinusDstAlpha";
		case         10: return "ConstantColor";
		case         11: return "OneMinusConstantColor";
		case         12: return "ConstantAlpha";
		case         13: return "OneMinusConstantAlpha";
		case         14: return "SrcAlphaSaturate";
		case         15: return "Src1Color";
		case         16: return "OneMinusSrc1Color";
		case         17: return "Src1Alpha";
		case         18: return "OneMinusSrc1Alpha";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class BlendOp : uint32_t {
	eAdd                 = 0,
	eSubtract            = 1,
	eReverseSubtract     = 2,
	eMin                 = 3,
	eMax                 = 4,
	eZeroExt             = 1000148000,
	eSrcExt              = 1000148001,
	eDstExt              = 1000148002,
	eSrcOverExt          = 1000148003,
	eDstOverExt          = 1000148004,
	eSrcInExt            = 1000148005,
	eDstInExt            = 1000148006,
	eSrcOutExt           = 1000148007,
	eDstOutExt           = 1000148008,
	eSrcAtopExt          = 1000148009,
	eDstAtopExt          = 1000148010,
	eXorExt              = 1000148011,
	eMultiplyExt         = 1000148012,
	eScreenExt           = 1000148013,
	eOverlayExt          = 1000148014,
	eDarkenExt           = 1000148015,
	eLightenExt          = 1000148016,
	eColordodgeExt       = 1000148017,
	eColorburnExt        = 1000148018,
	eHardlightExt        = 1000148019,
	eSoftlightExt        = 1000148020,
	eDifferenceExt       = 1000148021,
	eExclusionExt        = 1000148022,
	eInvertExt           = 1000148023,
	eInvertRgbExt        = 1000148024,
	eLineardodgeExt      = 1000148025,
	eLinearburnExt       = 1000148026,
	eVividlightExt       = 1000148027,
	eLinearlightExt      = 1000148028,
	ePinlightExt         = 1000148029,
	eHardmixExt          = 1000148030,
	eHslHueExt           = 1000148031,
	eHslSaturationExt    = 1000148032,
	eHslColorExt         = 1000148033,
	eHslLuminosityExt    = 1000148034,
	ePlusExt             = 1000148035,
	ePlusClampedExt      = 1000148036,
	ePlusClampedAlphaExt = 1000148037,
	ePlusDarkerExt       = 1000148038,
	eMinusExt            = 1000148039,
	eMinusClampedExt     = 1000148040,
	eContrastExt         = 1000148041,
	eInvertOvgExt        = 1000148042,
	eRedExt              = 1000148043,
	eGreenExt            = 1000148044,
	eBlueExt             = 1000148045,
};

static constexpr char const* to_str( const BlendOp& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Add";
		case          1: return "Subtract";
		case          2: return "ReverseSubtract";
		case          3: return "Min";
		case          4: return "Max";
		case 1000148000: return "ZeroExt";
		case 1000148001: return "SrcExt";
		case 1000148002: return "DstExt";
		case 1000148003: return "SrcOverExt";
		case 1000148004: return "DstOverExt";
		case 1000148005: return "SrcInExt";
		case 1000148006: return "DstInExt";
		case 1000148007: return "SrcOutExt";
		case 1000148008: return "DstOutExt";
		case 1000148009: return "SrcAtopExt";
		case 1000148010: return "DstAtopExt";
		case 1000148011: return "XorExt";
		case 1000148012: return "MultiplyExt";
		case 1000148013: return "ScreenExt";
		case 1000148014: return "OverlayExt";
		case 1000148015: return "DarkenExt";
		case 1000148016: return "LightenExt";
		case 1000148017: return "ColordodgeExt";
		case 1000148018: return "ColorburnExt";
		case 1000148019: return "HardlightExt";
		case 1000148020: return "SoftlightExt";
		case 1000148021: return "DifferenceExt";
		case 1000148022: return "ExclusionExt";
		case 1000148023: return "InvertExt";
		case 1000148024: return "InvertRgbExt";
		case 1000148025: return "LineardodgeExt";
		case 1000148026: return "LinearburnExt";
		case 1000148027: return "VividlightExt";
		case 1000148028: return "LinearlightExt";
		case 1000148029: return "PinlightExt";
		case 1000148030: return "HardmixExt";
		case 1000148031: return "HslHueExt";
		case 1000148032: return "HslSaturationExt";
		case 1000148033: return "HslColorExt";
		case 1000148034: return "HslLuminosityExt";
		case 1000148035: return "PlusExt";
		case 1000148036: return "PlusClampedExt";
		case 1000148037: return "PlusClampedAlphaExt";
		case 1000148038: return "PlusDarkerExt";
		case 1000148039: return "MinusExt";
		case 1000148040: return "MinusClampedExt";
		case 1000148041: return "ContrastExt";
		case 1000148042: return "InvertOvgExt";
		case 1000148043: return "RedExt";
		case 1000148044: return "GreenExt";
		case 1000148045: return "BlueExt";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class BorderColor : uint32_t {
	eFloatTransparentBlack = 0,
	eIntTransparentBlack   = 1,
	eFloatOpaqueBlack      = 2,
	eIntOpaqueBlack        = 3,
	eFloatOpaqueWhite      = 4,
	eIntOpaqueWhite        = 5,
	eFloatCustomExt        = 1000287003,
	eIntCustomExt          = 1000287004,
};

static constexpr char const* to_str( const BorderColor& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "FloatTransparentBlack";
		case          1: return "IntTransparentBlack";
		case          2: return "FloatOpaqueBlack";
		case          3: return "IntOpaqueBlack";
		case          4: return "FloatOpaqueWhite";
		case          5: return "IntOpaqueWhite";
		case 1000287003: return "FloatCustomExt";
		case 1000287004: return "IntCustomExt";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using BufferUsageFlags = uint32_t;
enum class BufferUsageFlagBits : BufferUsageFlags {
	eTransferSrc                                   = 0x00000001, // Can be used as a source of transfer operations
	eTransferDst                                   = 0x00000002, // Can be used as a destination of transfer operations
	eUniformTexelBuffer                            = 0x00000004, // Can be used as TBO
	eStorageTexelBuffer                            = 0x00000008, // Can be used as IBO
	eUniformBuffer                                 = 0x00000010, // Can be used as UBO
	eStorageBuffer                                 = 0x00000020, // Can be used as SSBO
	eIndexBuffer                                   = 0x00000040, // Can be used as source of fixed-function index fetch (index buffer)
	eVertexBuffer                                  = 0x00000080, // Can be used as source of fixed-function vertex fetch (VBO)
	eIndirectBuffer                                = 0x00000100, // Can be the source of indirect parameters (e.g. indirect buffer, parameter buffer)
	eConditionalRenderingBitExt                    = 0x00000200, // Specifies the buffer can be used as predicate in conditional rendering
	eShaderBindingTableBitKhr                      = 0x00000400,
	eTransformFeedbackBufferBitExt                 = 0x00000800,
	eTransformFeedbackCounterBufferBitExt          = 0x00001000,
	eVideoDecodeSrcBitKhr                          = 0x00002000,
	eVideoDecodeDstBitKhr                          = 0x00004000,
	eVideoEncodeDstBitKhr                          = 0x00008000,
	eVideoEncodeSrcBitKhr                          = 0x00010000,
	eShaderDeviceAddress                           = 0x00020000,
	eReserved18BitQcom                             = 0x00040000,
	eAccelerationStructureBuildInputReadOnlyBitKhr = 0x00080000,
	eAccelerationStructureStorageBitKhr            = 0x00100000,
	eReserved21BitAmd                              = 0x00200000,
	eReserved22BitAmd                              = 0x00400000,
	eReserved23BitNv                               = 0x00800000,
	eReserved24BitNv                               = 0x01000000,
	eRayTracingBitNv                               = eShaderBindingTableBitKhr,
	eShaderDeviceAddressBitExt                     = eShaderDeviceAddress,
	eShaderDeviceAddressBitKhr                     = eShaderDeviceAddress,
};

constexpr BufferUsageFlags operator|( BufferUsageFlagBits const& lhs, BufferUsageFlagBits const& rhs ) noexcept {
	return static_cast<const BufferUsageFlags>( static_cast<BufferUsageFlags>( lhs ) | static_cast<BufferUsageFlags>( rhs ) );
};

constexpr BufferUsageFlags operator|( BufferUsageFlags const& lhs, BufferUsageFlagBits const& rhs ) noexcept {
	return static_cast<const BufferUsageFlags>( lhs | static_cast<BufferUsageFlags>( rhs ) );
};

constexpr BufferUsageFlags operator&( BufferUsageFlagBits const& lhs, BufferUsageFlagBits const& rhs ) noexcept {
	return static_cast<const BufferUsageFlags>( static_cast<BufferUsageFlags>( lhs ) & static_cast<BufferUsageFlags>( rhs ) );
};

static constexpr char const* to_str( const BufferUsageFlagBits& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case 0x00000001: return "TransferSrc";
		case 0x00000002: return "TransferDst";
		case 0x00000004: return "UniformTexelBuffer";
		case 0x00000008: return "StorageTexelBuffer";
		case 0x00000010: return "UniformBuffer";
		case 0x00000020: return "StorageBuffer";
		case 0x00000040: return "IndexBuffer";
		case 0x00000080: return "VertexBuffer";
		case 0x00000100: return "IndirectBuffer";
		case 0x00000200: return "ConditionalRenderingBitExt";
		case 0x00000400: return "ShaderBindingTableBitKhr";
		case 0x00000800: return "TransformFeedbackBufferBitExt";
		case 0x00001000: return "TransformFeedbackCounterBufferBitExt";
		case 0x00002000: return "VideoDecodeSrcBitKhr";
		case 0x00004000: return "VideoDecodeDstBitKhr";
		case 0x00008000: return "VideoEncodeDstBitKhr";
		case 0x00010000: return "VideoEncodeSrcBitKhr";
		case 0x00020000: return "ShaderDeviceAddress";
		case 0x00040000: return "Reserved18BitQcom";
		case 0x00080000: return "AccelerationStructureBuildInputReadOnlyBitKhr";
		case 0x00100000: return "AccelerationStructureStorageBitKhr";
		case 0x00200000: return "Reserved21BitAmd";
		case 0x00400000: return "Reserved22BitAmd";
		case 0x00800000: return "Reserved23BitNv";
		case 0x01000000: return "Reserved24BitNv";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using BuildAccelerationStructureFlagsKHR = uint32_t;
enum class BuildAccelerationStructureFlagBitsKHR : BuildAccelerationStructureFlagsKHR {
	eAllowUpdateBitKhr     = 0x00000001,
	eAllowCompactionBitKhr = 0x00000002,
	ePreferFastTraceBitKhr = 0x00000004,
	ePreferFastBuildBitKhr = 0x00000008,
	eLowMemoryBitKhr       = 0x00000010,
	eMotionBitNv           = 0x00000020,
	eReserved6BitNv        = 0x00000040,
	eReserved7BitNv        = 0x00000080,
	eAllowCompactionBitNv  = eAllowCompactionBitKhr,
	eAllowUpdateBitNv      = eAllowUpdateBitKhr,
	eLowMemoryBitNv        = eLowMemoryBitKhr,
	ePreferFastBuildBitNv  = ePreferFastBuildBitKhr,
	ePreferFastTraceBitNv  = ePreferFastTraceBitKhr,
};

constexpr BuildAccelerationStructureFlagsKHR operator|( BuildAccelerationStructureFlagBitsKHR const& lhs, BuildAccelerationStructureFlagBitsKHR const& rhs ) noexcept {
	return static_cast<const BuildAccelerationStructureFlagsKHR>( static_cast<BuildAccelerationStructureFlagsKHR>( lhs ) | static_cast<BuildAccelerationStructureFlagsKHR>( rhs ) );
};

constexpr BuildAccelerationStructureFlagsKHR operator|( BuildAccelerationStructureFlagsKHR const& lhs, BuildAccelerationStructureFlagBitsKHR const& rhs ) noexcept {
	return static_cast<const BuildAccelerationStructureFlagsKHR>( lhs | static_cast<BuildAccelerationStructureFlagsKHR>( rhs ) );
};

constexpr BuildAccelerationStructureFlagsKHR operator&( BuildAccelerationStructureFlagBitsKHR const& lhs, BuildAccelerationStructureFlagBitsKHR const& rhs ) noexcept {
	return static_cast<const BuildAccelerationStructureFlagsKHR>( static_cast<BuildAccelerationStructureFlagsKHR>( lhs ) & static_cast<BuildAccelerationStructureFlagsKHR>( rhs ) );
};

static constexpr char const* to_str( const BuildAccelerationStructureFlagBitsKHR& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case 0x00000001: return "AllowUpdateBitKhr";
		case 0x00000002: return "AllowCompactionBitKhr";
		case 0x00000004: return "PreferFastTraceBitKhr";
		case 0x00000008: return "PreferFastBuildBitKhr";
		case 0x00000010: return "LowMemoryBitKhr";
		case 0x00000020: return "MotionBitNv";
		case 0x00000040: return "Reserved6BitNv";
		case 0x00000080: return "Reserved7BitNv";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using ColorComponentFlags = uint32_t;
enum class ColorComponentFlagBits : ColorComponentFlags {
	eR = 0x00000001,
	eG = 0x00000002,
	eB = 0x00000004,
	eA = 0x00000008,
};

constexpr ColorComponentFlags operator|( ColorComponentFlagBits const& lhs, ColorComponentFlagBits const& rhs ) noexcept {
	return static_cast<const ColorComponentFlags>( static_cast<ColorComponentFlags>( lhs ) | static_cast<ColorComponentFlags>( rhs ) );
};

constexpr ColorComponentFlags operator|( ColorComponentFlags const& lhs, ColorComponentFlagBits const& rhs ) noexcept {
	return static_cast<const ColorComponentFlags>( lhs | static_cast<ColorComponentFlags>( rhs ) );
};

constexpr ColorComponentFlags operator&( ColorComponentFlagBits const& lhs, ColorComponentFlagBits const& rhs ) noexcept {
	return static_cast<const ColorComponentFlags>( static_cast<ColorComponentFlags>( lhs ) & static_cast<ColorComponentFlags>( rhs ) );
};

// ----------------------------------------------------------------------

enum class CompareOp : uint32_t {
	eNever          = 0,
	eLess           = 1,
	eEqual          = 2,
	eLessOrEqual    = 3,
	eGreater        = 4,
	eNotEqual       = 5,
	eGreaterOrEqual = 6,
	eAlways         = 7,
};

static constexpr char const* to_str( const CompareOp& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Never";
		case          1: return "Less";
		case          2: return "Equal";
		case          3: return "LessOrEqual";
		case          4: return "Greater";
		case          5: return "NotEqual";
		case          6: return "GreaterOrEqual";
		case          7: return "Always";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using CullModeFlags = uint32_t;
enum class CullModeFlagBits : CullModeFlags {
	eNone         = 0,
	eFront        = 0x00000001,
	eBack         = 0x00000002,
	eFrontAndBack = 0x00000003,
};

constexpr CullModeFlags operator|( CullModeFlagBits const& lhs, CullModeFlagBits const& rhs ) noexcept {
	return static_cast<const CullModeFlags>( static_cast<CullModeFlags>( lhs ) | static_cast<CullModeFlags>( rhs ) );
};

constexpr CullModeFlags operator|( CullModeFlags const& lhs, CullModeFlagBits const& rhs ) noexcept {
	return static_cast<const CullModeFlags>( lhs | static_cast<CullModeFlags>( rhs ) );
};

constexpr CullModeFlags operator&( CullModeFlagBits const& lhs, CullModeFlagBits const& rhs ) noexcept {
	return static_cast<const CullModeFlags>( static_cast<CullModeFlags>( lhs ) & static_cast<CullModeFlags>( rhs ) );
};

// ----------------------------------------------------------------------

enum class Filter : uint32_t {
	eNearest  = 0,
	eLinear   = 1,
	eCubicImg = 1000015000,
	eCubicExt = eCubicImg,
};

static constexpr char const* to_str( const Filter& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Nearest";
		case          1: return "Linear";
		case 1000015000: return "CubicImg";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

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
	eAstc4X4UnormBlock                       = 157,
	eAstc4X4SrgbBlock                        = 158,
	eAstc5X4UnormBlock                       = 159,
	eAstc5X4SrgbBlock                        = 160,
	eAstc5X5UnormBlock                       = 161,
	eAstc5X5SrgbBlock                        = 162,
	eAstc6X5UnormBlock                       = 163,
	eAstc6X5SrgbBlock                        = 164,
	eAstc6X6UnormBlock                       = 165,
	eAstc6X6SrgbBlock                        = 166,
	eAstc8X5UnormBlock                       = 167,
	eAstc8X5SrgbBlock                        = 168,
	eAstc8X6UnormBlock                       = 169,
	eAstc8X6SrgbBlock                        = 170,
	eAstc8X8UnormBlock                       = 171,
	eAstc8X8SrgbBlock                        = 172,
	eAstc10X5UnormBlock                      = 173,
	eAstc10X5SrgbBlock                       = 174,
	eAstc10X6UnormBlock                      = 175,
	eAstc10X6SrgbBlock                       = 176,
	eAstc10X8UnormBlock                      = 177,
	eAstc10X8SrgbBlock                       = 178,
	eAstc10X10UnormBlock                     = 179,
	eAstc10X10SrgbBlock                      = 180,
	eAstc12X10UnormBlock                     = 181,
	eAstc12X10SrgbBlock                      = 182,
	eAstc12X12UnormBlock                     = 183,
	eAstc12X12SrgbBlock                      = 184,
	ePvrtc12BppUnormBlockImg                 = 1000054000,
	ePvrtc14BppUnormBlockImg                 = 1000054001,
	ePvrtc22BppUnormBlockImg                 = 1000054002,
	ePvrtc24BppUnormBlockImg                 = 1000054003,
	ePvrtc12BppSrgbBlockImg                  = 1000054004,
	ePvrtc14BppSrgbBlockImg                  = 1000054005,
	ePvrtc22BppSrgbBlockImg                  = 1000054006,
	ePvrtc24BppSrgbBlockImg                  = 1000054007,
	eAstc4X4SfloatBlock                      = 1000066000,
	eAstc5X4SfloatBlock                      = 1000066001,
	eAstc5X5SfloatBlock                      = 1000066002,
	eAstc6X5SfloatBlock                      = 1000066003,
	eAstc6X6SfloatBlock                      = 1000066004,
	eAstc8X5SfloatBlock                      = 1000066005,
	eAstc8X6SfloatBlock                      = 1000066006,
	eAstc8X8SfloatBlock                      = 1000066007,
	eAstc10X5SfloatBlock                     = 1000066008,
	eAstc10X6SfloatBlock                     = 1000066009,
	eAstc10X8SfloatBlock                     = 1000066010,
	eAstc10X10SfloatBlock                    = 1000066011,
	eAstc12X10SfloatBlock                    = 1000066012,
	eAstc12X12SfloatBlock                    = 1000066013,
	eG8B8G8R8422Unorm                        = 1000156000,
	eB8G8R8G8422Unorm                        = 1000156001,
	eG8B8R83Plane420Unorm                    = 1000156002,
	eG8B8R82Plane420Unorm                    = 1000156003,
	eG8B8R83Plane422Unorm                    = 1000156004,
	eG8B8R82Plane422Unorm                    = 1000156005,
	eG8B8R83Plane444Unorm                    = 1000156006,
	eR10X6UnormPack16                        = 1000156007,
	eR10X6G10X6Unorm2Pack16                  = 1000156008,
	eR10X6G10X6B10X6A10X6Unorm4Pack16        = 1000156009,
	eG10X6B10X6G10X6R10X6422Unorm4Pack16     = 1000156010,
	eB10X6G10X6R10X6G10X6422Unorm4Pack16     = 1000156011,
	eG10X6B10X6R10X63Plane420Unorm3Pack16    = 1000156012,
	eG10X6B10X6R10X62Plane420Unorm3Pack16    = 1000156013,
	eG10X6B10X6R10X63Plane422Unorm3Pack16    = 1000156014,
	eG10X6B10X6R10X62Plane422Unorm3Pack16    = 1000156015,
	eG10X6B10X6R10X63Plane444Unorm3Pack16    = 1000156016,
	eR12X4UnormPack16                        = 1000156017,
	eR12X4G12X4Unorm2Pack16                  = 1000156018,
	eR12X4G12X4B12X4A12X4Unorm4Pack16        = 1000156019,
	eG12X4B12X4G12X4R12X4422Unorm4Pack16     = 1000156020,
	eB12X4G12X4R12X4G12X4422Unorm4Pack16     = 1000156021,
	eG12X4B12X4R12X43Plane420Unorm3Pack16    = 1000156022,
	eG12X4B12X4R12X42Plane420Unorm3Pack16    = 1000156023,
	eG12X4B12X4R12X43Plane422Unorm3Pack16    = 1000156024,
	eG12X4B12X4R12X42Plane422Unorm3Pack16    = 1000156025,
	eG12X4B12X4R12X43Plane444Unorm3Pack16    = 1000156026,
	eG16B16G16R16422Unorm                    = 1000156027,
	eB16G16R16G16422Unorm                    = 1000156028,
	eG16B16R163Plane420Unorm                 = 1000156029,
	eG16B16R162Plane420Unorm                 = 1000156030,
	eG16B16R163Plane422Unorm                 = 1000156031,
	eG16B16R162Plane422Unorm                 = 1000156032,
	eG16B16R163Plane444Unorm                 = 1000156033,
	eAstc3X3X3UnormBlockExt                  = 1000288000,
	eAstc3X3X3SrgbBlockExt                   = 1000288001,
	eAstc3X3X3SfloatBlockExt                 = 1000288002,
	eAstc4X3X3UnormBlockExt                  = 1000288003,
	eAstc4X3X3SrgbBlockExt                   = 1000288004,
	eAstc4X3X3SfloatBlockExt                 = 1000288005,
	eAstc4X4X3UnormBlockExt                  = 1000288006,
	eAstc4X4X3SrgbBlockExt                   = 1000288007,
	eAstc4X4X3SfloatBlockExt                 = 1000288008,
	eAstc4X4X4UnormBlockExt                  = 1000288009,
	eAstc4X4X4SrgbBlockExt                   = 1000288010,
	eAstc4X4X4SfloatBlockExt                 = 1000288011,
	eAstc5X4X4UnormBlockExt                  = 1000288012,
	eAstc5X4X4SrgbBlockExt                   = 1000288013,
	eAstc5X4X4SfloatBlockExt                 = 1000288014,
	eAstc5X5X4UnormBlockExt                  = 1000288015,
	eAstc5X5X4SrgbBlockExt                   = 1000288016,
	eAstc5X5X4SfloatBlockExt                 = 1000288017,
	eAstc5X5X5UnormBlockExt                  = 1000288018,
	eAstc5X5X5SrgbBlockExt                   = 1000288019,
	eAstc5X5X5SfloatBlockExt                 = 1000288020,
	eAstc6X5X5UnormBlockExt                  = 1000288021,
	eAstc6X5X5SrgbBlockExt                   = 1000288022,
	eAstc6X5X5SfloatBlockExt                 = 1000288023,
	eAstc6X6X5UnormBlockExt                  = 1000288024,
	eAstc6X6X5SrgbBlockExt                   = 1000288025,
	eAstc6X6X5SfloatBlockExt                 = 1000288026,
	eAstc6X6X6UnormBlockExt                  = 1000288027,
	eAstc6X6X6SrgbBlockExt                   = 1000288028,
	eAstc6X6X6SfloatBlockExt                 = 1000288029,
	eG8B8R82Plane444Unorm                    = 1000330000,
	eG10X6B10X6R10X62Plane444Unorm3Pack16    = 1000330001,
	eG12X4B12X4R12X42Plane444Unorm3Pack16    = 1000330002,
	eG16B16R162Plane444Unorm                 = 1000330003,
	eA4R4G4B4UnormPack16                     = 1000340000,
	eA4B4G4R4UnormPack16                     = 1000340001,
	eA4B4G4R4UnormPack16Ext                  = eA4B4G4R4UnormPack16,
	eA4R4G4B4UnormPack16Ext                  = eA4R4G4B4UnormPack16,
	eAstc10X10SfloatBlockExt                 = eAstc10X10SfloatBlock,
	eAstc10X5SfloatBlockExt                  = eAstc10X5SfloatBlock,
	eAstc10X6SfloatBlockExt                  = eAstc10X6SfloatBlock,
	eAstc10X8SfloatBlockExt                  = eAstc10X8SfloatBlock,
	eAstc12X10SfloatBlockExt                 = eAstc12X10SfloatBlock,
	eAstc12X12SfloatBlockExt                 = eAstc12X12SfloatBlock,
	eAstc4X4SfloatBlockExt                   = eAstc4X4SfloatBlock,
	eAstc5X4SfloatBlockExt                   = eAstc5X4SfloatBlock,
	eAstc5X5SfloatBlockExt                   = eAstc5X5SfloatBlock,
	eAstc6X5SfloatBlockExt                   = eAstc6X5SfloatBlock,
	eAstc6X6SfloatBlockExt                   = eAstc6X6SfloatBlock,
	eAstc8X5SfloatBlockExt                   = eAstc8X5SfloatBlock,
	eAstc8X6SfloatBlockExt                   = eAstc8X6SfloatBlock,
	eAstc8X8SfloatBlockExt                   = eAstc8X8SfloatBlock,
	eB10X6G10X6R10X6G10X6422Unorm4Pack16Khr  = eB10X6G10X6R10X6G10X6422Unorm4Pack16,
	eB12X4G12X4R12X4G12X4422Unorm4Pack16Khr  = eB12X4G12X4R12X4G12X4422Unorm4Pack16,
	eB16G16R16G16422UnormKhr                 = eB16G16R16G16422Unorm,
	eB8G8R8G8422UnormKhr                     = eB8G8R8G8422Unorm,
	eG10X6B10X6G10X6R10X6422Unorm4Pack16Khr  = eG10X6B10X6G10X6R10X6422Unorm4Pack16,
	eG10X6B10X6R10X62Plane420Unorm3Pack16Khr = eG10X6B10X6R10X62Plane420Unorm3Pack16,
	eG10X6B10X6R10X62Plane422Unorm3Pack16Khr = eG10X6B10X6R10X62Plane422Unorm3Pack16,
	eG10X6B10X6R10X62Plane444Unorm3Pack16Ext = eG10X6B10X6R10X62Plane444Unorm3Pack16,
	eG10X6B10X6R10X63Plane420Unorm3Pack16Khr = eG10X6B10X6R10X63Plane420Unorm3Pack16,
	eG10X6B10X6R10X63Plane422Unorm3Pack16Khr = eG10X6B10X6R10X63Plane422Unorm3Pack16,
	eG10X6B10X6R10X63Plane444Unorm3Pack16Khr = eG10X6B10X6R10X63Plane444Unorm3Pack16,
	eG12X4B12X4G12X4R12X4422Unorm4Pack16Khr  = eG12X4B12X4G12X4R12X4422Unorm4Pack16,
	eG12X4B12X4R12X42Plane420Unorm3Pack16Khr = eG12X4B12X4R12X42Plane420Unorm3Pack16,
	eG12X4B12X4R12X42Plane422Unorm3Pack16Khr = eG12X4B12X4R12X42Plane422Unorm3Pack16,
	eG12X4B12X4R12X42Plane444Unorm3Pack16Ext = eG12X4B12X4R12X42Plane444Unorm3Pack16,
	eG12X4B12X4R12X43Plane420Unorm3Pack16Khr = eG12X4B12X4R12X43Plane420Unorm3Pack16,
	eG12X4B12X4R12X43Plane422Unorm3Pack16Khr = eG12X4B12X4R12X43Plane422Unorm3Pack16,
	eG12X4B12X4R12X43Plane444Unorm3Pack16Khr = eG12X4B12X4R12X43Plane444Unorm3Pack16,
	eG16B16G16R16422UnormKhr                 = eG16B16G16R16422Unorm,
	eG16B16R162Plane420UnormKhr              = eG16B16R162Plane420Unorm,
	eG16B16R162Plane422UnormKhr              = eG16B16R162Plane422Unorm,
	eG16B16R162Plane444UnormExt              = eG16B16R162Plane444Unorm,
	eG16B16R163Plane420UnormKhr              = eG16B16R163Plane420Unorm,
	eG16B16R163Plane422UnormKhr              = eG16B16R163Plane422Unorm,
	eG16B16R163Plane444UnormKhr              = eG16B16R163Plane444Unorm,
	eG8B8G8R8422UnormKhr                     = eG8B8G8R8422Unorm,
	eG8B8R82Plane420UnormKhr                 = eG8B8R82Plane420Unorm,
	eG8B8R82Plane422UnormKhr                 = eG8B8R82Plane422Unorm,
	eG8B8R82Plane444UnormExt                 = eG8B8R82Plane444Unorm,
	eG8B8R83Plane420UnormKhr                 = eG8B8R83Plane420Unorm,
	eG8B8R83Plane422UnormKhr                 = eG8B8R83Plane422Unorm,
	eG8B8R83Plane444UnormKhr                 = eG8B8R83Plane444Unorm,
	eR10X6G10X6B10X6A10X6Unorm4Pack16Khr     = eR10X6G10X6B10X6A10X6Unorm4Pack16,
	eR10X6G10X6Unorm2Pack16Khr               = eR10X6G10X6Unorm2Pack16,
	eR10X6UnormPack16Khr                     = eR10X6UnormPack16,
	eR12X4G12X4B12X4A12X4Unorm4Pack16Khr     = eR12X4G12X4B12X4A12X4Unorm4Pack16,
	eR12X4G12X4Unorm2Pack16Khr               = eR12X4G12X4Unorm2Pack16,
	eR12X4UnormPack16Khr                     = eR12X4UnormPack16,
};

static constexpr char const* to_str( const Format& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Undefined";
		case          1: return "R4G4UnormPack8";
		case          2: return "R4G4B4A4UnormPack16";
		case          3: return "B4G4R4A4UnormPack16";
		case          4: return "R5G6B5UnormPack16";
		case          5: return "B5G6R5UnormPack16";
		case          6: return "R5G5B5A1UnormPack16";
		case          7: return "B5G5R5A1UnormPack16";
		case          8: return "A1R5G5B5UnormPack16";
		case          9: return "R8Unorm";
		case         10: return "R8Snorm";
		case         11: return "R8Uscaled";
		case         12: return "R8Sscaled";
		case         13: return "R8Uint";
		case         14: return "R8Sint";
		case         15: return "R8Srgb";
		case         16: return "R8G8Unorm";
		case         17: return "R8G8Snorm";
		case         18: return "R8G8Uscaled";
		case         19: return "R8G8Sscaled";
		case         20: return "R8G8Uint";
		case         21: return "R8G8Sint";
		case         22: return "R8G8Srgb";
		case         23: return "R8G8B8Unorm";
		case         24: return "R8G8B8Snorm";
		case         25: return "R8G8B8Uscaled";
		case         26: return "R8G8B8Sscaled";
		case         27: return "R8G8B8Uint";
		case         28: return "R8G8B8Sint";
		case         29: return "R8G8B8Srgb";
		case         30: return "B8G8R8Unorm";
		case         31: return "B8G8R8Snorm";
		case         32: return "B8G8R8Uscaled";
		case         33: return "B8G8R8Sscaled";
		case         34: return "B8G8R8Uint";
		case         35: return "B8G8R8Sint";
		case         36: return "B8G8R8Srgb";
		case         37: return "R8G8B8A8Unorm";
		case         38: return "R8G8B8A8Snorm";
		case         39: return "R8G8B8A8Uscaled";
		case         40: return "R8G8B8A8Sscaled";
		case         41: return "R8G8B8A8Uint";
		case         42: return "R8G8B8A8Sint";
		case         43: return "R8G8B8A8Srgb";
		case         44: return "B8G8R8A8Unorm";
		case         45: return "B8G8R8A8Snorm";
		case         46: return "B8G8R8A8Uscaled";
		case         47: return "B8G8R8A8Sscaled";
		case         48: return "B8G8R8A8Uint";
		case         49: return "B8G8R8A8Sint";
		case         50: return "B8G8R8A8Srgb";
		case         51: return "A8B8G8R8UnormPack32";
		case         52: return "A8B8G8R8SnormPack32";
		case         53: return "A8B8G8R8UscaledPack32";
		case         54: return "A8B8G8R8SscaledPack32";
		case         55: return "A8B8G8R8UintPack32";
		case         56: return "A8B8G8R8SintPack32";
		case         57: return "A8B8G8R8SrgbPack32";
		case         58: return "A2R10G10B10UnormPack32";
		case         59: return "A2R10G10B10SnormPack32";
		case         60: return "A2R10G10B10UscaledPack32";
		case         61: return "A2R10G10B10SscaledPack32";
		case         62: return "A2R10G10B10UintPack32";
		case         63: return "A2R10G10B10SintPack32";
		case         64: return "A2B10G10R10UnormPack32";
		case         65: return "A2B10G10R10SnormPack32";
		case         66: return "A2B10G10R10UscaledPack32";
		case         67: return "A2B10G10R10SscaledPack32";
		case         68: return "A2B10G10R10UintPack32";
		case         69: return "A2B10G10R10SintPack32";
		case         70: return "R16Unorm";
		case         71: return "R16Snorm";
		case         72: return "R16Uscaled";
		case         73: return "R16Sscaled";
		case         74: return "R16Uint";
		case         75: return "R16Sint";
		case         76: return "R16Sfloat";
		case         77: return "R16G16Unorm";
		case         78: return "R16G16Snorm";
		case         79: return "R16G16Uscaled";
		case         80: return "R16G16Sscaled";
		case         81: return "R16G16Uint";
		case         82: return "R16G16Sint";
		case         83: return "R16G16Sfloat";
		case         84: return "R16G16B16Unorm";
		case         85: return "R16G16B16Snorm";
		case         86: return "R16G16B16Uscaled";
		case         87: return "R16G16B16Sscaled";
		case         88: return "R16G16B16Uint";
		case         89: return "R16G16B16Sint";
		case         90: return "R16G16B16Sfloat";
		case         91: return "R16G16B16A16Unorm";
		case         92: return "R16G16B16A16Snorm";
		case         93: return "R16G16B16A16Uscaled";
		case         94: return "R16G16B16A16Sscaled";
		case         95: return "R16G16B16A16Uint";
		case         96: return "R16G16B16A16Sint";
		case         97: return "R16G16B16A16Sfloat";
		case         98: return "R32Uint";
		case         99: return "R32Sint";
		case        100: return "R32Sfloat";
		case        101: return "R32G32Uint";
		case        102: return "R32G32Sint";
		case        103: return "R32G32Sfloat";
		case        104: return "R32G32B32Uint";
		case        105: return "R32G32B32Sint";
		case        106: return "R32G32B32Sfloat";
		case        107: return "R32G32B32A32Uint";
		case        108: return "R32G32B32A32Sint";
		case        109: return "R32G32B32A32Sfloat";
		case        110: return "R64Uint";
		case        111: return "R64Sint";
		case        112: return "R64Sfloat";
		case        113: return "R64G64Uint";
		case        114: return "R64G64Sint";
		case        115: return "R64G64Sfloat";
		case        116: return "R64G64B64Uint";
		case        117: return "R64G64B64Sint";
		case        118: return "R64G64B64Sfloat";
		case        119: return "R64G64B64A64Uint";
		case        120: return "R64G64B64A64Sint";
		case        121: return "R64G64B64A64Sfloat";
		case        122: return "B10G11R11UfloatPack32";
		case        123: return "E5B9G9R9UfloatPack32";
		case        124: return "D16Unorm";
		case        125: return "X8D24UnormPack32";
		case        126: return "D32Sfloat";
		case        127: return "S8Uint";
		case        128: return "D16UnormS8Uint";
		case        129: return "D24UnormS8Uint";
		case        130: return "D32SfloatS8Uint";
		case        131: return "Bc1RgbUnormBlock";
		case        132: return "Bc1RgbSrgbBlock";
		case        133: return "Bc1RgbaUnormBlock";
		case        134: return "Bc1RgbaSrgbBlock";
		case        135: return "Bc2UnormBlock";
		case        136: return "Bc2SrgbBlock";
		case        137: return "Bc3UnormBlock";
		case        138: return "Bc3SrgbBlock";
		case        139: return "Bc4UnormBlock";
		case        140: return "Bc4SnormBlock";
		case        141: return "Bc5UnormBlock";
		case        142: return "Bc5SnormBlock";
		case        143: return "Bc6HUfloatBlock";
		case        144: return "Bc6HSfloatBlock";
		case        145: return "Bc7UnormBlock";
		case        146: return "Bc7SrgbBlock";
		case        147: return "Etc2R8G8B8UnormBlock";
		case        148: return "Etc2R8G8B8SrgbBlock";
		case        149: return "Etc2R8G8B8A1UnormBlock";
		case        150: return "Etc2R8G8B8A1SrgbBlock";
		case        151: return "Etc2R8G8B8A8UnormBlock";
		case        152: return "Etc2R8G8B8A8SrgbBlock";
		case        153: return "EacR11UnormBlock";
		case        154: return "EacR11SnormBlock";
		case        155: return "EacR11G11UnormBlock";
		case        156: return "EacR11G11SnormBlock";
		case        157: return "Astc4X4UnormBlock";
		case        158: return "Astc4X4SrgbBlock";
		case        159: return "Astc5X4UnormBlock";
		case        160: return "Astc5X4SrgbBlock";
		case        161: return "Astc5X5UnormBlock";
		case        162: return "Astc5X5SrgbBlock";
		case        163: return "Astc6X5UnormBlock";
		case        164: return "Astc6X5SrgbBlock";
		case        165: return "Astc6X6UnormBlock";
		case        166: return "Astc6X6SrgbBlock";
		case        167: return "Astc8X5UnormBlock";
		case        168: return "Astc8X5SrgbBlock";
		case        169: return "Astc8X6UnormBlock";
		case        170: return "Astc8X6SrgbBlock";
		case        171: return "Astc8X8UnormBlock";
		case        172: return "Astc8X8SrgbBlock";
		case        173: return "Astc10X5UnormBlock";
		case        174: return "Astc10X5SrgbBlock";
		case        175: return "Astc10X6UnormBlock";
		case        176: return "Astc10X6SrgbBlock";
		case        177: return "Astc10X8UnormBlock";
		case        178: return "Astc10X8SrgbBlock";
		case        179: return "Astc10X10UnormBlock";
		case        180: return "Astc10X10SrgbBlock";
		case        181: return "Astc12X10UnormBlock";
		case        182: return "Astc12X10SrgbBlock";
		case        183: return "Astc12X12UnormBlock";
		case        184: return "Astc12X12SrgbBlock";
		case 1000054000: return "Pvrtc12BppUnormBlockImg";
		case 1000054001: return "Pvrtc14BppUnormBlockImg";
		case 1000054002: return "Pvrtc22BppUnormBlockImg";
		case 1000054003: return "Pvrtc24BppUnormBlockImg";
		case 1000054004: return "Pvrtc12BppSrgbBlockImg";
		case 1000054005: return "Pvrtc14BppSrgbBlockImg";
		case 1000054006: return "Pvrtc22BppSrgbBlockImg";
		case 1000054007: return "Pvrtc24BppSrgbBlockImg";
		case 1000066000: return "Astc4X4SfloatBlock";
		case 1000066001: return "Astc5X4SfloatBlock";
		case 1000066002: return "Astc5X5SfloatBlock";
		case 1000066003: return "Astc6X5SfloatBlock";
		case 1000066004: return "Astc6X6SfloatBlock";
		case 1000066005: return "Astc8X5SfloatBlock";
		case 1000066006: return "Astc8X6SfloatBlock";
		case 1000066007: return "Astc8X8SfloatBlock";
		case 1000066008: return "Astc10X5SfloatBlock";
		case 1000066009: return "Astc10X6SfloatBlock";
		case 1000066010: return "Astc10X8SfloatBlock";
		case 1000066011: return "Astc10X10SfloatBlock";
		case 1000066012: return "Astc12X10SfloatBlock";
		case 1000066013: return "Astc12X12SfloatBlock";
		case 1000156000: return "G8B8G8R8422Unorm";
		case 1000156001: return "B8G8R8G8422Unorm";
		case 1000156002: return "G8B8R83Plane420Unorm";
		case 1000156003: return "G8B8R82Plane420Unorm";
		case 1000156004: return "G8B8R83Plane422Unorm";
		case 1000156005: return "G8B8R82Plane422Unorm";
		case 1000156006: return "G8B8R83Plane444Unorm";
		case 1000156007: return "R10X6UnormPack16";
		case 1000156008: return "R10X6G10X6Unorm2Pack16";
		case 1000156009: return "R10X6G10X6B10X6A10X6Unorm4Pack16";
		case 1000156010: return "G10X6B10X6G10X6R10X6422Unorm4Pack16";
		case 1000156011: return "B10X6G10X6R10X6G10X6422Unorm4Pack16";
		case 1000156012: return "G10X6B10X6R10X63Plane420Unorm3Pack16";
		case 1000156013: return "G10X6B10X6R10X62Plane420Unorm3Pack16";
		case 1000156014: return "G10X6B10X6R10X63Plane422Unorm3Pack16";
		case 1000156015: return "G10X6B10X6R10X62Plane422Unorm3Pack16";
		case 1000156016: return "G10X6B10X6R10X63Plane444Unorm3Pack16";
		case 1000156017: return "R12X4UnormPack16";
		case 1000156018: return "R12X4G12X4Unorm2Pack16";
		case 1000156019: return "R12X4G12X4B12X4A12X4Unorm4Pack16";
		case 1000156020: return "G12X4B12X4G12X4R12X4422Unorm4Pack16";
		case 1000156021: return "B12X4G12X4R12X4G12X4422Unorm4Pack16";
		case 1000156022: return "G12X4B12X4R12X43Plane420Unorm3Pack16";
		case 1000156023: return "G12X4B12X4R12X42Plane420Unorm3Pack16";
		case 1000156024: return "G12X4B12X4R12X43Plane422Unorm3Pack16";
		case 1000156025: return "G12X4B12X4R12X42Plane422Unorm3Pack16";
		case 1000156026: return "G12X4B12X4R12X43Plane444Unorm3Pack16";
		case 1000156027: return "G16B16G16R16422Unorm";
		case 1000156028: return "B16G16R16G16422Unorm";
		case 1000156029: return "G16B16R163Plane420Unorm";
		case 1000156030: return "G16B16R162Plane420Unorm";
		case 1000156031: return "G16B16R163Plane422Unorm";
		case 1000156032: return "G16B16R162Plane422Unorm";
		case 1000156033: return "G16B16R163Plane444Unorm";
		case 1000288000: return "Astc3X3X3UnormBlockExt";
		case 1000288001: return "Astc3X3X3SrgbBlockExt";
		case 1000288002: return "Astc3X3X3SfloatBlockExt";
		case 1000288003: return "Astc4X3X3UnormBlockExt";
		case 1000288004: return "Astc4X3X3SrgbBlockExt";
		case 1000288005: return "Astc4X3X3SfloatBlockExt";
		case 1000288006: return "Astc4X4X3UnormBlockExt";
		case 1000288007: return "Astc4X4X3SrgbBlockExt";
		case 1000288008: return "Astc4X4X3SfloatBlockExt";
		case 1000288009: return "Astc4X4X4UnormBlockExt";
		case 1000288010: return "Astc4X4X4SrgbBlockExt";
		case 1000288011: return "Astc4X4X4SfloatBlockExt";
		case 1000288012: return "Astc5X4X4UnormBlockExt";
		case 1000288013: return "Astc5X4X4SrgbBlockExt";
		case 1000288014: return "Astc5X4X4SfloatBlockExt";
		case 1000288015: return "Astc5X5X4UnormBlockExt";
		case 1000288016: return "Astc5X5X4SrgbBlockExt";
		case 1000288017: return "Astc5X5X4SfloatBlockExt";
		case 1000288018: return "Astc5X5X5UnormBlockExt";
		case 1000288019: return "Astc5X5X5SrgbBlockExt";
		case 1000288020: return "Astc5X5X5SfloatBlockExt";
		case 1000288021: return "Astc6X5X5UnormBlockExt";
		case 1000288022: return "Astc6X5X5SrgbBlockExt";
		case 1000288023: return "Astc6X5X5SfloatBlockExt";
		case 1000288024: return "Astc6X6X5UnormBlockExt";
		case 1000288025: return "Astc6X6X5SrgbBlockExt";
		case 1000288026: return "Astc6X6X5SfloatBlockExt";
		case 1000288027: return "Astc6X6X6UnormBlockExt";
		case 1000288028: return "Astc6X6X6SrgbBlockExt";
		case 1000288029: return "Astc6X6X6SfloatBlockExt";
		case 1000330000: return "G8B8R82Plane444Unorm";
		case 1000330001: return "G10X6B10X6R10X62Plane444Unorm3Pack16";
		case 1000330002: return "G12X4B12X4R12X42Plane444Unorm3Pack16";
		case 1000330003: return "G16B16R162Plane444Unorm";
		case 1000340000: return "A4R4G4B4UnormPack16";
		case 1000340001: return "A4B4G4R4UnormPack16";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class FrontFace : uint32_t {
	eCounterClockwise = 0,
	eClockwise        = 1,
};

static constexpr char const* to_str( const FrontFace& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "CounterClockwise";
		case          1: return "Clockwise";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using ImageCreateFlags = uint32_t;
enum class ImageCreateFlagBits : ImageCreateFlags {
	eSparseBinding                        = 0x00000001, // Image should support sparse backing
	eSparseResidency                      = 0x00000002, // Image should support sparse backing with partial residency
	eSparseAliased                        = 0x00000004, // Image should support constant data access to physical memory ranges mapped into multiple locations of sparse images
	eMutableFormat                        = 0x00000008, // Allows image views to have different format than the base image
	eCubeCompatible                       = 0x00000010, // Allows creating image views with cube type from the created image
	e2DArrayCompatible                    = 0x00000020, // The 3D image can be viewed as a 2D or 2D array image
	eSplitInstanceBindRegions             = 0x00000040, // Allows using VkBindImageMemoryDeviceGroupInfo::pSplitInstanceBindRegions when binding memory to the image
	eBlockTexelViewCompatible             = 0x00000080,
	eExtendedUsage                        = 0x00000100,
	eDisjoint                             = 0x00000200,
	eAlias                                = 0x00000400,
	eProtected                            = 0x00000800, // Image requires protected memory
	eSampleLocationsCompatibleDepthBitExt = 0x00001000,
	eCornerSampledBitNv                   = 0x00002000,
	eSubsampledBitExt                     = 0x00004000,
	eFragmentDensityMapOffsetBitQcom      = 0x00008000,
	eReserved16BitAmd                     = 0x00010000,
	e2DViewCompatibleBitExt               = 0x00020000, // Image is created with a layout where individual slices are capable of being used as 2D images
	eReserved18BitExt                     = 0x00040000,
	e2DArrayCompatibleBitKhr              = e2DArrayCompatible,
	eAliasBitKhr                          = eAlias,
	eBlockTexelViewCompatibleBitKhr       = eBlockTexelViewCompatible,
	eDisjointBitKhr                       = eDisjoint,
	eExtendedUsageBitKhr                  = eExtendedUsage,
	eSplitInstanceBindRegionsBitKhr       = eSplitInstanceBindRegions,
};

constexpr ImageCreateFlags operator|( ImageCreateFlagBits const& lhs, ImageCreateFlagBits const& rhs ) noexcept {
	return static_cast<const ImageCreateFlags>( static_cast<ImageCreateFlags>( lhs ) | static_cast<ImageCreateFlags>( rhs ) );
};

constexpr ImageCreateFlags operator|( ImageCreateFlags const& lhs, ImageCreateFlagBits const& rhs ) noexcept {
	return static_cast<const ImageCreateFlags>( lhs | static_cast<ImageCreateFlags>( rhs ) );
};

constexpr ImageCreateFlags operator&( ImageCreateFlagBits const& lhs, ImageCreateFlagBits const& rhs ) noexcept {
	return static_cast<const ImageCreateFlags>( static_cast<ImageCreateFlags>( lhs ) & static_cast<ImageCreateFlags>( rhs ) );
};

// ----------------------------------------------------------------------

enum class ImageTiling : uint32_t {
	eOptimal              = 0,
	eLinear               = 1,
	eDrmFormatModifierExt = 1000158000,
};

static constexpr char const* to_str( const ImageTiling& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Optimal";
		case          1: return "Linear";
		case 1000158000: return "DrmFormatModifierExt";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class ImageType : uint32_t {
	e1D = 0,
	e2D = 1,
	e3D = 2,
};

static constexpr char const* to_str( const ImageType& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "1D";
		case          1: return "2D";
		case          2: return "3D";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using ImageUsageFlags = uint32_t;
enum class ImageUsageFlagBits : ImageUsageFlags {
	eTransferSrc                         = 0x00000001, // Can be used as a source of transfer operations
	eTransferDst                         = 0x00000002, // Can be used as a destination of transfer operations
	eSampled                             = 0x00000004, // Can be sampled from (SAMPLED_IMAGE and COMBINED_IMAGE_SAMPLER descriptor types)
	eStorage                             = 0x00000008, // Can be used as storage image (STORAGE_IMAGE descriptor type)
	eColorAttachment                     = 0x00000010, // Can be used as framebuffer color attachment
	eDepthStencilAttachment              = 0x00000020, // Can be used as framebuffer depth/stencil attachment
	eTransientAttachment                 = 0x00000040, // Image data not needed outside of rendering
	eInputAttachment                     = 0x00000080, // Can be used as framebuffer input attachment
	eFragmentShadingRateAttachmentBitKhr = 0x00000100,
	eFragmentDensityMapBitExt            = 0x00000200,
	eVideoDecodeDstBitKhr                = 0x00000400,
	eVideoDecodeSrcBitKhr                = 0x00000800,
	eVideoDecodeDpbBitKhr                = 0x00001000,
	eVideoEncodeDstBitKhr                = 0x00002000,
	eVideoEncodeSrcBitKhr                = 0x00004000,
	eVideoEncodeDpbBitKhr                = 0x00008000,
	eReserved16BitQcom                   = 0x00010000,
	eReserved17BitQcom                   = 0x00020000,
	eInvocationMaskBitHuawei             = 0x00040000,
	eReserved19BitExt                    = 0x00080000,
	eReserved20BitQcom                   = 0x00100000,
	eReserved21BitQcom                   = 0x00200000,
	eReserved22BitExt                    = 0x00400000,
	eShadingRateImageBitNv               = eFragmentShadingRateAttachmentBitKhr,
};

constexpr ImageUsageFlags operator|( ImageUsageFlagBits const& lhs, ImageUsageFlagBits const& rhs ) noexcept {
	return static_cast<const ImageUsageFlags>( static_cast<ImageUsageFlags>( lhs ) | static_cast<ImageUsageFlags>( rhs ) );
};

constexpr ImageUsageFlags operator|( ImageUsageFlags const& lhs, ImageUsageFlagBits const& rhs ) noexcept {
	return static_cast<const ImageUsageFlags>( lhs | static_cast<ImageUsageFlags>( rhs ) );
};

constexpr ImageUsageFlags operator&( ImageUsageFlagBits const& lhs, ImageUsageFlagBits const& rhs ) noexcept {
	return static_cast<const ImageUsageFlags>( static_cast<ImageUsageFlags>( lhs ) & static_cast<ImageUsageFlags>( rhs ) );
};

static constexpr char const* to_str( const ImageUsageFlagBits& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case 0x00000001: return "TransferSrc";
		case 0x00000002: return "TransferDst";
		case 0x00000004: return "Sampled";
		case 0x00000008: return "Storage";
		case 0x00000010: return "ColorAttachment";
		case 0x00000020: return "DepthStencilAttachment";
		case 0x00000040: return "TransientAttachment";
		case 0x00000080: return "InputAttachment";
		case 0x00000100: return "FragmentShadingRateAttachmentBitKhr";
		case 0x00000200: return "FragmentDensityMapBitExt";
		case 0x00000400: return "VideoDecodeDstBitKhr";
		case 0x00000800: return "VideoDecodeSrcBitKhr";
		case 0x00001000: return "VideoDecodeDpbBitKhr";
		case 0x00002000: return "VideoEncodeDstBitKhr";
		case 0x00004000: return "VideoEncodeSrcBitKhr";
		case 0x00008000: return "VideoEncodeDpbBitKhr";
		case 0x00010000: return "Reserved16BitQcom";
		case 0x00020000: return "Reserved17BitQcom";
		case 0x00040000: return "InvocationMaskBitHuawei";
		case 0x00080000: return "Reserved19BitExt";
		case 0x00100000: return "Reserved20BitQcom";
		case 0x00200000: return "Reserved21BitQcom";
		case 0x00400000: return "Reserved22BitExt";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class ImageViewType : uint32_t {
	e1D        = 0,
	e2D        = 1,
	e3D        = 2,
	eCube      = 3,
	e1DArray   = 4,
	e2DArray   = 5,
	eCubeArray = 6,
};

static constexpr char const* to_str( const ImageViewType& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "1D";
		case          1: return "2D";
		case          2: return "3D";
		case          3: return "Cube";
		case          4: return "1DArray";
		case          5: return "2DArray";
		case          6: return "CubeArray";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class IndexType : uint32_t {
	eUint16   = 0,
	eUint32   = 1,
	eNoneKhr  = 1000165000,
	eUint8Ext = 1000265000,
	eNoneNv   = eNoneKhr,
};

static constexpr char const* to_str( const IndexType& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Uint16";
		case          1: return "Uint32";
		case 1000165000: return "NoneKhr";
		case 1000265000: return "Uint8Ext";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using PipelineStageFlags2 = uint64_t;
enum class PipelineStageFlagBits2 : PipelineStageFlags2 {
	eNone                                = 0,
	eTopOfPipe                           = 0x00000001ULL,
	eDrawIndirect                        = 0x00000002ULL,
	eVertexInput                         = 0x00000004ULL,
	eVertexShader                        = 0x00000008ULL,
	eTessellationControlShader           = 0x00000010ULL,
	eTessellationEvaluationShader        = 0x00000020ULL,
	eGeometryShader                      = 0x00000040ULL,
	eFragmentShader                      = 0x00000080ULL,
	eEarlyFragmentTests                  = 0x00000100ULL,
	eLateFragmentTests                   = 0x00000200ULL,
	eColorAttachmentOutput               = 0x00000400ULL,
	eComputeShader                       = 0x00000800ULL,
	eAllTransfer                         = 0x00001000ULL,
	eBottomOfPipe                        = 0x00002000ULL,
	eHost                                = 0x00004000ULL,
	eAllGraphics                         = 0x00008000ULL,
	eAllCommands                         = 0x00010000ULL,
	eCommandPreprocessBitNv              = 0x00020000ULL,
	eConditionalRenderingBitExt          = 0x00040000ULL, // A pipeline stage for conditional rendering predicate fetch
	eTaskShaderBitNv                     = 0x00080000ULL,
	eMeshShaderBitNv                     = 0x00100000ULL,
	eRayTracingShaderBitKhr              = 0x00200000ULL,
	eFragmentShadingRateAttachmentBitKhr = 0x00400000ULL,
	eFragmentDensityProcessBitExt        = 0x00800000ULL,
	eTransformFeedbackBitExt             = 0x01000000ULL,
	eAccelerationStructureBuildBitKhr    = 0x02000000ULL,
	eVideoDecodeBitKhr                   = 0x04000000ULL,
	eVideoEncodeBitKhr                   = 0x08000000ULL,
	eInvocationMaskBitHuawei             = 0x10000000000ULL,
	eIndexInput                          = 0x1000000000ULL,
	eCopy                                = 0x100000000ULL,
	eReserved387BitKhr                   = 0x10000000ULL,
	eVertexAttributeInput                = 0x2000000000ULL,
	eResolve                             = 0x200000000ULL,
	eReserved29BitNv                     = 0x20000000ULL,
	ePreRasterizationShaders             = 0x4000000000ULL,
	eBlit                                = 0x400000000ULL,
	eReserved30BitNv                     = 0x40000000ULL,
	eSubpassShadingBitHuawei             = 0x8000000000ULL,
	eClear                               = 0x800000000ULL,
	eAccelerationStructureBuildBitNv     = eAccelerationStructureBuildBitKhr,
	eAllCommandsBitKhr                   = eAllCommands,
	eAllGraphicsBitKhr                   = eAllGraphics,
	eAllTransferBitKhr                   = eAllTransfer,
	eTransferBitKhr                      = eAllTransfer,
	eTransfer                            = eAllTransferBitKhr,
	eBlitBitKhr                          = eBlit,
	eBottomOfPipeBitKhr                  = eBottomOfPipe,
	eClearBitKhr                         = eClear,
	eColorAttachmentOutputBitKhr         = eColorAttachmentOutput,
	eComputeShaderBitKhr                 = eComputeShader,
	eCopyBitKhr                          = eCopy,
	eDrawIndirectBitKhr                  = eDrawIndirect,
	eEarlyFragmentTestsBitKhr            = eEarlyFragmentTests,
	eFragmentShaderBitKhr                = eFragmentShader,
	eShadingRateImageBitNv               = eFragmentShadingRateAttachmentBitKhr,
	eGeometryShaderBitKhr                = eGeometryShader,
	eHostBitKhr                          = eHost,
	eIndexInputBitKhr                    = eIndexInput,
	eLateFragmentTestsBitKhr             = eLateFragmentTests,
	eNoneKhr                             = eNone,
	ePreRasterizationShadersBitKhr       = ePreRasterizationShaders,
	eRayTracingShaderBitNv               = eRayTracingShaderBitKhr,
	eResolveBitKhr                       = eResolve,
	eTessellationControlShaderBitKhr     = eTessellationControlShader,
	eTessellationEvaluationShaderBitKhr  = eTessellationEvaluationShader,
	eTopOfPipeBitKhr                     = eTopOfPipe,
	eVertexAttributeInputBitKhr          = eVertexAttributeInput,
	eVertexInputBitKhr                   = eVertexInput,
	eVertexShaderBitKhr                  = eVertexShader,
};

constexpr PipelineStageFlags2 operator|( PipelineStageFlagBits2 const& lhs, PipelineStageFlagBits2 const& rhs ) noexcept {
	return static_cast<const PipelineStageFlags2>( static_cast<PipelineStageFlags2>( lhs ) | static_cast<PipelineStageFlags2>( rhs ) );
};

constexpr PipelineStageFlags2 operator|( PipelineStageFlags2 const& lhs, PipelineStageFlagBits2 const& rhs ) noexcept {
	return static_cast<const PipelineStageFlags2>( lhs | static_cast<PipelineStageFlags2>( rhs ) );
};

constexpr PipelineStageFlags2 operator&( PipelineStageFlagBits2 const& lhs, PipelineStageFlagBits2 const& rhs ) noexcept {
	return static_cast<const PipelineStageFlags2>( static_cast<PipelineStageFlags2>( lhs ) & static_cast<PipelineStageFlags2>( rhs ) );
};

// ----------------------------------------------------------------------

enum class PolygonMode : uint32_t {
	eFill            = 0,
	eLine            = 1,
	ePoint           = 2,
	eFillRectangleNv = 1000153000,
};

static constexpr char const* to_str( const PolygonMode& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Fill";
		case          1: return "Line";
		case          2: return "Point";
		case 1000153000: return "FillRectangleNv";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class PrimitiveTopology : uint32_t {
	ePointList                  = 0,
	eLineList                   = 1,
	eLineStrip                  = 2,
	eTriangleList               = 3,
	eTriangleStrip              = 4,
	eTriangleFan                = 5,
	eLineListWithAdjacency      = 6,
	eLineStripWithAdjacency     = 7,
	eTriangleListWithAdjacency  = 8,
	eTriangleStripWithAdjacency = 9,
	ePatchList                  = 10,
};

static constexpr char const* to_str( const PrimitiveTopology& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "PointList";
		case          1: return "LineList";
		case          2: return "LineStrip";
		case          3: return "TriangleList";
		case          4: return "TriangleStrip";
		case          5: return "TriangleFan";
		case          6: return "LineListWithAdjacency";
		case          7: return "LineStripWithAdjacency";
		case          8: return "TriangleListWithAdjacency";
		case          9: return "TriangleStripWithAdjacency";
		case         10: return "PatchList";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using SampleCountFlags = uint32_t;
enum class SampleCountFlagBits : SampleCountFlags {
	e1  = 0x00000001, // Sample count 1 supported
	e2  = 0x00000002, // Sample count 2 supported
	e4  = 0x00000004, // Sample count 4 supported
	e8  = 0x00000008, // Sample count 8 supported
	e16 = 0x00000010, // Sample count 16 supported
	e32 = 0x00000020, // Sample count 32 supported
	e64 = 0x00000040, // Sample count 64 supported
};

constexpr SampleCountFlags operator|( SampleCountFlagBits const& lhs, SampleCountFlagBits const& rhs ) noexcept {
	return static_cast<const SampleCountFlags>( static_cast<SampleCountFlags>( lhs ) | static_cast<SampleCountFlags>( rhs ) );
};

constexpr SampleCountFlags operator|( SampleCountFlags const& lhs, SampleCountFlagBits const& rhs ) noexcept {
	return static_cast<const SampleCountFlags>( lhs | static_cast<SampleCountFlags>( rhs ) );
};

constexpr SampleCountFlags operator&( SampleCountFlagBits const& lhs, SampleCountFlagBits const& rhs ) noexcept {
	return static_cast<const SampleCountFlags>( static_cast<SampleCountFlags>( lhs ) & static_cast<SampleCountFlags>( rhs ) );
};

// ----------------------------------------------------------------------

enum class SamplerAddressMode : uint32_t {
	eRepeat               = 0,
	eMirroredRepeat       = 1,
	eClampToEdge          = 2,
	eClampToBorder        = 3,
	eMirrorClampToEdge    = 4,                  // No need to add an extnumber attribute, since this uses a core enum value
	eMirrorClampToEdgeKhr = eMirrorClampToEdge, // Alias introduced for consistency with extension suffixing rules
};

static constexpr char const* to_str( const SamplerAddressMode& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Repeat";
		case          1: return "MirroredRepeat";
		case          2: return "ClampToEdge";
		case          3: return "ClampToBorder";
		case          4: return "MirrorClampToEdge";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class SamplerMipmapMode : uint32_t {
	eNearest = 0, // Choose nearest mip level
	eLinear  = 1, // Linear filter between mip levels
};

static constexpr char const* to_str( const SamplerMipmapMode& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Nearest";
		case          1: return "Linear";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

using ShaderStageFlags = uint32_t;
enum class ShaderStageFlagBits : ShaderStageFlags {
	eVertex                  = 0x00000001,
	eTessellationControl     = 0x00000002,
	eTessellationEvaluation  = 0x00000004,
	eGeometry                = 0x00000008,
	eFragment                = 0x00000010,
	eAllGraphics             = 0x0000001F,
	eCompute                 = 0x00000020,
	eTaskBitNv               = 0x00000040,
	eMeshBitNv               = 0x00000080,
	eRaygenBitKhr            = 0x00000100,
	eAnyHitBitKhr            = 0x00000200,
	eClosestHitBitKhr        = 0x00000400,
	eMissBitKhr              = 0x00000800,
	eIntersectionBitKhr      = 0x00001000,
	eCallableBitKhr          = 0x00002000,
	eSubpassShadingBitHuawei = 0x00004000,
	eAll                     = 0x7FFFFFFF,
	eAnyHitBitNv             = eAnyHitBitKhr,
	eCallableBitNv           = eCallableBitKhr,
	eClosestHitBitNv         = eClosestHitBitKhr,
	eIntersectionBitNv       = eIntersectionBitKhr,
	eMissBitNv               = eMissBitKhr,
	eRaygenBitNv             = eRaygenBitKhr,
};

constexpr ShaderStageFlags operator|( ShaderStageFlagBits const& lhs, ShaderStageFlagBits const& rhs ) noexcept {
	return static_cast<const ShaderStageFlags>( static_cast<ShaderStageFlags>( lhs ) | static_cast<ShaderStageFlags>( rhs ) );
};

constexpr ShaderStageFlags operator|( ShaderStageFlags const& lhs, ShaderStageFlagBits const& rhs ) noexcept {
	return static_cast<const ShaderStageFlags>( lhs | static_cast<ShaderStageFlags>( rhs ) );
};

constexpr ShaderStageFlags operator&( ShaderStageFlagBits const& lhs, ShaderStageFlagBits const& rhs ) noexcept {
	return static_cast<const ShaderStageFlags>( static_cast<ShaderStageFlags>( lhs ) & static_cast<ShaderStageFlags>( rhs ) );
};

static constexpr char const* to_str( const ShaderStageFlagBits& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case 0x00000001: return "Vertex";
		case 0x00000002: return "TessellationControl";
		case 0x00000004: return "TessellationEvaluation";
		case 0x00000008: return "Geometry";
		case 0x00000010: return "Fragment";
		case 0x0000001F: return "AllGraphics";
		case 0x00000020: return "Compute";
		case 0x00000040: return "TaskBitNv";
		case 0x00000080: return "MeshBitNv";
		case 0x00000100: return "RaygenBitKhr";
		case 0x00000200: return "AnyHitBitKhr";
		case 0x00000400: return "ClosestHitBitKhr";
		case 0x00000800: return "MissBitKhr";
		case 0x00001000: return "IntersectionBitKhr";
		case 0x00002000: return "CallableBitKhr";
		case 0x00004000: return "SubpassShadingBitHuawei";
		case 0x7FFFFFFF: return "All";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

enum class StencilOp : uint32_t {
	eKeep              = 0,
	eZero              = 1,
	eReplace           = 2,
	eIncrementAndClamp = 3,
	eDecrementAndClamp = 4,
	eInvert            = 5,
	eIncrementAndWrap  = 6,
	eDecrementAndWrap  = 7,
};

static constexpr char const* to_str( const StencilOp& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
		case          0: return "Keep";
		case          1: return "Zero";
		case          2: return "Replace";
		case          3: return "IncrementAndClamp";
		case          4: return "DecrementAndClamp";
		case          5: return "Invert";
		case          6: return "IncrementAndWrap";
		case          7: return "DecrementAndWrap";
		default: return "Unknown";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

} // end namespace le
