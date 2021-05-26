#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

#include "le_core/hash_util.h"

// Wraps a type (also may also be an enum) in a struct with `struct_name` so
// that it can be opaquely passed around, then unwrapped.
#define LE_WRAP_TYPE_IN_STRUCT( type_name, struct_name )               \
	struct struct_name {                                               \
		type_name        data;                                         \
		inline constexpr operator const type_name &() const noexcept { \
			return data;                                               \
		}                                                              \
		inline constexpr operator type_name &() noexcept {             \
			return data;                                               \
		}                                                              \
	}

LE_OPAQUE_HANDLE( le_texture_handle );

enum class LeResourceType : uint32_t {
	eUndefined = 0,
	eBuffer,
	eImage,
	eRtxBlas, // bottom level acceleration structure
	eRtxTlas, // top level acceleration structure
};

LE_OPAQUE_HANDLE( le_resource_handle );
LE_OPAQUE_HANDLE( le_img_resource_handle );
LE_OPAQUE_HANDLE( le_buf_resource_handle );
LE_OPAQUE_HANDLE( le_blas_resource_handle );
LE_OPAQUE_HANDLE( le_tlas_resource_handle );

enum LeRenderPassType : uint32_t {
	LE_RENDER_PASS_TYPE_UNDEFINED = 0,
	LE_RENDER_PASS_TYPE_DRAW      = 1,
	LE_RENDER_PASS_TYPE_TRANSFER  = 2,
	LE_RENDER_PASS_TYPE_COMPUTE   = 3,
};

// A graphics pipeline handle is an opaque handle to a *pipeline state* object.
// Note that the pipeline state is different from the actual pipeline, as the
// pipeline is created, based on a pipeline state and a renderpass.
//
LE_OPAQUE_HANDLE( le_gpso_handle );   // Opaque graphics pipeline state object handle
LE_OPAQUE_HANDLE( le_cpso_handle );   // Opaque compute pipeline state object handle
LE_OPAQUE_HANDLE( le_rtxpso_handle ); // Opaque rtx pipeline state object handle

typedef uint32_t LeImageCreateFlags;
// Codegen <VkImageCreateFlagBits, LeImageCreateFlags, c>
enum LeImageCreateFlagBits : LeImageCreateFlags {
	LE_IMAGE_CREATE_SPARSE_BINDING_BIT                        = 0x00000001,
	LE_IMAGE_CREATE_SPARSE_RESIDENCY_BIT                      = 0x00000002,
	LE_IMAGE_CREATE_SPARSE_ALIASED_BIT                        = 0x00000004,
	LE_IMAGE_CREATE_MUTABLE_FORMAT_BIT                        = 0x00000008,
	LE_IMAGE_CREATE_CUBE_COMPATIBLE_BIT                       = 0x00000010,
	LE_IMAGE_CREATE_ALIAS_BIT                                 = 0x00000400,
	LE_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT           = 0x00000040,
	LE_IMAGE_CREATE_2_D_ARRAY_COMPATIBLE_BIT                  = 0x00000020,
	LE_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT           = 0x00000080,
	LE_IMAGE_CREATE_EXTENDED_USAGE_BIT                        = 0x00000100,
	LE_IMAGE_CREATE_PROTECTED_BIT                             = 0x00000800,
	LE_IMAGE_CREATE_DISJOINT_BIT                              = 0x00000200,
	LE_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV                     = 0x00002000,
	LE_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT = 0x00001000,
	LE_IMAGE_CREATE_SUBSAMPLED_BIT_EXT                        = 0x00004000,
	LE_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT_KHR       = LE_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT,
	LE_IMAGE_CREATE_2_D_ARRAY_COMPATIBLE_BIT_KHR              = LE_IMAGE_CREATE_2_D_ARRAY_COMPATIBLE_BIT,
	LE_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT_KHR       = LE_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT,
	LE_IMAGE_CREATE_EXTENDED_USAGE_BIT_KHR                    = LE_IMAGE_CREATE_EXTENDED_USAGE_BIT,
	LE_IMAGE_CREATE_DISJOINT_BIT_KHR                          = LE_IMAGE_CREATE_DISJOINT_BIT,
	LE_IMAGE_CREATE_ALIAS_BIT_KHR                             = LE_IMAGE_CREATE_ALIAS_BIT,
};
// Codegen </VkImageCreateFlagBits>

typedef uint32_t LeAccessFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeAccessFlags_t, LeAccessFlags );
// Codegen <VkAccessFlagBits, LeAccessFlags_t, c>
enum LeAccessFlagBits : LeAccessFlags_t {
	LE_ACCESS_INDIRECT_COMMAND_READ_BIT                     = 0x00000001,
	LE_ACCESS_INDEX_READ_BIT                                = 0x00000002,
	LE_ACCESS_VERTEX_ATTRIBUTE_READ_BIT                     = 0x00000004,
	LE_ACCESS_UNIFORM_READ_BIT                              = 0x00000008,
	LE_ACCESS_INPUT_ATTACHMENT_READ_BIT                     = 0x00000010,
	LE_ACCESS_SHADER_READ_BIT                               = 0x00000020,
	LE_ACCESS_SHADER_WRITE_BIT                              = 0x00000040,
	LE_ACCESS_COLOR_ATTACHMENT_READ_BIT                     = 0x00000080,
	LE_ACCESS_COLOR_ATTACHMENT_WRITE_BIT                    = 0x00000100,
	LE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT             = 0x00000200,
	LE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT            = 0x00000400,
	LE_ACCESS_TRANSFER_READ_BIT                             = 0x00000800,
	LE_ACCESS_TRANSFER_WRITE_BIT                            = 0x00001000,
	LE_ACCESS_HOST_READ_BIT                                 = 0x00002000,
	LE_ACCESS_HOST_WRITE_BIT                                = 0x00004000,
	LE_ACCESS_MEMORY_READ_BIT                               = 0x00008000,
	LE_ACCESS_MEMORY_WRITE_BIT                              = 0x00010000,
	LE_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT              = 0x02000000,
	LE_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT       = 0x04000000,
	LE_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT      = 0x08000000,
	LE_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT            = 0x00100000,
	LE_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT     = 0x00080000,
	LE_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR           = 0x00200000,
	LE_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR          = 0x00400000,
	LE_ACCESS_SHADING_RATE_IMAGE_READ_BIT_NV                = 0x00800000,
	LE_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT             = 0x01000000,
	LE_ACCESS_COMMAND_PREPROCESS_READ_BIT_NV                = 0x00020000,
	LE_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_NV               = 0x00040000,
	LE_ACCESS_NONE_KHR                                      = 0,
	LE_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV            = LE_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
	LE_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV           = LE_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
	LE_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR = LE_ACCESS_SHADING_RATE_IMAGE_READ_BIT_NV,
};
// Codegen </VkAccessFlagBits>

