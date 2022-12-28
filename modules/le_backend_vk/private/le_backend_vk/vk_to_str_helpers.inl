#pragma once
//
// *** THIS FILE WAS AUTO-GENERATED - DO NOT EDIT ***
//
// See ./scripts/codegen/gen_vk_enum_to_str.py for details.
//

#include <stdint.h>

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_access_flag_bits2( const VkAccessFlagBits2& tp ) {
	switch ( static_cast<int64_t>( tp ) ) {
		// clang-format off
	     case 0         : return "None";
	     case 0x00000001ULL: return "IndirectCommandRead";
	     case 0x00000002ULL: return "IndexRead";
	     case 0x00000004ULL: return "VertexAttributeRead";
	     case 0x00000008ULL: return "UniformRead";
	     case 0x00000010ULL: return "InputAttachmentRead";
	     case 0x00000020ULL: return "ShaderRead";
	     case 0x00000040ULL: return "ShaderWrite";
	     case 0x00000080ULL: return "ColorAttachmentRead";
	     case 0x00000100ULL: return "ColorAttachmentWrite";
	     case 0x00000200ULL: return "DepthStencilAttachmentRead";
	     case 0x00000400ULL: return "DepthStencilAttachmentWrite";
	     case 0x00000800ULL: return "TransferRead";
	     case 0x00001000ULL: return "TransferWrite";
	     case 0x00002000ULL: return "HostRead";
	     case 0x00004000ULL: return "HostWrite";
	     case 0x00008000ULL: return "MemoryRead";
	     case 0x00010000ULL: return "MemoryWrite";
	     case 0x00020000ULL: return "CommandPreprocessReadBitNv";
	     case 0x00040000ULL: return "CommandPreprocessWriteBitNv";
	     case 0x00080000ULL: return "ColorAttachmentReadNoncoherentBitExt";
	     case 0x00100000ULL: return "ConditionalRenderingReadBitExt";
	     case 0x00200000ULL: return "AccelerationStructureReadBitKhr";
	     case 0x00400000ULL: return "AccelerationStructureWriteBitKhr";
	     case 0x00800000ULL: return "FragmentShadingRateAttachmentReadBitKhr";
	     case 0x01000000ULL: return "FragmentDensityMapReadBitExt";
	     case 0x02000000ULL: return "TransformFeedbackWriteBitExt";
	     case 0x04000000ULL: return "TransformFeedbackCounterReadBitExt";
	     case 0x08000000ULL: return "TransformFeedbackCounterWriteBitExt";
	     case 0x100000000000ULL: return "MicromapReadBitExt";
	     case 0x10000000000ULL: return "ShaderBindingTableReadBitKhr";
	     case 0x1000000000ULL: return "VideoDecodeWriteBitKhr";
	     case 0x100000000ULL: return "ShaderSampledRead";
	     case 0x200000000000ULL: return "MicromapWriteBitExt";
	     case 0x20000000000ULL: return "DescriptorBufferReadBitExt";
	     case 0x2000000000ULL: return "VideoEncodeReadBitKhr";
	     case 0x200000000ULL: return "ShaderStorageRead";
	     case 0x400000000000ULL: return "Reserved46BitExt";
	     case 0x40000000000ULL: return "OpticalFlowReadBitNv";
	     case 0x4000000000ULL: return "VideoEncodeWriteBitKhr";
	     case 0x400000000ULL: return "ShaderStorageWrite";
	     case 0x80000000000ULL: return "OpticalFlowWriteBitNv";
	     case 0x8000000000ULL: return "InvocationMaskReadBitHuawei";
	     case 0x800000000ULL: return "VideoDecodeReadBitKhr";
	          default     : return "";
		// clang-format on
	};
}

static std::string to_string_vk_access_flags2( const VkAccessFlags2& tp ) {
	uint64_t    flags = tp;
	std::string result;
	int         bit_pos = 0;
	while ( flags ) {
		if ( flags & 1 ) {
			if ( false == result.empty() ) {
				result.append( " | " );
			}
			result.append( to_str_vk_access_flag_bits2( VkAccessFlagBits2( 1ULL << bit_pos ) ) );
		}
		flags >>= 1;
		bit_pos++;
	}
	return result;
}

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_buffer_usage_flag_bits( const VkBufferUsageFlagBits& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
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
	     case 0x00200000: return "SamplerDescriptorBufferBitExt";
	     case 0x00400000: return "ResourceDescriptorBufferBitExt";
	     case 0x00800000: return "MicromapBuildInputReadOnlyBitExt";
	     case 0x01000000: return "MicromapStorageBitExt";
	     case 0x02000000: return "Reserved25BitAmd";
	     case 0x04000000: return "PushDescriptorsDescriptorBufferBitExt";
	          default     : return "";
		// clang-format on
	};
}

