#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

#include "le_hash_util.h"

constexpr size_t LE_MAX_NUM_GRAPH_RESOURCES = 2048; // Maximum number of unique resources in a Rendergraph. Set this to larger value if you want to deal with a larger number of distinct resources.
constexpr size_t LE_MAX_NUM_GRAPH_ROOTS     = 64;   // Maximum number of root nodes in a given RenderGraph. We assume this is much smaller than LE_MAX_NUM_GRAPH_RESOURCES, but worst case it would need to be the same size.

namespace le {
using RootPassesField = uint64_t; // used to express affinity to a root pass - each bit may represent a root pass
static_assert( sizeof( RootPassesField ) == LE_MAX_NUM_GRAPH_ROOTS / 8, "LeRootPassesField must have enough bits available to cover LE_MAX_NUM_GRAPH_ROOTS" );
} // namespace le

// Wraps a type (also may also be an enum) in a struct with `struct_name` so
// that it can be opaquely passed around, then unwrapped.
#define LE_WRAP_TYPE_IN_STRUCT( type_name, struct_name )              \
	struct struct_name {                                              \
		type_name        data;                                        \
		inline constexpr operator const type_name&() const noexcept { \
			return data;                                              \
		}                                                             \
		inline constexpr operator type_name&() noexcept {             \
			return data;                                              \
		}                                                             \
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

struct le_resource_handle_t {
	struct le_resource_handle_data_t* data;
};

struct le_img_resource_handle_t : le_resource_handle_t {
};
struct le_buf_resource_handle_t : le_resource_handle_t {
};
struct le_blas_resource_handle_t : le_resource_handle_t {
};
struct le_tlas_resource_handle_t : le_resource_handle_t {
};

// A graphics pipeline handle is an opaque handle to a *pipeline state* object.
// Note that the pipeline state is different from the actual pipeline, as the
// pipeline is created, based on a pipeline state and a renderpass.
//
LE_OPAQUE_HANDLE( le_gpso_handle );   // Opaque graphics pipeline state object handle
LE_OPAQUE_HANDLE( le_cpso_handle );   // Opaque compute pipeline state object handle
LE_OPAQUE_HANDLE( le_rtxpso_handle ); // Opaque rtx pipeline state object handle

#include "le_vk_enums.inl"

namespace le {
using ShaderStage = le::ShaderStageFlagBits;
}

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
		le::ImageUsageFlags  image_usage_flags;
		le::BufferUsageFlags buffer_usage_flags;
		LeRtxBlasUsageFlags  rtx_blas_usage_flags;
		LeRtxTlasUsageFlags  rtx_tlas_usage_flags;
		uint32_t             raw_data;
	} as;
};

// Callback type for a resource that needs to be notified that the current frame has been cleared
// we use this to tie lifetime of objects to the lifetime of the current frame by decrementing
// an intrusive pointer counter on each callback. (in le_video_decoder for example)
struct le_on_frame_clear_callback_data_t {
	void ( *cb_fun )( void* user_data ); // function pointer to call upon clear
	void* user_data;                     // user data to pass into function
};

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
enum class AttachmentBlendPreset : uint32_t {
	ePremultipliedAlpha = 0,
	eAdd,
	eMultiply,
	eCopy,
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

struct Extent2D {
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

struct ClearColorValue {
	union {
		float    float32[ 4 ];
		int32_t  int32[ 4 ];
		uint32_t uint32[ 4 ];
	};
};

struct ClearDepthStencilValue {
	float    depth;
	uint32_t stencil;
};

struct ClearValue {
	union {
		ClearColorValue        color;
		ClearDepthStencilValue depthStencil;
	};
};

} // namespace le

struct le_image_attachment_info_t {
	static constexpr le::ClearValue DefaultClearValueColor        = { { { { { 0.f, 0.f, 0.f, 0.f } } } } };
	static constexpr le::ClearValue DefaultClearValueDepthStencil = { { { { { 1.f, 0 } } } } };