typedef uint32_t LePipelineStageFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LePipelineStageFlags_t, LePipelineStageFlags );
// Codegen <VkPipelineStageFlagBits, LePipelineStageFlags_t, c>
enum LePipelineStageFlagBits : LePipelineStageFlags_t {
	LE_PIPELINE_STAGE_TOP_OF_PIPE_BIT                          = 0x00000001,
	LE_PIPELINE_STAGE_DRAW_INDIRECT_BIT                        = 0x00000002,
	LE_PIPELINE_STAGE_VERTEX_INPUT_BIT                         = 0x00000004,
	LE_PIPELINE_STAGE_VERTEX_SHADER_BIT                        = 0x00000008,
	LE_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT          = 0x00000010,
	LE_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT       = 0x00000020,
	LE_PIPELINE_STAGE_GEOMETRY_SHADER_BIT                      = 0x00000040,
	LE_PIPELINE_STAGE_FRAGMENT_SHADER_BIT                      = 0x00000080,
	LE_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT                 = 0x00000100,
	LE_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT                  = 0x00000200,
	LE_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT              = 0x00000400,
	LE_PIPELINE_STAGE_COMPUTE_SHADER_BIT                       = 0x00000800,
	LE_PIPELINE_STAGE_TRANSFER_BIT                             = 0x00001000,
	LE_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT                       = 0x00002000,
	LE_PIPELINE_STAGE_HOST_BIT                                 = 0x00004000,
	LE_PIPELINE_STAGE_ALL_GRAPHICS_BIT                         = 0x00008000,
	LE_PIPELINE_STAGE_ALL_COMMANDS_BIT                         = 0x00010000,
	LE_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT               = 0x01000000,
	LE_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT            = 0x00040000,
	LE_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR     = 0x02000000,
	LE_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR               = 0x00200000,
	LE_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV                = 0x00400000,
	LE_PIPELINE_STAGE_TASK_SHADER_BIT_NV                       = 0x00080000,
	LE_PIPELINE_STAGE_MESH_SHADER_BIT_NV                       = 0x00100000,
	LE_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT         = 0x00800000,
	LE_PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_NV                = 0x00020000,
	LE_PIPELINE_STAGE_NONE_KHR                                 = 0,
	LE_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV                = LE_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
	LE_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV      = LE_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
	LE_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR = LE_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV,
};
// Codegen </VkPipelineStageFlagBits>

typedef uint32_t LeImageUsageFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeImageUsageFlags_t, LeImageUsageFlags );
// Codegen <VkImageUsageFlagBits, LeImageUsageFlags_t, c>
enum LeImageUsageFlagBits : LeImageUsageFlags_t {
	LE_IMAGE_USAGE_TRANSFER_SRC_BIT                         = 0x00000001,
	LE_IMAGE_USAGE_TRANSFER_DST_BIT                         = 0x00000002,
	LE_IMAGE_USAGE_SAMPLED_BIT                              = 0x00000004,
	LE_IMAGE_USAGE_STORAGE_BIT                              = 0x00000008,
	LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT                     = 0x00000010,
	LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT             = 0x00000020,
	LE_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT                 = 0x00000040,
	LE_IMAGE_USAGE_INPUT_ATTACHMENT_BIT                     = 0x00000080,
	LE_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV                = 0x00000100,
	LE_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT             = 0x00000200,
	LE_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR = LE_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV,
};
// Codegen </VkImageUsageFlagBits>

typedef uint32_t LeBuildAccelerationStructureFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeBuildAccelerationStructureFlags_t, LeBuildAccelerationStructureFlags );
// Codegen <VkBuildAccelerationStructureFlagBitsKHR, LeBuildAccelerationStructureFlags_t, c>
enum LeBuildAccelerationStructureFlagBitsKHR : LeBuildAccelerationStructureFlags_t {
	LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR      = 0x00000001,
	LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR  = 0x00000002,
	LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR = 0x00000004,
	LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR = 0x00000008,
	LE_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR        = 0x00000010,
	LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV       = LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
	LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_NV   = LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR,
	LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV  = LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
	LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV  = LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
	LE_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_NV         = LE_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR,
};
// Codegen </VkBuildAccelerationStructureFlagBitsKHR>

typedef uint32_t LeBufferUsageFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeBufferUsageFlags_t, LeBufferUsageFlags );
// Codegen <VkBufferUsageFlagBits, LeBufferUsageFlags_t, c>
enum LeBufferUsageFlagBits : LeBufferUsageFlags_t {
	LE_BUFFER_USAGE_TRANSFER_SRC_BIT                                     = 0x00000001,
	LE_BUFFER_USAGE_TRANSFER_DST_BIT                                     = 0x00000002,
	LE_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT                             = 0x00000004,
	LE_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT                             = 0x00000008,
	LE_BUFFER_USAGE_UNIFORM_BUFFER_BIT                                   = 0x00000010,
	LE_BUFFER_USAGE_STORAGE_BUFFER_BIT                                   = 0x00000020,
	LE_BUFFER_USAGE_INDEX_BUFFER_BIT                                     = 0x00000040,
	LE_BUFFER_USAGE_VERTEX_BUFFER_BIT                                    = 0x00000080,
	LE_BUFFER_USAGE_INDIRECT_BUFFER_BIT                                  = 0x00000100,
	LE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT                            = 0x00020000,
	LE_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT                    = 0x00000800,
	LE_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT            = 0x00001000,
	LE_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT                        = 0x00000200,
	LE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR = 0x00080000,
	LE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR               = 0x00100000,
	LE_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR                         = 0x00000400,
	LE_BUFFER_USAGE_RAY_TRACING_BIT_NV                                   = LE_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
	LE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT                        = LE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	LE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR                        = LE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
};
// Codegen </VkBufferUsageFlagBits>

typedef uint32_t LeRtxBlasUsageFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeRtxBlasUsageFlags_t, LeRtxBlasUsageFlags );
enum LeRtxBlasUsageFlagBits : LeRtxBlasUsageFlags_t {
	LE_RTX_BLAS_USAGE_READ_BIT  = 0x00000001,
	LE_RTX_BLAS_USAGE_WRITE_BIT = 0x00000002,
	LE_RTX_BLAS_BUILD_BIT       = 0x00000004 | LE_RTX_BLAS_USAGE_WRITE_BIT, // build implies write
};

typedef uint32_t LeRtxTlasUsageFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeRtxTlasUsageFlags_t, LeRtxTlasUsageFlags );
enum LeRtxTlasUsageFlagBits : LeRtxBlasUsageFlags_t {
	LE_RTX_TLAS_USAGE_READ_BIT  = 0x00000001,
	LE_RTX_TLAS_USAGE_WRITE_BIT = 0x00000002,
	LE_RTX_TLAS_BUILD_BIT       = 0x00000004 | LE_RTX_TLAS_USAGE_WRITE_BIT, // build implies write
};

struct LeResourceUsageFlags {
	LeResourceType type;
	union {
		LeImageUsageFlags   image_usage_flags;
		LeBufferUsageFlags  buffer_usage_flags;
		LeRtxBlasUsageFlags rtx_blas_usage_flags;
		LeRtxTlasUsageFlags rtx_tlas_usage_flags;
		uint32_t            raw_data;
	} as;
};