static std::string to_string_vk_buffer_usage_flags( const VkBufferUsageFlags& tp ) {
	uint64_t    flags = tp;
	std::string result;
	int         bit_pos = 0;
	while ( flags ) {
		if ( flags & 1 ) {
			if ( false == result.empty() ) {
				result.append( " | " );
			}
			result.append( to_str_vk_buffer_usage_flag_bits( VkBufferUsageFlagBits( 1ULL << bit_pos ) ) );
		}
		flags >>= 1;
		bit_pos++;
	}
	return result;
}

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_format( const VkFormat& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
		// clang-format off
	     case 0         : return "Undefined";
	     case 1         : return "R4G4UnormPack8";
	     case 2         : return "R4G4B4A4UnormPack16";
	     case 3         : return "B4G4R4A4UnormPack16";
	     case 4         : return "R5G6B5UnormPack16";
	     case 5         : return "B5G6R5UnormPack16";
	     case 6         : return "R5G5B5A1UnormPack16";
	     case 7         : return "B5G5R5A1UnormPack16";
	     case 8         : return "A1R5G5B5UnormPack16";
	     case 9         : return "R8Unorm";
	     case 10        : return "R8Snorm";
	     case 11        : return "R8Uscaled";
	     case 12        : return "R8Sscaled";
	     case 13        : return "R8Uint";
	     case 14        : return "R8Sint";
	     case 15        : return "R8Srgb";
	     case 16        : return "R8G8Unorm";
	     case 17        : return "R8G8Snorm";
	     case 18        : return "R8G8Uscaled";
	     case 19        : return "R8G8Sscaled";
	     case 20        : return "R8G8Uint";
	     case 21        : return "R8G8Sint";
	     case 22        : return "R8G8Srgb";
	     case 23        : return "R8G8B8Unorm";
	     case 24        : return "R8G8B8Snorm";
	     case 25        : return "R8G8B8Uscaled";
	     case 26        : return "R8G8B8Sscaled";
	     case 27        : return "R8G8B8Uint";
	     case 28        : return "R8G8B8Sint";
	     case 29        : return "R8G8B8Srgb";
	     case 30        : return "B8G8R8Unorm";
	     case 31        : return "B8G8R8Snorm";
	     case 32        : return "B8G8R8Uscaled";
	     case 33        : return "B8G8R8Sscaled";
	     case 34        : return "B8G8R8Uint";
	     case 35        : return "B8G8R8Sint";
	     case 36        : return "B8G8R8Srgb";
	     case 37        : return "R8G8B8A8Unorm";
	     case 38        : return "R8G8B8A8Snorm";
	     case 39        : return "R8G8B8A8Uscaled";
	     case 40        : return "R8G8B8A8Sscaled";
	     case 41        : return "R8G8B8A8Uint";
	     case 42        : return "R8G8B8A8Sint";
	     case 43        : return "R8G8B8A8Srgb";
	     case 44        : return "B8G8R8A8Unorm";
	     case 45        : return "B8G8R8A8Snorm";
	     case 46        : return "B8G8R8A8Uscaled";
	     case 47        : return "B8G8R8A8Sscaled";
	     case 48        : return "B8G8R8A8Uint";
	     case 49        : return "B8G8R8A8Sint";
	     case 50        : return "B8G8R8A8Srgb";
	     case 51        : return "A8B8G8R8UnormPack32";
	     case 52        : return "A8B8G8R8SnormPack32";
	     case 53        : return "A8B8G8R8UscaledPack32";
	     case 54        : return "A8B8G8R8SscaledPack32";
	     case 55        : return "A8B8G8R8UintPack32";
	     case 56        : return "A8B8G8R8SintPack32";
	     case 57        : return "A8B8G8R8SrgbPack32";
	     case 58        : return "A2R10G10B10UnormPack32";
	     case 59        : return "A2R10G10B10SnormPack32";
	     case 60        : return "A2R10G10B10UscaledPack32";
	     case 61        : return "A2R10G10B10SscaledPack32";
	     case 62        : return "A2R10G10B10UintPack32";
	     case 63        : return "A2R10G10B10SintPack32";
	     case 64        : return "A2B10G10R10UnormPack32";
	     case 65        : return "A2B10G10R10SnormPack32";
	     case 66        : return "A2B10G10R10UscaledPack32";
	     case 67        : return "A2B10G10R10SscaledPack32";
	     case 68        : return "A2B10G10R10UintPack32";
	     case 69        : return "A2B10G10R10SintPack32";
	     case 70        : return "R16Unorm";
	     case 71        : return "R16Snorm";
	     case 72        : return "R16Uscaled";
	     case 73        : return "R16Sscaled";
	     case 74        : return "R16Uint";
	     case 75        : return "R16Sint";
	     case 76        : return "R16Sfloat";
	     case 77        : return "R16G16Unorm";
	     case 78        : return "R16G16Snorm";
	     case 79        : return "R16G16Uscaled";
	     case 80        : return "R16G16Sscaled";
	     case 81        : return "R16G16Uint";
	     case 82        : return "R16G16Sint";
	     case 83        : return "R16G16Sfloat";
	     case 84        : return "R16G16B16Unorm";
	     case 85        : return "R16G16B16Snorm";
	     case 86        : return "R16G16B16Uscaled";
	     case 87        : return "R16G16B16Sscaled";
	     case 88        : return "R16G16B16Uint";
	     case 89        : return "R16G16B16Sint";
	     case 90        : return "R16G16B16Sfloat";
	     case 91        : return "R16G16B16A16Unorm";
	     case 92        : return "R16G16B16A16Snorm";
	     case 93        : return "R16G16B16A16Uscaled";
	     case 94        : return "R16G16B16A16Sscaled";
	     case 95        : return "R16G16B16A16Uint";
	     case 96        : return "R16G16B16A16Sint";
	     case 97        : return "R16G16B16A16Sfloat";
	     case 98        : return "R32Uint";
	     case 99        : return "R32Sint";
	     case 100       : return "R32Sfloat";
	     case 101       : return "R32G32Uint";
	     case 102       : return "R32G32Sint";
	     case 103       : return "R32G32Sfloat";
	     case 104       : return "R32G32B32Uint";
	     case 105       : return "R32G32B32Sint";
	     case 106       : return "R32G32B32Sfloat";
	     case 107       : return "R32G32B32A32Uint";
	     case 108       : return "R32G32B32A32Sint";
	     case 109       : return "R32G32B32A32Sfloat";
	     case 110       : return "R64Uint";
	     case 111       : return "R64Sint";
	     case 112       : return "R64Sfloat";
	     case 113       : return "R64G64Uint";
	     case 114       : return "R64G64Sint";
	     case 115       : return "R64G64Sfloat";
	     case 116       : return "R64G64B64Uint";
	     case 117       : return "R64G64B64Sint";
	     case 118       : return "R64G64B64Sfloat";
	     case 119       : return "R64G64B64A64Uint";
	     case 120       : return "R64G64B64A64Sint";
	     case 121       : return "R64G64B64A64Sfloat";
	     case 122       : return "B10G11R11UfloatPack32";
	     case 123       : return "E5B9G9R9UfloatPack32";
	     case 124       : return "D16Unorm";
	     case 125       : return "X8D24UnormPack32";
	     case 126       : return "D32Sfloat";
	     case 127       : return "S8Uint";
	     case 128       : return "D16UnormS8Uint";
	     case 129       : return "D24UnormS8Uint";
	     case 130       : return "D32SfloatS8Uint";
	     case 131       : return "Bc1RgbUnormBlock";
	     case 132       : return "Bc1RgbSrgbBlock";
	     case 133       : return "Bc1RgbaUnormBlock";
	     case 134       : return "Bc1RgbaSrgbBlock";
	     case 135       : return "Bc2UnormBlock";
	     case 136       : return "Bc2SrgbBlock";
	     case 137       : return "Bc3UnormBlock";
	     case 138       : return "Bc3SrgbBlock";
	     case 139       : return "Bc4UnormBlock";
	     case 140       : return "Bc4SnormBlock";
	     case 141       : return "Bc5UnormBlock";
	     case 142       : return "Bc5SnormBlock";
	     case 143       : return "Bc6HUfloatBlock";
	     case 144       : return "Bc6HSfloatBlock";
	     case 145       : return "Bc7UnormBlock";
	     case 146       : return "Bc7SrgbBlock";
	     case 147       : return "Etc2R8G8B8UnormBlock";
	     case 148       : return "Etc2R8G8B8SrgbBlock";
	     case 149       : return "Etc2R8G8B8A1UnormBlock";
	     case 150       : return "Etc2R8G8B8A1SrgbBlock";
	     case 151       : return "Etc2R8G8B8A8UnormBlock";
	     case 152       : return "Etc2R8G8B8A8SrgbBlock";
	     case 153       : return "EacR11UnormBlock";
	     case 154       : return "EacR11SnormBlock";
	     case 155       : return "EacR11G11UnormBlock";
	     case 156       : return "EacR11G11SnormBlock";
	     case 157       : return "Astc4X4UnormBlock";
	     case 158       : return "Astc4X4SrgbBlock";
	     case 159       : return "Astc5X4UnormBlock";
	     case 160       : return "Astc5X4SrgbBlock";
	     case 161       : return "Astc5X5UnormBlock";
	     case 162       : return "Astc5X5SrgbBlock";
	     case 163       : return "Astc6X5UnormBlock";
	     case 164       : return "Astc6X5SrgbBlock";
	     case 165       : return "Astc6X6UnormBlock";
	     case 166       : return "Astc6X6SrgbBlock";
	     case 167       : return "Astc8X5UnormBlock";
	     case 168       : return "Astc8X5SrgbBlock";
	     case 169       : return "Astc8X6UnormBlock";
	     case 170       : return "Astc8X6SrgbBlock";
	     case 171       : return "Astc8X8UnormBlock";
	     case 172       : return "Astc8X8SrgbBlock";
	     case 173       : return "Astc10X5UnormBlock";
	     case 174       : return "Astc10X5SrgbBlock";
	     case 175       : return "Astc10X6UnormBlock";
	     case 176       : return "Astc10X6SrgbBlock";
	     case 177       : return "Astc10X8UnormBlock";
	     case 178       : return "Astc10X8SrgbBlock";
	     case 179       : return "Astc10X10UnormBlock";
	     case 180       : return "Astc10X10SrgbBlock";
	     case 181       : return "Astc12X10UnormBlock";
	     case 182       : return "Astc12X10SrgbBlock";
	     case 183       : return "Astc12X12UnormBlock";
	     case 184       : return "Astc12X12SrgbBlock";
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
	     case 1000464000: return "R16G16S105Nv";
	          default     : return "";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_image_layout( const VkImageLayout& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
		// clang-format off
	     case 0         : return "Undefined";
	     case 1         : return "General";
	     case 2         : return "ColorAttachmentOptimal";
	     case 3         : return "DepthStencilAttachmentOptimal";
	     case 4         : return "DepthStencilReadOnlyOptimal";
	     case 5         : return "ShaderReadOnlyOptimal";
	     case 6         : return "TransferSrcOptimal";
	     case 7         : return "TransferDstOptimal";
	     case 8         : return "Preinitialized";
	     case 1000001002: return "PresentSrcKhr";
	     case 1000024000: return "VideoDecodeDstKhr";
	     case 1000024001: return "VideoDecodeSrcKhr";
	     case 1000024002: return "VideoDecodeDpbKhr";
	     case 1000111000: return "SharedPresentKhr";
	     case 1000117000: return "DepthReadOnlyStencilAttachmentOptimal";
	     case 1000117001: return "DepthAttachmentStencilReadOnlyOptimal";
	     case 1000164003: return "FragmentShadingRateAttachmentOptimalKhr";
	     case 1000218000: return "FragmentDensityMapOptimalExt";
	     case 1000241000: return "DepthAttachmentOptimal";
	     case 1000241001: return "DepthReadOnlyOptimal";
	     case 1000241002: return "StencilAttachmentOptimal";
	     case 1000241003: return "StencilReadOnlyOptimal";
	     case 1000299000: return "VideoEncodeDstKhr";
	     case 1000299001: return "VideoEncodeSrcKhr";
	     case 1000299002: return "VideoEncodeDpbKhr";
	     case 1000314000: return "ReadOnlyOptimal";
	     case 1000314001: return "AttachmentOptimal";
	     case 1000339000: return "AttachmentFeedbackLoopOptimalExt";
	          default     : return "";
		// clang-format on
	};
}

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_image_usage_flag_bits( const VkImageUsageFlagBits& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
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
	     case 0x00080000: return "AttachmentFeedbackLoopBitExt";
	     case 0x00100000: return "SampleWeightBitQcom";
	     case 0x00200000: return "SampleBlockMatchBitQcom";
	     case 0x00400000: return "Reserved22BitExt";
	          default     : return "";
		// clang-format on
	};
}