	le::AttachmentLoadOp  loadOp  = le::AttachmentLoadOp::eClear;  //
	le::AttachmentStoreOp storeOp = le::AttachmentStoreOp::eStore; //

	le::ClearValue clearValue = DefaultClearValueColor; // only used if loadOp == clear
};

static constexpr le_image_attachment_info_t LeDepthAttachmentInfo() {
	auto info       = le_image_attachment_info_t{};
	info.clearValue = le_image_attachment_info_t::DefaultClearValueDepthStencil;
	return info;
}

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
			eImmediate = 0,
			eMailbox,
			eFifo,
			eDefault = eFifo,
			eFifoRelaxed,
			eSharedDemandRefresh,
			eSharedContinuousRefresh,
		};
		Presentmode            presentmode_hint;
		struct VkSurfaceKHR_T* vk_surface; // Will be set by backend.
		struct le_window_o*    window;
	};
	struct khr_direct_mode_settings_t {
		khr_settings_t::Presentmode presentmode_hint;
		struct VkSurfaceKHR_T*      vk_surface;   // Will be set by backend.
		char const*                 display_name; // Will be matched against display name
	};
	struct img_settings_t {
		struct le_image_encoder_interface_t* image_encoder_i;          // ffdecl. declared in shared/interfaces/le_image_encoder_interface.h
		void*                                image_encoder_parameters; // non-owning
		char const*                          image_filename_template;  // a format string, must contain %d for current image number.
		char const*                          pipe_cmd;                 // command used to save images - will receive stream of images via stdin
	};

	Type       type            = LE_KHR_SWAPCHAIN;
	uint32_t   width_hint      = 640;
	uint32_t   height_hint     = 480;
	uint32_t   imagecount_hint = 3;
	le::Format format_hint     = le::Format::eB8G8R8A8Unorm; // preferred surface format
	uint32_t   defer_create    = 0;                          // if set to non-null, then do not automatically create a swapchain if passed as parameter to renderer.setup()

	union {
		khr_settings_t             khr_settings;
		khr_direct_mode_settings_t khr_direct_mode_settings;
		img_settings_t             img_settings;
	};

	void init_khr_settings() {
		this->type                          = LE_KHR_SWAPCHAIN;
		this->khr_settings.presentmode_hint = khr_settings_t::Presentmode::eDefault;
		this->khr_settings.vk_surface       = nullptr;
		this->khr_settings.window           = nullptr;
	}
	void init_khr_direct_mode_settings() {
		this->type                                      = LE_DIRECT_SWAPCHAIN;
		this->khr_direct_mode_settings.presentmode_hint = khr_settings_t::Presentmode::eDefault;
		this->khr_direct_mode_settings.display_name     = "";
		this->khr_direct_mode_settings.vk_surface       = nullptr;
	}
	void init_img_settings() {
		this->type                                  = LE_IMG_SWAPCHAIN;
		this->img_settings.pipe_cmd                 = "";
		this->img_settings.image_encoder_i          = nullptr;
		this->img_settings.image_encoder_parameters = nullptr;
		this->img_settings.image_filename_template  = "isl_%08d.raw"; // image name template - a printf format string, must contain %d for image number.
	}
};

struct le_renderer_settings_t {
	le_swapchain_settings_t swapchain_settings[ 16 ] = {}; // todo: rename this to initial_swapchain_settings; make sure that this is only accessed during renderer::setup, and not any later. convert this into a linked list!
	size_t                  num_swapchain_settings   = 0;
	// TODO: add a hint for number of swapchain frames
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
		le::ImageCreateFlags flags;             // creation flags
		le::ImageType        imageType;         // enum vk::ImageType
		le::Format           format;            // enum vk::Format
		le::Extent3D         extent;            //
		le::Extent3D         extent_from_pass;  // used to calculate fallback extent if no extent given for all instances of the same image resource
		uint32_t             mipLevels;         //
		uint32_t             arrayLayers;       //
		uint32_t             sample_count_log2; // sample count as log2, 0 means 1, 1 means 2, 2 means 4...
		le::ImageTiling      tiling;            // enum VkImageTiling
		le::ImageUsageFlags  usage;             // usage flags (LeImageUsageFlags : uint32_t)
		uint32_t             samplesFlags;      // bitfield over all variants of this image resource- we use this to tell how many multisampling instances this image requires