typedef uint32_t LeColorComponentFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeColorComponentFlags_t, LeColorComponentFlags );
// Codegen <VkColorComponentFlagBits, LeColorComponentFlags_t, c>
enum LeColorComponentFlagBits : LeColorComponentFlags_t {
	LE_COLOR_COMPONENT_R_BIT = 0x00000001,
	LE_COLOR_COMPONENT_G_BIT = 0x00000002,
	LE_COLOR_COMPONENT_B_BIT = 0x00000004,
	LE_COLOR_COMPONENT_A_BIT = 0x00000008,
};
// Codegen </VkColorComponentFlagBits>

namespace le {
// Codegen <VkShaderStageFlagBits, uint32_t, cpp, ShaderStage>
enum class ShaderStage : uint32_t {
	eVertex                 = 0x00000001,
	eTessellationControl    = 0x00000002,
	eTessellationEvaluation = 0x00000004,
	eGeometry               = 0x00000008,
	eFragment               = 0x00000010,
	eCompute                = 0x00000020,
	eAllGraphics            = 0x0000001F,
	eAll                    = 0x7FFFFFFF,
	eRaygenBitKhr           = 0x00000100,
	eAnyHitBitKhr           = 0x00000200,
	eClosestHitBitKhr       = 0x00000400,
	eMissBitKhr             = 0x00000800,
	eIntersectionBitKhr     = 0x00001000,
	eCallableBitKhr         = 0x00002000,
	eTaskBitNv              = 0x00000040,
	eMeshBitNv              = 0x00000080,
	eRaygenBitNv            = eRaygenBitKhr,
	eAnyHitBitNv            = eAnyHitBitKhr,
	eClosestHitBitNv        = eClosestHitBitKhr,
	eMissBitNv              = eMissBitKhr,
	eIntersectionBitNv      = eIntersectionBitKhr,
	eCallableBitNv          = eCallableBitKhr,
};
// Codegen </VkShaderStageFlagBits>
} // namespace le
LE_WRAP_TYPE_IN_STRUCT( le::ShaderStage, LeShaderStageEnum );

namespace le {
enum class ShaderSourceLanguage : uint32_t {
	eGlsl    = 0,
	eHlsl    = 1,
	eSpirv   = 2,
	eDefault = eGlsl,
};
} // namespace le
LE_WRAP_TYPE_IN_STRUCT( le::ShaderSourceLanguage, LeShaderSourceLanguageEnum );