static std::string to_string_vk_image_usage_flags( const VkImageUsageFlags& tp ) {
	uint64_t    flags = tp;
	std::string result;
	int         bit_pos = 0;
	while ( flags ) {
		if ( flags & 1 ) {
			if ( false == result.empty() ) {
				result.append( " | " );
			}
			result.append( to_str_vk_image_usage_flag_bits( VkImageUsageFlagBits( 1ULL << bit_pos ) ) );
		}
		flags >>= 1;
		bit_pos++;
	}
	return result;
}

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_pipeline_stage_flag_bits2( const VkPipelineStageFlagBits2& tp ) {
	switch ( static_cast<int64_t>( tp ) ) {
		// clang-format off
	     case 0         : return "None";
	     case 0x00000001ULL: return "TopOfPipe";
	     case 0x00000002ULL: return "DrawIndirect";
	     case 0x00000004ULL: return "VertexInput";
	     case 0x00000008ULL: return "VertexShader";
	     case 0x00000010ULL: return "TessellationControlShader";
	     case 0x00000020ULL: return "TessellationEvaluationShader";
	     case 0x00000040ULL: return "GeometryShader";
	     case 0x00000080ULL: return "FragmentShader";
	     case 0x00000100ULL: return "EarlyFragmentTests";
	     case 0x00000200ULL: return "LateFragmentTests";
	     case 0x00000400ULL: return "ColorAttachmentOutput";
	     case 0x00000800ULL: return "ComputeShader";
	     case 0x00001000ULL: return "AllTransfer";
	     case 0x00002000ULL: return "BottomOfPipe";
	     case 0x00004000ULL: return "Host";
	     case 0x00008000ULL: return "AllGraphics";
	     case 0x00010000ULL: return "AllCommands";
	     case 0x00020000ULL: return "CommandPreprocessBitNv";
	     case 0x00040000ULL: return "ConditionalRenderingBitExt";
	     case 0x00080000ULL: return "TaskShaderBitExt";
	     case 0x00100000ULL: return "MeshShaderBitExt";
	     case 0x00200000ULL: return "RayTracingShaderBitKhr";
	     case 0x00400000ULL: return "FragmentShadingRateAttachmentBitKhr";
	     case 0x00800000ULL: return "FragmentDensityProcessBitExt";
	     case 0x01000000ULL: return "TransformFeedbackBitExt";
	     case 0x02000000ULL: return "AccelerationStructureBuildBitKhr";
	     case 0x04000000ULL: return "VideoDecodeBitKhr";
	     case 0x08000000ULL: return "VideoEncodeBitKhr";
	     case 0x10000000000ULL: return "InvocationMaskBitHuawei";
	     case 0x1000000000ULL: return "IndexInput";
	     case 0x100000000ULL: return "Copy";
	     case 0x10000000ULL: return "AccelerationStructureCopyBitKhr";
	     case 0x20000000000ULL: return "Reseved41BitHuawei";
	     case 0x2000000000ULL: return "VertexAttributeInput";
	     case 0x200000000ULL: return "Resolve";
	     case 0x20000000ULL: return "OpticalFlowBitNv";
	     case 0x4000000000ULL: return "PreRasterizationShaders";
	     case 0x400000000ULL: return "Blit";
	     case 0x40000000ULL: return "MicromapBuildBitExt";
	     case 0x8000000000ULL: return "SubpassShadingBitHuawei";
	     case 0x800000000ULL: return "Clear";
	          default     : return "";
		// clang-format on
	};
}