		//		bool operator==( ImageInfo const& ) const = default;
	};

	struct BufferInfo {
		uint32_t             size;
		le::BufferUsageFlags usage; // usage flags (LeBufferUsageFlags : uint32_t)

		//		bool operator==( BufferInfo const& ) const = default;
	};

	struct TlasInfo {
		le_rtx_tlas_info_handle info; // opaque handle, but enough to refer back to original
		LeRtxTlasUsageFlags     usage;

		//		bool operator==( TlasInfo const& ) const = default;
	};

	struct BlasInfo {
		le_rtx_blas_info_handle info; // opaque handle, but enough to refer back to original
		LeRtxBlasUsageFlags     usage;

		//		bool operator==( BlasInfo const& ) const = default;
	};

	LeResourceType type;

	union {
		BufferInfo buffer;
		ImageInfo  image;
		BlasInfo   blas;
		TlasInfo   tlas;
	};

	le_resource_info_t( LeResourceType const& type_ = LeResourceType::eUndefined )
	    : type( type_ ) {
		image = {};
	};

	le_resource_info_t( le_resource_info_t const& lhs )            = default;
	le_resource_info_t( le_resource_info_t&& lhs )                 = default;
	le_resource_info_t& operator=( le_resource_info_t const& lhs ) = default;
	le_resource_info_t& operator=( le_resource_info_t&& lhs )      = default;

	explicit le_resource_info_t( ImageInfo const& img_info )
	    : image( img_info ) {
	}
	explicit le_resource_info_t( BufferInfo const& buffer_info )
	    : buffer( buffer_info ) {
	}
	explicit le_resource_info_t( BlasInfo const& blas_info )
	    : blas( blas_info ) {
	}
	explicit le_resource_info_t( TlasInfo const& tlas_info )
	    : tlas( tlas_info ) {
	}

	//	bool operator==( le_resource_info_t const& lhs ) const {
	//		if ( type != lhs.type ) {
	//			return false;
	//		}
	//		switch ( type ) {
	//		case ( LeResourceType::eUndefined ):
	//			return true;
	//		case ( LeResourceType::eBuffer ):
	//			return lhs.buffer == buffer;
	//		case ( LeResourceType::eImage ):
	//			return lhs.image == image;
	//		case ( LeResourceType::eRtxBlas ):
	//			return lhs.blas == blas;
	//		case ( LeResourceType::eRtxTlas ):
	//			return lhs.tlas == tlas;
	//		}

	//		return false;
	//	};
	//	bool operator!=( le_resource_info_t const& lhs ) const {
	//		return ( !operator==( lhs ) );
	//	}
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