namespace le {
// Codegen <VkFrontFace, uint32_t>
enum class FrontFace : uint32_t {
	eCounterClockwise = 0,
	eClockwise        = 1,
};
// Codegen </VkFrontFace, uint32_t>

// Codegen <VkFilter, uint32_t>
enum class Filter : uint32_t {
	eNearest  = 0,
	eLinear   = 1,
	eCubicImg = 1000015000,
	eCubicExt = eCubicImg,
};
// Codegen </VkFilter>

// Codegen <VkSampleCountFlagBits, uint32_t>
enum class SampleCountFlagBits : uint32_t {
	e1  = 0x00000001,
	e2  = 0x00000002,
	e4  = 0x00000004,
	e8  = 0x00000008,
	e16 = 0x00000010,
	e32 = 0x00000020,
	e64 = 0x00000040,
};
// Codegen </VkSampleCountFlagBits, uint32_t>

// Codegen <VkCullModeFlagBits, uint32_t>
enum class CullModeFlagBits : uint32_t {
	eNone         = 0,
	eFront        = 0x00000001,
	eBack         = 0x00000002,
	eFrontAndBack = 0x00000003,
};
// Codegen </VkCullModeFlagBits, uint32_t>

// Codegen <VkPolygonMode, uint32_t>
enum class PolygonMode : uint32_t {
	eFill            = 0,
	eLine            = 1,
	ePoint           = 2,
	eFillRectangleNv = 1000153000,
};
// Codegen </VkPolygonMode, uint32_t>

// Codegen <VkPrimitiveTopology, uint32_t>
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
// Codegen </VkPrimitiveTopology>

// Codegen <VkIndexType, uint32_t>
enum class IndexType : uint32_t {
	eUint16   = 0,
	eUint32   = 1,
	eNoneKhr  = 1000165000,
	eUint8Ext = 1000265000,
	eNoneNv   = eNoneKhr,
};
// Codegen </VkIndexType>

// Codegen <VkBlendFactor, uint32_t>
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
// Codegen </VkBlendFactor>

// Codegen <VkSamplerAddressMode, uint32_t>
enum class SamplerAddressMode : uint32_t {
	eRepeat               = 0,
	eMirroredRepeat       = 1,
	eClampToEdge          = 2,
	eClampToBorder        = 3,
	eMirrorClampToEdge    = 4,
	eMirrorClampToEdgeKhr = eMirrorClampToEdge,
};
// Codegen </VkSamplerAddressMode>

// Codegen <VkSamplerMipmapMode, uint32_t>
enum class SamplerMipmapMode : uint32_t {
	eNearest = 0,
	eLinear  = 1,
};
// Codegen </VkSamplerMipmapMode>

// Codegen <VkBorderColor, uint32_t>
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
// Codegen </VkBorderColor>

// Codegen <VkBlendOp, uint32_t>
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
// Codegen </VkBlendOp>

enum class AttachmentBlendPreset : uint32_t {
	ePremultipliedAlpha = 0,
	eAdd,
	eMultiply,
	eCopy,
};

// Codegen <VkAttachmentStoreOp, uint32_t>
enum class AttachmentStoreOp : uint32_t {
	eStore    = 0,
	eDontCare = 1,
	eNoneQcom = 1000301000,
};
// Codegen </VkAttachmentStoreOp>

// Codegen <VkStencilOp, uint32_t>
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
// Codegen </VkStencilOp>

// Codegen <VkCompareOp, uint32_t>
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
// Codegen </VkCompareOp>

// Codegen <VkAttachmentLoadOp, uint32_t>
enum class AttachmentLoadOp : uint32_t {
	eLoad     = 0,
	eClear    = 1,
	eDontCare = 2,
};
// Codegen </VkAttachmentLoadOp>

// Codegen <VkImageViewType, uint32_t>
enum class ImageViewType : uint32_t {
	e1D        = 0,
	e2D        = 1,
	e3D        = 2,
	eCube      = 3,
	e1DArray   = 4,
	e2DArray   = 5,
	eCubeArray = 6,
};
// Codegen </VkImageViewType>

// Codegen <VkImageType, uint32_t>
enum class ImageType : uint32_t {
	e1D = 0,
	e2D = 1,
	e3D = 2,
};
// Codegen </VkImageType>

static const char *to_str( const AttachmentStoreOp &lhs ) {
	switch ( lhs ) {
	case AttachmentStoreOp::eStore:
		return "Store";
	case AttachmentStoreOp::eDontCare:
		return "DontCare";
	case AttachmentStoreOp::eNoneQcom:
		return "NoneQcom";
	}
	return "";
}

static const char *to_str( const AttachmentLoadOp &lhs ) {
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

static const char *to_str( const ImageType &lhs ) {
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

// Codegen <VkImageTiling, uint32_t>
enum class ImageTiling : uint32_t {
	eOptimal              = 0,
	eLinear               = 1,
	eDrmFormatModifierExt = 1000158000,
};
// Codegen </VkImageTiling>

// Codegen <VkFormat>
enum class Format {
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
	eAstc4x4SfloatBlockExt                   = 1000066000,
	eAstc5x4SfloatBlockExt                   = 1000066001,
	eAstc5x5SfloatBlockExt                   = 1000066002,
	eAstc6x5SfloatBlockExt                   = 1000066003,
	eAstc6x6SfloatBlockExt                   = 1000066004,
	eAstc8x5SfloatBlockExt                   = 1000066005,
	eAstc8x6SfloatBlockExt                   = 1000066006,
	eAstc8x8SfloatBlockExt                   = 1000066007,
	eAstc10x5SfloatBlockExt                  = 1000066008,
	eAstc10x6SfloatBlockExt                  = 1000066009,
	eAstc10x8SfloatBlockExt                  = 1000066010,
	eAstc10x10SfloatBlockExt                 = 1000066011,
	eAstc12x10SfloatBlockExt                 = 1000066012,
	eAstc12x12SfloatBlockExt                 = 1000066013,
	eA4R4G4B4UnormPack16Ext                  = 1000340000,
	eA4B4G4R4UnormPack16Ext                  = 1000340001,
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

struct Extent2D {
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

struct le_image_attachment_info_t {
	static constexpr LeClearValue DefaultClearValueColor        = { { { { { 0.f, 0.f, 0.f, 0.f } } } } };
	static constexpr LeClearValue DefaultClearValueDepthStencil = { { { { { 1.f, 0 } } } } };

	le::AttachmentLoadOp  loadOp  = le::AttachmentLoadOp::eClear;  //
	le::AttachmentStoreOp storeOp = le::AttachmentStoreOp::eStore; //

	LeClearValue clearValue = DefaultClearValueColor; // only used if loadOp == clear
};

static constexpr le_image_attachment_info_t LeDepthAttachmentInfo() {
	auto info       = le_image_attachment_info_t{};
	info.clearValue = le_image_attachment_info_t::DefaultClearValueDepthStencil;
	return info;
}

typedef uint32_t LeResourceAccessFlags_t;
LE_WRAP_TYPE_IN_STRUCT( LeResourceAccessFlags_t, LeResourceAccessFlags );
enum LeResourceAccessFlagBits : LeResourceAccessFlags_t {
	eLeResourceAccessFlagBitUndefined  = 0x0,
	eLeResourceAccessFlagBitRead       = 0x1 << 0,
	eLeResourceAccessFlagBitWrite      = 0x1 << 1,
	eLeResourceAccessFlagBitsReadWrite = eLeResourceAccessFlagBitRead | eLeResourceAccessFlagBitWrite,
};

// use le::ImageSamplerBuilder to define texture info
struct le_sampler_info_t {
	le::Filter             magFilter               = le::Filter::eLinear;
	le::Filter             minFilter               = le::Filter::eLinear;
	le::SamplerMipmapMode  mipmapMode              = le::SamplerMipmapMode::eLinear;
	le::SamplerAddressMode addressModeU            = le::SamplerAddressMode::eClampToBorder;
	le::SamplerAddressMode addressModeV            = le::SamplerAddressMode::eClampToBorder;
	le::SamplerAddressMode addressModeW            = le::SamplerAddressMode::eRepeat;
	float                  mipLodBias              = 0.f;
	bool                   anisotropyEnable        = false;
	float                  maxAnisotropy           = 0.f;
	bool                   compareEnable           = false;
	le::CompareOp          compareOp               = le::CompareOp::eLess;
	float                  minLod                  = 0.f;
	float                  maxLod                  = 1.f;
	le::BorderColor        borderColor             = le::BorderColor::eFloatTransparentBlack;
	bool                   unnormalizedCoordinates = false;
};

struct le_image_sampler_info_t {
	struct le_image_view_info_t {
		le_img_resource_handle imageId{}; // le image resource id
		le::Format             format{};  // leave at 0 (undefined) to use format of image referenced by `imageId`
		le::ImageViewType      image_view_type{ le::ImageViewType::e2D };
		uint32_t               base_array_layer{ 0 };
		uint32_t               layer_count{ 1 };
	};
	le_sampler_info_t    sampler{};
	le_image_view_info_t imageView{};
};

struct le_swapchain_settings_t {
	enum Type {
		LE_KHR_SWAPCHAIN = 0,
		LE_DIRECT_SWAPCHAIN,
		LE_IMG_SWAPCHAIN,
	};
	struct khr_settings_t {
		enum class Presentmode : uint32_t {
			eDefault = 0,
			eImmediate,
			eMailbox,
			eFifo,
			eFifoRelaxed,
			eSharedDemandRefresh,
			eSharedContinuousRefresh,
		};
		Presentmode            presentmode_hint = Presentmode::eFifo;
		struct VkSurfaceKHR_T *vk_surface;
		struct le_window_o *   window;
	};
	struct img_settings_t {
		char const *pipe_cmd; // command used to save images - will receive stream of images via stdin
	};

	Type       type            = LE_KHR_SWAPCHAIN;
	uint32_t   width_hint      = 640;
	uint32_t   height_hint     = 480;
	uint32_t   imagecount_hint = 3;
	le::Format format_hint     = le::Format::eB8G8R8A8Unorm; // preferred surface format

	union {
		khr_settings_t khr_settings{};
		img_settings_t img_settings;
	};
};

struct le_renderer_settings_t {
	char const **           requested_device_extensions       = nullptr; // optional
	uint32_t                requested_device_extensions_count = 0;       //
	le_swapchain_settings_t swapchain_settings[ 16 ]          = {};
	size_t                  num_swapchain_settings            = 1;
};

// specifies parameters for an image write operation.
struct le_write_to_image_settings_t {
	uint32_t image_w         = 0; // image (slice) width in texels
	uint32_t image_h         = 0; // image (slice) height in texels
	uint32_t image_d         = 1; // image (slice) depth in texels
	int32_t  offset_x        = 0; // target offset for width
	int32_t  offset_y        = 0; // target offset for height
	int32_t  offset_z        = 0; // target offset for depth
	uint32_t dst_array_layer = 0; // target array layer to write into - default 0 for non-array, or cube map images.
	uint32_t dst_miplevel    = 0; // target image mip level to write into
	uint32_t num_miplevels   = 1; // number of miplevels to auto-generate (default 1 - more than one means to auto-generate miplevels)
};

namespace le {
using Presentmode = le_swapchain_settings_t::khr_settings_t::Presentmode;

#define BUILDER_IMPLEMENT( builder, method_name, param_type, param, default_value ) \
	constexpr builder &method_name( param_type param default_value ) {              \
		self.param = param;                                                         \
		return *this;                                                               \
	}

#define P_BUILDER_IMPLEMENT( builder, method_name, param_type, param, default_value ) \
	constexpr builder &method_name( param_type param default_value ) {                \
		self->param = param;                                                          \
		return *this;                                                                 \
	}
class RendererInfoBuilder {
	le_renderer_settings_t   info{};
	le_renderer_settings_t & self               = info;
	le_swapchain_settings_t *swapchain_settings = nullptr;

  public:
	RendererInfoBuilder( le_window_o *window = nullptr, char const **requested_device_extensions = nullptr, uint32_t requested_device_extensions_count = 0 )
	    : swapchain_settings( info.swapchain_settings ) {
		if ( window != nullptr ) {
			info.swapchain_settings->khr_settings.window = window;
			info.swapchain_settings->type                = le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN;
		}
		info.requested_device_extensions       = requested_device_extensions;
		info.requested_device_extensions_count = requested_device_extensions_count;
	}

	class SwapchainInfoBuilder {
		RendererInfoBuilder &     parent;
		le_swapchain_settings_t *&self;

	  public:
		SwapchainInfoBuilder( RendererInfoBuilder &parent_ )
		    : parent( parent_ )
		    , self( parent.swapchain_settings ) {
		}

		P_BUILDER_IMPLEMENT( SwapchainInfoBuilder, setWidthHint, uint32_t, width_hint, = 640 )
		P_BUILDER_IMPLEMENT( SwapchainInfoBuilder, setHeightHint, uint32_t, height_hint, = 480 )
		P_BUILDER_IMPLEMENT( SwapchainInfoBuilder, setImagecountHint, uint32_t, imagecount_hint, = 3 )
		P_BUILDER_IMPLEMENT( SwapchainInfoBuilder, setFormatHint, le::Format, format_hint, = le::Format::eB8G8R8A8Unorm )

		class KhrSwapchainInfoBuilder {
			SwapchainInfoBuilder &parent;

		  public:
			KhrSwapchainInfoBuilder( SwapchainInfoBuilder &parent_ )
			    : parent( parent_ ) {
			}

			KhrSwapchainInfoBuilder &setPresentmode( le::Presentmode presentmode_hint = le::Presentmode::eFifo ) {
				this->parent.parent.swapchain_settings->khr_settings.presentmode_hint = presentmode_hint;
				return *this;
			}

			KhrSwapchainInfoBuilder &setWindow( le_window_o *window = nullptr ) {
				this->parent.parent.swapchain_settings->khr_settings.window = window;
				return *this;
			}

			SwapchainInfoBuilder &end() {
				parent.parent.swapchain_settings->type = le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN;
				return parent;
			}
		};

		class DirectSwapchainInfoBuilder {
			SwapchainInfoBuilder &parent;

		  public:
			DirectSwapchainInfoBuilder( SwapchainInfoBuilder &parent_ )
			    : parent( parent_ ) {
			}

			DirectSwapchainInfoBuilder &setPresentmode( le::Presentmode presentmode_hint = le::Presentmode::eFifo ) {
				this->parent.parent.swapchain_settings->khr_settings.presentmode_hint = presentmode_hint;
				return *this;
			}

			SwapchainInfoBuilder &end() {
				parent.parent.swapchain_settings->type = le_swapchain_settings_t::Type::LE_DIRECT_SWAPCHAIN;
				return parent;
			}
		};

		class ImgSwapchainInfoBuilder {
			SwapchainInfoBuilder &parent;

			static constexpr auto default_pipe_cmd = "ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s %dx%d -i - -threads 0 -preset fast -y -pix_fmt yuv420p isl%s.mp4";

		  public:
			ImgSwapchainInfoBuilder( SwapchainInfoBuilder &parent_ )
			    : parent( parent_ ) {
			}

			ImgSwapchainInfoBuilder &setPipeCmd( char const *pipe_cmd = default_pipe_cmd ) {
				parent.parent.swapchain_settings->img_settings.pipe_cmd = pipe_cmd;
				return *this;
			}

			SwapchainInfoBuilder &end() {
				parent.parent.swapchain_settings->type = le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN;
				return parent;
			}
		};

		DirectSwapchainInfoBuilder mDirectSwapchainInfoBuilder{ *this }; // order matters, last one will be default, because initialisation overwrites.
		ImgSwapchainInfoBuilder    mImgSwapchainInfoBuilder{ *this };    // order matters, last one will be default, because initialisation overwrites.
		KhrSwapchainInfoBuilder    mKhrSwapchainInfoBuilder{ *this };    // order matters, last one will be default, because initialisation overwrites.

		KhrSwapchainInfoBuilder &asWindowSwapchain() {
			return mKhrSwapchainInfoBuilder;
		}

		ImgSwapchainInfoBuilder &asImgSwapchain() {
			mImgSwapchainInfoBuilder.setPipeCmd();
			return mImgSwapchainInfoBuilder;
		}

		DirectSwapchainInfoBuilder &asDirectSwapchain() {
			return mDirectSwapchainInfoBuilder;
		}

		RendererInfoBuilder &end() {
			if ( parent.swapchain_settings != &parent.info.swapchain_settings[ 0 ] ) {
				parent.info.num_swapchain_settings++;
			}
			parent.swapchain_settings++;
			return parent;
		}
	};

	SwapchainInfoBuilder mSwapchainInfoBuilder{ *this };

	SwapchainInfoBuilder &addSwapchain() {
		return mSwapchainInfoBuilder;
	}

	le_renderer_settings_t const &build() {

		// Do some checks:
		// + If no window was specified, then force image swapchain as a fallback.

		//		if ( self.swapchain_settings.type == le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN && self.window == nullptr ) {
		//			// We must force an image swapchain as a fallback.
		//			self.swapchain_settings.type         = le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN;
		//			self.swapchain_settings.img_settings = {}; // apply default image swapchain settings.
		//		}

		return info;
	}
};

// ----------------------------------------------------------------------

class ImageSamplerInfoBuilder {
	le_image_sampler_info_t info{};

	class SamplerInfoBuilder {
		ImageSamplerInfoBuilder &parent;
		le_sampler_info_t &      self = parent.info.sampler;

	  public:
		SamplerInfoBuilder( ImageSamplerInfoBuilder &parent_ )
		    : parent( parent_ ) {
		}

		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMagFilter, le::Filter, magFilter, = le::Filter::eLinear )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMinFilter, le::Filter, minFilter, = le::Filter::eLinear )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMipmapMode, le::SamplerMipmapMode, mipmapMode, = le::SamplerMipmapMode::eLinear )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeU, le::SamplerAddressMode, addressModeU, = le::SamplerAddressMode::eClampToBorder )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeV, le::SamplerAddressMode, addressModeV, = le::SamplerAddressMode::eClampToBorder )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAddressModeW, le::SamplerAddressMode, addressModeW, = le::SamplerAddressMode::eRepeat )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMipLodBias, float, mipLodBias, = 0.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setAnisotropyEnable, bool, anisotropyEnable, = false )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMaxAnisotropy, float, maxAnisotropy, = 0.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setCompareEnable, bool, compareEnable, = false )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setCompareOp, le::CompareOp, compareOp, = le::CompareOp::eLess )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMinLod, float, minLod, = 0.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setMaxLod, float, maxLod, = 1.f )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setBorderColor, le::BorderColor, borderColor, = le::BorderColor::eFloatTransparentBlack )
		BUILDER_IMPLEMENT( SamplerInfoBuilder, setUnnormalizedCoordinates, bool, unnormalizedCoordinates, = false )

		ImageSamplerInfoBuilder &end() {
			return parent;
		}
	};

	class ImageViewInfoBuilder {
		ImageSamplerInfoBuilder &                      parent;
		le_image_sampler_info_t::le_image_view_info_t &self = parent.info.imageView;

	  public:
		ImageViewInfoBuilder( ImageSamplerInfoBuilder &parent_ )
		    : parent( parent_ ) {
		}

		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setImage, le_img_resource_handle, imageId, = {} )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setImageViewType, le::ImageViewType, image_view_type, = le::ImageViewType::e2D )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setFormat, le::Format, format, = le::Format::eUndefined )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setBaseArrayLayer, uint32_t, base_array_layer, = 0 )
		BUILDER_IMPLEMENT( ImageViewInfoBuilder, setLayerCount, uint32_t, layer_count, = 1 )

		ImageSamplerInfoBuilder &end() {
			return parent;
		}
	};

	SamplerInfoBuilder   mSamplerInfoBuilder{ *this };
	ImageViewInfoBuilder mImageViewInfoBuilder{ *this };

  public:
	ImageSamplerInfoBuilder()  = default;
	~ImageSamplerInfoBuilder() = default;

	ImageSamplerInfoBuilder( le_image_sampler_info_t const &info_ )
	    : info( info_ ) {
	}

	ImageSamplerInfoBuilder( le_img_resource_handle const &image_resource ) {
		info.imageView.imageId = image_resource;
	}

	ImageViewInfoBuilder &withImageViewInfo() {
		return mImageViewInfoBuilder;
	}

	SamplerInfoBuilder &withSamplerInfo() {
		return mSamplerInfoBuilder;
	}

	le_image_sampler_info_t const &build() {
		return info;
	}
};