static std::string to_string_vk_pipeline_stage_flags2( const VkPipelineStageFlags2& tp ) {
	uint64_t    flags = tp;
	std::string result;
	int         bit_pos = 0;
	while ( flags ) {
		if ( flags & 1 ) {
			if ( false == result.empty() ) {
				result.append( " | " );
			}
			result.append( to_str_vk_pipeline_stage_flag_bits2( VkPipelineStageFlagBits2( 1ULL << bit_pos ) ) );
		}
		flags >>= 1;
		bit_pos++;
	}
	return result;
}

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_queue_flag_bits( const VkQueueFlagBits& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
		// clang-format off
	     case 0x00000001: return "Graphics";
	     case 0x00000002: return "Compute";
	     case 0x00000004: return "Transfer";
	     case 0x00000008: return "SparseBinding";
	     case 0x00000010: return "Protected";
	     case 0x00000020: return "VideoDecodeBitKhr";
	     case 0x00000040: return "VideoEncodeBitKhr";
	     case 0x00000080: return "Reserved7BitQcom";
	     case 0x00000100: return "OpticalFlowBitNv";
	     case 0x00000200: return "Reserved9BitExt";
	          default     : return "";
		// clang-format on
	};
}

static std::string to_string_vk_queue_flags( const VkQueueFlags& tp ) {
	uint64_t    flags = tp;
	std::string result;
	int         bit_pos = 0;
	while ( flags ) {
		if ( flags & 1 ) {
			if ( false == result.empty() ) {
				result.append( " | " );
			}
			result.append( to_str_vk_queue_flag_bits( VkQueueFlagBits( 1ULL << bit_pos ) ) );
		}
		flags >>= 1;
		bit_pos++;
	}
	return result;
}