constexpr uint8_t get_num_components( le_compound_num_type const& tp ) {
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

static bool le_format_infer_channels_and_num_type( le::Format const& format, uint32_t* num_channels, le_num_type* num_type ) {
	switch ( format ) {
	case le::Format::eB8G8R8A8Uint: // deliberate fall-through
	case le::Format::eB8G8R8A8Unorm:
	case le::Format::eR8G8B8A8Uint: // deliberate fall-through
	case le::Format::eR8G8B8A8Unorm:
		*num_channels = 4;
		*num_type     = le_num_type::eU8;
		return true;
	case le::Format::eR8G8B8Uint: // deliberate fall-through
	case le::Format::eR8G8B8Unorm:
		*num_channels = 3;
		*num_type     = le_num_type::eU8;
		return true;
	case le::Format::eR8Unorm:
		*num_channels = 1;
		*num_type     = le_num_type::eU8;
		return true;
	case le::Format::eR8G8Unorm:
		*num_channels = 2;
		*num_type     = le_num_type::eU8;
		return true;
	case le::Format::eR16Unorm:
		*num_channels = 1;
		*num_type     = le_num_type::eU16;
		return true;
	case le::Format::eR16Sfloat:
		*num_channels = 1;
		*num_type     = le_num_type::eF16;
		return true;
	case le::Format::eR32Sfloat:
		*num_channels = 1;
		*num_type     = le_num_type::eF32;
		return true;
	case le::Format::eR16G16Unorm:
		*num_channels = 2;
		*num_type     = le_num_type::eU16;
		return true;
	case le::Format::eR16G16Sfloat:
		*num_channels = 2;
		*num_type     = le_num_type::eF16;
		return true;
	case le::Format::eR32G32Sfloat:
		*num_channels = 2;
		*num_type     = le_num_type::eF32;
		return true;
	case le::Format::eR16G16B16Unorm:
		*num_channels = 3;
		*num_type     = le_num_type::eU16;
		return true;
	case le::Format::eR16G16B16Sfloat:
		*num_channels = 3;
		*num_type     = le_num_type::eF16;
		return true;
	case le::Format::eR32G32B32Sfloat:
		*num_channels = 3;
		*num_type     = le_num_type::eF32;
		return true;
	case le::Format::eR16G16B16A16Unorm:
		*num_channels = 4;
		*num_type     = le_num_type::eU16;
		return true;
	case le::Format::eR16G16B16A16Sfloat:
		*num_channels = 4;
		*num_type     = le_num_type::eF16;
		return true;
	case le::Format::eR32G32B32A32Sfloat:
		*num_channels = 4;
		*num_type     = le_num_type::eF32;
		return true;
	case le::Format::eUndefined: // deliberate fall-through
	default:
		return false;
	}
}

// returns number of bytes needed to store given num_type
constexpr uint32_t size_of( le_num_type const& tp ) {
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
	void*    pipeline_obj;                    // opaque pipeline object
};

namespace le {
enum class CommandType : uint32_t {
	eDrawIndexed,
	eDraw,
	eDrawMeshTasks,
	eDrawMeshTasksNV,
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
	eVideoDecoderExecuteCallback,
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
		uint32_t x_count;
		uint32_t y_count;
		uint32_t z_count;
	} info;
};

struct CommandDrawMeshTasksNV {
	CommandHeader header = { { { CommandType::eDrawMeshTasksNV, sizeof( CommandDrawMeshTasksNV ) } } };
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
		le::PipelineStageFlags2 srcStageMask;
		le::PipelineStageFlags2 dstStageMask;
		le::AccessFlags2        dstAccessMask;
		le_buf_resource_handle  buffer;
		uint64_t                offset;
		uint64_t                range;

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
		le_tlas_resource_handle tlas_handle;
		uint32_t                geometry_instances_count;     // number of geometry instances for this tlas
		uint32_t                staging_buffer_offset;        // offset into staging buffer for geometry instance data
		le_buf_resource_handle  staging_buffer_id;            // staging buffer which stores geometry instance data
		void*                   staging_buffer_mapped_memory; // address of mapped area on staging buffer.
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
		uint64_t                argument_name_id; // const_char_hash id of argument name
		le_tlas_resource_handle tlas_id;          // top level acceleration structure resource id,
		uint64_t                array_index;      // argument array index (default is 0)
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
		uint32_t firstBinding;
		uint32_t bindingCount;
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

		void*    pipeline_native_handle; // handle to native pipeline object, most likely VkPipeline
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

struct CommandVideoDecoderExecuteCallback {
	CommandHeader header = { { { CommandType::eVideoDecoderExecuteCallback, sizeof( CommandVideoDecoderExecuteCallback ) } } };

	typedef void( callback_fun_t )( struct VkCommandBuffer_T* cmd, void* user_data, void const* p_backend_frame_data );

	struct {
		callback_fun_t* callback;
		void*           user_data;
	} info;
};
} // namespace le

#endif