class ImageAttachmentInfoBuilder {
	le_image_attachment_info_t self{};

  public:
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setLoadOp, le::AttachmentLoadOp, loadOp, = le::AttachmentLoadOp::eClear )
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setStoreOp, le::AttachmentStoreOp, storeOp, = le::AttachmentStoreOp::eStore )
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setColorClearValue, LeClearValue, clearValue, = le_image_attachment_info_t::DefaultClearValueColor )
	BUILDER_IMPLEMENT( ImageAttachmentInfoBuilder, setDepthStencilClearValue, LeClearValue, clearValue, = le_image_attachment_info_t::DefaultClearValueDepthStencil )

	le_image_attachment_info_t const &build() {
		return self;
	}
};

class WriteToImageSettingsBuilder {
	le_write_to_image_settings_t self{};

  public:
	WriteToImageSettingsBuilder() = default;

	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setImageW, uint32_t, image_w, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setImageH, uint32_t, image_h, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setImageD, uint32_t, image_d, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setOffsetX, int32_t, offset_x, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setOffsetY, int32_t, offset_y, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setOffsetZ, int32_t, offset_z, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setArrayLayer, uint32_t, dst_array_layer, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setDstMiplevel, uint32_t, dst_miplevel, = 0 )
	BUILDER_IMPLEMENT( WriteToImageSettingsBuilder, setNumMiplevels, uint32_t, num_miplevels, = 1 )

	constexpr le_write_to_image_settings_t const &build() const {
		return self;
	}
};