// ----------------------------------------------------------------------

static constexpr char const* to_str_vk_result( const VkResult& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
		// clang-format off
	     case -1        : return "VkErrorOutOfHostMemory";
	     case -10       : return "VkErrorTooManyObjects";
	     case -1000000000: return "VkErrorSurfaceLostKhr";
	     case -1000000001: return "VkErrorNativeWindowInUseKhr";
	     case -1000001004: return "VkErrorOutOfDateKhr";
	     case -1000003001: return "VkErrorIncompatibleDisplayKhr";
	     case -1000011001: return "VkErrorValidationFailedExt";
	     case -1000012000: return "VkErrorInvalidShaderNv";
	     case -1000023000: return "VkErrorImageUsageNotSupportedKhr";
	     case -1000023001: return "VkErrorVideoPictureLayoutNotSupportedKhr";
	     case -1000023002: return "VkErrorVideoProfileOperationNotSupportedKhr";
	     case -1000023003: return "VkErrorVideoProfileFormatNotSupportedKhr";
	     case -1000023004: return "VkErrorVideoProfileCodecNotSupportedKhr";
	     case -1000023005: return "VkErrorVideoStdVersionNotSupportedKhr";
	     case -1000069000: return "VkErrorOutOfPoolMemory";
	     case -1000072003: return "VkErrorInvalidExternalHandle";
	     case -1000158000: return "VkErrorInvalidDrmFormatModifierPlaneLayoutExt";
	     case -1000161000: return "VkErrorFragmentation";
	     case -1000174001: return "VkErrorNotPermittedKhr";
	     case -1000255000: return "VkErrorFullScreenExclusiveModeLostExt";
	     case -1000257000: return "VkErrorInvalidOpaqueCaptureAddress";
	     case -1000338000: return "VkErrorCompressionExhaustedExt";
	     case -11       : return "VkErrorFormatNotSupported";
	     case -12       : return "VkErrorFragmentedPool";
	     case -13       : return "VkErrorUnknown";
	     case -2        : return "VkErrorOutOfDeviceMemory";
	     case -3        : return "VkErrorInitializationFailed";
	     case -4        : return "VkErrorDeviceLost";
	     case -5        : return "VkErrorMemoryMapFailed";
	     case -6        : return "VkErrorLayerNotPresent";
	     case -7        : return "VkErrorExtensionNotPresent";
	     case -8        : return "VkErrorFeatureNotPresent";
	     case -9        : return "VkErrorIncompatibleDriver";
	     case 0         : return "VkSuccess";
	     case 1         : return "VkNotReady";
	     case 2         : return "VkTimeout";
	     case 3         : return "VkEventSet";
	     case 4         : return "VkEventReset";
	     case 5         : return "VkIncomplete";
	     case 1000001003: return "VkSuboptimalKhr";
	     case 1000268000: return "VkThreadIdleKhr";
	     case 1000268001: return "VkThreadDoneKhr";
	     case 1000268002: return "VkOperationDeferredKhr";
	     case 1000268003: return "VkOperationNotDeferredKhr";
	     case 1000297000: return "VkPipelineCompileRequired";
	          default     : return "";
		// clang-format on
	};
}

// ----------------------------------------------------------------------