#undef BUILDER_IMPLEMENT
#undef P_BUILDER_IMPLEMENT

} // namespace le

namespace le {
enum class RayTracingShaderGroupType : uint32_t {
	eRayGen = 0,
	eTrianglesHitGroup,
	eProceduralHitGroup,
	eMiss,
	eCallable,
};

} // namespace le

LE_OPAQUE_HANDLE( le_rtx_blas_info_handle ); // opaque handle to a bottom level acceleration structure info owned by the backend.
LE_OPAQUE_HANDLE( le_rtx_tlas_info_handle ); // opaque handle to a top level acceleration structure info owned by the backend.

static constexpr uint32_t LE_SHADER_UNUSED_NV = ~( 0u );

// we use this internally instead of vk::RayTrancingShaderGroupCreateInfoNV because
// we must hash this as part of getting the hash of the pipeline state.
// We can and must control that this struct is tightly packed.
struct le_rtx_shader_group_info {
	le::RayTracingShaderGroupType type;
	uint32_t                      generalShaderIdx      = LE_SHADER_UNUSED_NV;
	uint32_t                      closestHitShaderIdx   = LE_SHADER_UNUSED_NV;
	uint32_t                      anyHitShaderIdx       = LE_SHADER_UNUSED_NV;
	uint32_t                      intersectionShaderIdx = LE_SHADER_UNUSED_NV;
};

struct le_rtx_geometry_t {
	le_buf_resource_handle vertex_buffer;
	uint32_t               vertex_offset; // offset into vertex buffer
	uint32_t               vertex_count;  // number of vertices
	uint32_t               vertex_stride; // should default to size_for(vertex_format)
	le::Format             vertex_format; //

	le_buf_resource_handle index_buffer;
	uint32_t               index_offset;
	uint32_t               index_count;
	le::IndexType          index_type;
};

// Ray tracing geometry instance
struct le_rtx_geometry_instance_t {
	float transform[ 12 ]; // transposed, and truncated glm::mat4

	// Note that this bitfield assumes that instanceId will be stored in lower bits
	// and that mask will be placed in higher bits. The c standard does not define
	// a layout for bitfields. But this is how the Vulkan spec suggests doing it
	// anyway.

	uint32_t instanceCustomIndex : 24; // -> gl_InstanceCustomIndex
	uint32_t mask : 8;
	uint32_t instanceShaderBindingTableRecordOffset : 24; // Given in records - offset into Shader Binding Table for this instance - points at first hit shader for first geometry for this instance
	uint32_t flags : 8;
	uint64_t blas_handle; ///< you don't need to fill this in, will get patched by backend

	// We must enforce that hande has the same size as an uint64_t, as this is
	// what's internally used by the RTX api for the actual vkAccelerationHandle
	static_assert( sizeof( blas_handle ) == sizeof( uint64_t ), "size of blas info handle must be 64bit" );
};

// we must enforce size of le_rtx_geometry instance to be the same as what the spec requires
// `VkGeometryInstanceNV` to be. Note that VkGeometryInstanceNV is not defined in the header,
// but described in the spec.
static_assert( sizeof( le_rtx_geometry_instance_t ) == 64, "rtx_geometry_instance must be 64 bytes in size" );

// ----------------------------------------------------------------------
/// Specifies the intended usage for a resource.
///
/// It is the backend's responsibility to provide a concrete implementation
/// which matches the specified intent.
///
/// \brief Use ImageInfoBuilder, and BufferInfoBuilder to build `resource_info_t`
struct le_resource_info_t {
	struct ImageInfo {
		LeImageCreateFlags flags;             // creation flags
		le::ImageType      imageType;         // enum vk::ImageType
		le::Format         format;            // enum vk::Format
		le::Extent3D       extent;            //
		le::Extent3D       extent_from_pass;  // used to calculate fallback extent if no extent given for all instances of the same image resource
		uint32_t           mipLevels;         //
		uint32_t           arrayLayers;       //
		uint32_t           sample_count_log2; // sample count as log2, 0 means 1, 1 means 2, 2 means 4...
		le::ImageTiling    tiling;            // enum VkImageTiling
		LeImageUsageFlags  usage;             // usage flags (LeImageUsageFlags : uint32_t)
		uint32_t           samplesFlags;      // bitfield over all variants of this image resource
	};

	struct BufferInfo {
		uint32_t           size;
		LeBufferUsageFlags usage; // usage flags (LeBufferUsageFlags : uint32_t)
	};

	struct TlasInfo {
		le_rtx_tlas_info_handle info; // opaque handle, but enough to refer back to original
		LeRtxTlasUsageFlags     usage;
	};

	struct BlasInfo {
		le_rtx_blas_info_handle info; // opaque handle, but enough to refer back to original
		LeRtxBlasUsageFlags     usage;
	};

	LeResourceType type;
	union {
		BufferInfo buffer;
		ImageInfo  image;
		BlasInfo   blas;
		TlasInfo   tlas;
	};
};

enum class le_compound_num_type : uint8_t {
	// Note that we store the number of components
	// for each num_type in the lower 4 bits, so that it may be extracted
	// as: (type & 0xF);
	eUndefined = ( 0 << 4 ) | 0,
	eScalar    = ( 1 << 4 ) | 1,
	eVec2      = ( 2 << 4 ) | 2,
	eVec3      = ( 3 << 4 ) | 3,
	eVec4      = ( 4 << 4 ) | 4,
	eMat2      = ( 5 << 4 ) | 4,
	eMat3      = ( 6 << 4 ) | 9,
	eMat4      = ( 7 << 4 ) | 16,
	eQuat4     = ( 8 << 4 ) | 4, // quaternion is stored as vec4, but interpolated as slerp rather than lerp
};

constexpr uint8_t get_num_components( le_compound_num_type const &tp ) {
	return ( uint8_t( tp ) & 0xF );
}

enum class le_num_type : uint8_t {
	//
	// Note that we store the log2 of the number of Bytes needed to store values of a type
	// in the least significant two bits, so that we can say: numBytes =  1 << (type & 0b11);
	//
	eChar      = ( 0 << 2 ) | 0,  //  8 bit signed int
	eUChar     = ( 1 << 2 ) | 0,  //  8 bit unsigned int
	eShort     = ( 2 << 2 ) | 1,  // 16 bit signed int
	eUShort    = ( 3 << 2 ) | 1,  // 16 bit unsigned int
	eInt       = ( 4 << 2 ) | 2,  // 32 bit signed int
	eUInt      = ( 5 << 2 ) | 2,  // 32 bit unsigned int
	eHalf      = ( 6 << 2 ) | 1,  // 16 bit float type
	eFloat     = ( 7 << 2 ) | 2,  // 32 bit float type
	eLong      = ( 8 << 2 ) | 3,  // 64 bit signed int
	eULong     = ( 9 << 2 ) | 3,  // 64 bit unsigned int
	eUndefined = ( 63 << 2 ) | 0, // undefined
	//
	// Aliases
	eU8  = eUChar,
	eI8  = eChar,
	eI16 = eShort,
	eU16 = eUShort,
	eU32 = eUInt,
	eI32 = eInt,
	eU64 = eULong,
	eI64 = eLong,
	eF32 = eFloat,
	eF16 = eHalf,
};

constexpr uint32_t size_of( le_num_type const &tp ) {
	return ( 1 << ( uint8_t( tp ) & 0b11 ) );
}

enum class le_vertex_input_rate : uint8_t {
	ePerVertex   = 0,
	ePerInstance = 1,
};

/// \note This struct assumes a little endian machine for sorting
struct le_vertex_input_attribute_description {
	union {
		struct {
			uint8_t     location;       /// 0..32 shader attribute location
			uint8_t     binding;        /// 0..32 binding slot
			uint16_t    binding_offset; /// 0..65565 offset for this location within binding (careful: must not be larger than maxVertexInputAttributeOffset [0.0x7ff])
			le_num_type type;           /// base type for attribute
			uint8_t     vecsize;        /// 0..7 number of elements of base type
			uint8_t     isNormalised;   /// whether this input comes pre-normalized
		};
		uint64_t raw_data = 0;
	};
};

struct le_vertex_input_binding_description {
	union {
		struct {
			uint8_t              binding;    /// binding slot 0..32(==MAX_ATTRIBUTE_BINDINGS)
			le_vertex_input_rate input_rate; /// per-vertex (0) or per-instance (1)
			uint16_t             stride;     /// per-vertex or per-instance stride in bytes (must be smaller than maxVertexInputBindingStride = [0x800])
		};
		uint32_t raw_data;
	};
};

struct LeShaderGroupDataHeader {
	uint32_t data_byte_count;                 // number of bytes in use for payload
	uint32_t rtx_shader_group_handle_size;    // given in bytes
	uint32_t rtx_shader_group_base_alignment; // given in bytes
	uint32_t rtx_shader_group_handles_count;  // number of handles in payload, must equal data_byte_count / rtx_shader_group_handle_size
	void *   pipeline_obj;                    // opaque pipeline object
};

namespace le {
enum class CommandType : uint32_t {
	eDrawIndexed,
	eDraw,
	eDrawMeshTasks,
	eDispatch,
	eBufferMemoryBarrier,
	eTraceRays,
	eSetLineWidth,
	eSetViewport,
	eBuildRtxTlas,
	eBuildRtxBlas,
	eSetScissor,
	eBindArgumentBuffer,
	eSetArgumentTexture,
	eSetArgumentImage,
	eSetArgumentTlas,
	eSetPushConstantData,
	eBindIndexBuffer,
	eBindVertexBuffers,
	eBindGraphicsPipeline,
	eBindComputePipeline,
	eBindRtxPipeline,
	eWriteToBuffer,
	eWriteToImage,
};

struct CommandHeader {
	union {
		struct {
			CommandType type; // type of recorded command
			uint32_t    size; // number of bytes this command occupies in a tightly packed array
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

struct CommandDrawMeshTasks {
	CommandHeader header = { { { CommandType::eDrawMeshTasks, sizeof( CommandDrawMeshTasks ) } } };
	struct {
		uint32_t taskCount;
		uint32_t firstTask;
	} info;
};

struct CommandDispatch {
	CommandHeader header = { { { CommandType::eDispatch, sizeof( CommandDispatch ) } } };
	struct {
		uint32_t groupCountX;
		uint32_t groupCountY;
		uint32_t groupCountZ;
		uint32_t __padding__;
	} info;
};

struct CommandBufferMemoryBarrier {
	CommandHeader header = { { { CommandType::eBufferMemoryBarrier, sizeof( CommandBufferMemoryBarrier ) } } };
	struct {
		LePipelineStageFlags   srcStageMask;
		LePipelineStageFlags   dstStageMask;
		LeAccessFlags          dstAccessMask;
		le_buf_resource_handle buffer;
		uint64_t               offset;
		uint64_t               range;

	} info;
};

struct CommandTraceRays {
	CommandHeader header = { { { CommandType::eTraceRays, sizeof( CommandTraceRays ) } } };
	struct {
		uint32_t width;
		uint32_t height;
		uint32_t depth;
		uint32_t __padding__;
	} info;
};

struct CommandSetViewport {
	CommandHeader header = { { { CommandType::eSetViewport, sizeof( CommandSetViewport ) } } };
	struct {
		uint32_t firstViewport;
		uint32_t viewportCount;
	} info;
};

struct CommandSetPushConstantData {
	CommandHeader header = { { { CommandType::eSetPushConstantData, sizeof( CommandSetPushConstantData ) } } };
	struct {
		uint64_t num_bytes;
	} info;
};

struct CommandBuildRtxTlas {
	CommandHeader header = { { { CommandType::eBuildRtxTlas, sizeof( CommandBuildRtxTlas ) } } };
	struct {
		le_resource_handle tlas_handle;
		uint32_t           geometry_instances_count;     // number of geometry instances for this tlas
		uint32_t           staging_buffer_offset;        // offset into staging buffer for geometry instance data
		le_resource_handle staging_buffer_id;            // staging buffer which stores geometry instance data
		void *             staging_buffer_mapped_memory; // address of mapped area on staging buffer.
	} info;
};

struct CommandBuildRtxBlas {
	CommandHeader header = { { { CommandType::eBuildRtxBlas, sizeof( CommandBuildRtxBlas ) } } };
	struct {
		uint32_t blas_handles_count;
		uint32_t padding__;
	} info;
};

struct CommandSetScissor {
	CommandHeader header = { { { CommandType::eSetScissor, sizeof( CommandSetScissor ) } } };
	struct {
		uint32_t firstScissor;
		uint32_t scissorCount;
	} info;
};

struct CommandSetArgumentTexture {
	CommandHeader header = { { { CommandType::eSetArgumentTexture, sizeof( CommandSetArgumentTexture ) } } };
	struct {
		uint64_t          argument_name_id; // const_char_hash id of argument name
		le_texture_handle texture_id;       // texture id, hash of texture name
		uint64_t          array_index;      // argument array index (default is 0)
	} info;
};

struct CommandSetArgumentImage {
	CommandHeader header = { { { CommandType::eSetArgumentImage, sizeof( CommandSetArgumentImage ) } } };
	struct {
		uint64_t               argument_name_id; // const_char_hash id of argument name
		le_img_resource_handle image_id;         // image resource id,
		uint64_t               array_index;      // argument array index (default is 0)
	} info;
};

struct CommandSetArgumentTlas {
	CommandHeader header = { { { CommandType::eSetArgumentTlas, sizeof( CommandSetArgumentTlas ) } } };
	struct {
		uint64_t           argument_name_id; // const_char_hash id of argument name
		le_resource_handle tlas_id;          // top level acceleration structure resource id,
		uint64_t           array_index;      // argument array index (default is 0)
	} info;
};

// -- bind a buffer to a ssbo shader argument
struct CommandBindArgumentBuffer {
	CommandHeader header = { { { CommandType::eBindArgumentBuffer, sizeof( CommandBindArgumentBuffer ) } } };
	struct {
		uint64_t               argument_name_id; // const_char_hash id of argument name
		le_buf_resource_handle buffer_id;        // id of buffer that holds data
		uint64_t               offset;           // offset into buffer
		uint64_t               range;            // size of argument data in bytes
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
		uint32_t                firstBinding;
		uint32_t                bindingCount;
		le_buf_resource_handle *pBuffers;
		uint64_t *              pOffsets;
	} info;
};

struct CommandBindIndexBuffer {
	CommandHeader header = { { { CommandType::eBindIndexBuffer, sizeof( CommandBindIndexBuffer ) } } };
	struct {
		le_buf_resource_handle buffer; // buffer id
		uint64_t               offset;
		le::IndexType          indexType;
		uint32_t               padding;
	} info;
};

struct CommandBindGraphicsPipeline {
	CommandHeader header = { { { CommandType::eBindGraphicsPipeline, sizeof( CommandBindGraphicsPipeline ) } } };
	struct {
		le_gpso_handle gpsoHandle;
	} info;
};

struct CommandBindComputePipeline {
	CommandHeader header = { { { CommandType::eBindComputePipeline, sizeof( CommandBindComputePipeline ) } } };
	struct {
		le_cpso_handle cpsoHandle;
	} info;
};

struct CommandBindRtxPipeline {
	CommandHeader header = { { { CommandType::eBindRtxPipeline, sizeof( CommandBindRtxPipeline ) } } };
	struct {

		void *   pipeline_native_handle; // handle to native pipeline object, most likely VkPipeline
		uint64_t pipeline_layout_key;
		uint64_t descriptor_set_layout_keys[ 8 ];
		uint64_t descriptor_set_layout_count;

		le_buf_resource_handle sbt_buffer;
		uint64_t               ray_gen_sbt_offset;
		uint64_t               ray_gen_sbt_size;
		uint64_t               miss_sbt_offset;
		uint64_t               miss_sbt_stride;
		uint64_t               miss_sbt_size;
		uint64_t               hit_sbt_offset;
		uint64_t               hit_sbt_stride;
		uint64_t               hit_sbt_size;
		uint64_t               callable_sbt_offset;
		uint64_t               callable_sbt_stride;
		uint64_t               callable_sbt_size;
	} info;
};

struct CommandWriteToBuffer {
	CommandHeader header = { { { CommandType::eWriteToBuffer, sizeof( CommandWriteToBuffer ) } } };
	struct {
		le_buf_resource_handle src_buffer_id; // le buffer id of scratch buffer
		le_buf_resource_handle dst_buffer_id; // which resource to write to
		uint64_t               src_offset;    // offset in scratch buffer where to find source data
		uint64_t               dst_offset;    // offset where to write to in target resource
		uint64_t               numBytes;      // number of bytes

	} info;
};

struct CommandWriteToImage {
	CommandHeader header = { { { CommandType::eWriteToImage, sizeof( CommandWriteToImage ) } } };

	struct {
		le_buf_resource_handle src_buffer_id;   // le buffer id of scratch buffer
		le_img_resource_handle dst_image_id;    // which resource to write to
		uint64_t               numBytes;        // number of bytes
		uint32_t               image_w;         // target region width in texels
		uint32_t               image_h;         // target region height in texels
		uint32_t               image_d;         // target region depth in texels - (default 1), must not be 0
		int32_t                offset_x;        // target offset x
		int32_t                offset_y;        // target offset y
		int32_t                offset_z;        // target offset z
		uint32_t               dst_array_layer; // array layer to write into (default 0)
		uint32_t               dst_miplevel;    // mip level to write into
		uint32_t               num_miplevels;   // number of miplevels to generate (default 1 - more than one means to auto-generate miplevels)
		uint32_t               padding;         // unused
	} info;
};

} // namespace le

#endif
