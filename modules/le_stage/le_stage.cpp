#include "le_stage.h"
#include "le_core/le_core.h"

#include "le_renderer/le_renderer.h"
#include "le_stage_types.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#include "le_camera/le_camera.h"
#include "le_pixels/le_pixels.h"
#include "le_timebase/le_timebase.h"

#include "3rdparty/src/spooky/SpookyV2.h"

#include "string.h" // for memcpy

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <unordered_map>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"
#include <glm/gtx/matrix_decompose.hpp>

// It could be nice if le_mesh_o could live outside of the stage - so that
// we could use it as a method to generate primitives for example, like spheres etc.

// the mesh would need a way to upload its geometry data.
// but in the most common cases that data will not be held inside the mesh.

// Stage is where we store our complete scene graph.
// The stage is the owner of the scene graph
//

/* 
 * We need a method which allows us to upload resources. This method needs to be called
 * once before we do any rendering of scenes from a particular gltf instance.
 * 
*/

constexpr auto RTX_IMAGE_TARGET_HANDLE = LE_IMG_RESOURCE( "rtx_target_img" );

// Wrappers so that we can pass data via opaque pointers across header boundaries

struct glm_vec3_t {
	glm::vec3 data;
};
struct glm_quat_t {
	glm::quat data;
};
struct glm_vec4_t {
	glm::vec4 data;
};

struct glm_mat4_t {
	glm::mat4 data;
};

struct stage_image_o {
	le_pixels_o *  pixels;
	le_pixels_info info;

	le_resource_handle_t handle;
	le_resource_info_t   resource_info;

	bool was_transferred;
};

struct le_texture_o {
	uint32_t             image_idx;
	uint32_t             sampler_idx;
	le_resource_handle_t texture_handle;
	std::string          name;
};

struct le_buffer_o {
	void *               mem;    // nullptr if not owning
	le_resource_handle_t handle; // renderer resource handle
	le_resource_info_t   resource_info;
	uint32_t             size;            // number of bytes
	bool                 was_transferred; // whether this buffer was transferred to gpu already
	bool                 owns_mem;        // true if sole owner of memory pointed to in mem
};

struct le_buffer_view_o {
	uint32_t            buffer_idx; // index of buffer in stage
	uint32_t            byte_offset;
	uint32_t            byte_length;
	uint32_t            byte_stride;
	le_buffer_view_type type; // vertex, or index type
};

struct le_accessor_o {
	le_num_type          component_type;
	le_compound_num_type type;
	uint16_t             byte_offset;
	uint32_t             count;
	uint32_t             buffer_view_idx; // index of buffer view in stage
	float                min[ 16 ];
	float                max[ 16 ];
	bool                 is_normalized;
	bool                 has_min;
	bool                 has_max;
	bool                 is_sparse;
};

struct le_attribute_o {
	le_primitive_attribute_info::Type type;
	uint32_t                          index;
	uint32_t                          accessor_idx;
	union morph_target_union_t {
		struct morph_target_t {
			uint16_t idx;       // index of morph target this attribute belongs to
			uint16_t is_target; // whether this attribute belongs to a morph target
		} target;
		uint32_t target_data;
	} morph;

	std::string name;
};

struct le_texture_view_o {
	uint32_t texture_id;
	uint32_t uv_set;
	uint32_t transform_uv_set;
	float    scale;

	glm::mat3 transform;

	bool has_transform;
};

struct le_material_pbr_metallic_roughness_o {
	le_texture_view_o *base_color;
	le_texture_view_o *metallic_roughness;

	float base_color_factor[ 4 ];
	float metallic_factor;
	float roughness_factor;
};

struct le_material_o {

	struct UboTextureParamsSlice {
		union {
			struct {
				float    scale   = 1;
				uint32_t uv_set  = 0;
				uint32_t tex_idx = 0;
				uint32_t padding = 0;
			} data;
			glm::vec4 vec;
		} slice;
	};

	std::string                           name;
	le_texture_view_o *                   normal_texture;
	le_texture_view_o *                   occlusion_texture;
	le_texture_view_o *                   emissive_texture;
	le_material_pbr_metallic_roughness_o *metallic_roughness;
	glm::vec3                             emissive_factor;

	// We initialise the following two elements when we set up our
	// materials and pipelines. This allows us to fetch textures
	// and associated settings quickers.
	//
	std::vector<le_resource_handle_t>  texture_handles;       // cached: texture handles
	std::vector<UboTextureParamsSlice> cached_texture_params; // cached: texture parameters from texture_infos
};

// A primitive is a set of triangles sharing the same material.
// it may optionally be affected by morph targets, and joints.
struct le_primitive_o {
	std::vector<uint64_t>             bindings_buffer_offsets; // cached: offset into each buffer_handle when binding
	std::vector<le_resource_handle_t> bindings_buffer_handles; // cached: bufferviews sorted and grouped based on accessors
	                                                           //
	uint32_t vertex_count;                                     // cached: number of POSITION vertices, used to figure out draw call param
	uint32_t index_count;                                      // cached: number of INDICES, if any.
	                                                           //
	le_gpso_handle pipeline_state_handle; /* non-owning */     // cached: contains material shaders, and vertex input state
	                                                           //
	uint64_t all_defines_hash;                                 // cached: hash over all shader defines
	                                                           //
	std::vector<le_attribute_o> attributes;                    // attributes (may also contain morph target attributes)
	                                                           //
	uint32_t morph_target_count;                               // number of morph targets (default 0)
	                                                           //
	uint32_t num_joints_sets;                                  // number of joints sets (for skinning) (default 0)

	uint32_t indices_accessor_idx;
	uint32_t material_idx;

	le_resource_handle_t rtx_blas_handle;
	le_resource_info_t   rtx_blas_info;
	bool                 rtx_was_transferred;

	bool has_indices;
	bool has_material;
};

// has many primitives
struct le_mesh_o {
	std::vector<le_primitive_o> primitives;
};

struct le_node_o {
	glm::mat4 global_transform;
	glm::mat4 inverse_global_transform;
	glm::mat4 local_transform;

	glm::vec3 local_translation;
	glm::quat local_rotation;
	glm::vec3 local_scale;

	float morph_target_weights[ 12 ]; // Morph target weights; These apply to all primitives in meshes associated with this node...

	char name[ 32 ];

	bool local_transform_cached;   // whether local transform is accurate wrt local[translation|rotation|scale]
	bool global_transform_chached; // whether global transform is current

	bool     has_mesh;
	uint32_t mesh_idx;

	bool     has_camera;
	uint32_t camera_idx;

	struct le_skin_o *skin; // Optional, non-owning

	// TODO: we could use the scene_bit_flags to express affinity,
	// or whether a node should be used for raytracing for example.
	uint64_t scene_bit_flags; // one bit for every scene this node is included in -

	std::vector<le_node_o *> children; // non-owning
};

struct le_skin_o {
	std::vector<le_node_o *> joints;                // non-owning
	le_node_o *              skeleton;              // optional, if present, this is applied instead of the transform of the node containing the skin.
	std::vector<glm::mat4>   inverse_bind_matrices; // one per joint
};

struct le_keyframe_o {
	enum class Type : uint32_t {
		eLinear,
		eStep,
		eCubicSpline,
	};
	uint64_t             delta_ticks;       // given in units of 1/12000 seconds.
	Type                 type;              //
	le_num_type          num_type;          // numeric type
	le_compound_num_type compound_num_type; // scalar, vec2, vec3, etc.
	uint16_t             array_size;        // number of elements used, default must be 1
	union {                                 //
		glm::quat as_quat[ 3 ];             // used for spline       : xyzw quaternion
		glm::vec4 as_vec4[ 3 ];             // used for spline
		glm::vec3 as_vec3[ 3 ];             // used for spline
		glm::vec2 as_vec2[ 3 ];             // used for spline
		float     as_scalar[ 12 ];          // used for weights
	} data;
};

/// A channel is a mapping from a sequence of keyframes to a node property
struct le_animation_channel_o {
	uint64_t ticks_offset;   // Offset (in ticks) of first keyframe
	uint64_t ticks_duration; // Offset (in ticks) of last keyframe, designating total duration in ticks for this channel, since keyframes are defined as: [0..n[
	//
	std::vector<le_keyframe_o> sampler;        // (non-owning) keyframes for this channel, their time is relative to this channel.
	                                           //
	le_compound_num_type target_compound_type; // numeric type for target - we keep this mostly because quaternion requires slerp rather than lerp.
	le_node_o *          target_node;          // (non-owning) pointer to targeted node						 : how do we deal with deleted nodes?
	void *               target_node_element;  // (non-owning) pointer to targeted node element (t, r, or s) : how do we deal with deleted nodes?
};

/// An animation is a collection of channels
struct le_animation_o {

	enum class PlaybackMode : uint32_t {
		eForward = 0,
		eLoop,
		eBounce,
	}; // how this channel should behave when repeating

	PlaybackMode playback_mode;

	uint64_t ticks_offset;   // Given in ticks for first keyframe over all channels. number of ticks to wait before starting animation, default 0
	uint64_t ticks_duration; // Given in ticks for last keyframe over all channels.  number of ticks this animation should run before resetting, default: duration of longest animation channel.

	std::vector<le_animation_channel_o> channels;
};

// A camera is only a camera if it is attached to a node - the same camera settings may be
// attached to multiple nodes, therefore we name this camera_settings for lack of a better
// name. Our interactive camera is held by a module, and that camera is called le_camera_o
// as should be expected.
struct le_camera_settings_o {

	enum class Type : uint32_t {
		eUndefined = 0,
		ePerspective,
		eOrthographic,
	};

	struct perspective_t {
		float fov_y_rad;    // vertical firld of view in radians
		float aspect_ratio; // width/height
		float z_far;
		float z_near;
	};

	struct orthographic_t {
		float x_mag;
		float y_mag;
		float z_far;
		float z_near;
	};

	Type type;

	union {
		perspective_t  as_perspective;
		orthographic_t as_orthographic;
	} data;
};

struct le_scene_o {
	uint8_t                  scene_id;   // matches scene bit flag in node.
	std::vector<le_node_o *> root_nodes; // non-owning

	le_resource_handle_t rtx_tlas_handle; // only used for rtx
	le_resource_info_t   rtx_tlas_info;   // only used for rtx

	le_resource_handle_t rtx_image_target;
};

// Owns all the data
struct le_stage_o {
	le_renderer_o *                   renderer;        // non-owning
	le_timebase_o *                   timebase;        // non-owning, optional
	std::vector<le_scene_o>           scenes;          //
	std::vector<le_animation_o>       animations;      //
	std::vector<le_node_o *>          nodes;           // owning
	std::vector<le_camera_settings_o> camera_settings; //
	std::vector<le_mesh_o>            meshes;          //
	std::vector<le_material_o>        materials;       //
	std::vector<le_accessor_o>        accessors;       //
	std::vector<le_buffer_view_o>     buffer_views;    //
	std::vector<le_buffer_o *>        buffers;         // owning
	std::vector<LeSamplerInfo>        samplers;        //
	std::vector<le_resource_handle_t> buffer_handles;  //
	std::vector<le_texture_o>         textures;        //
	std::vector<stage_image_o *>      images;          // owning
	std::vector<le_resource_handle_t> image_handles;   //
	std::vector<le_skin_o *>          skins;           // owning
};

// clang-format off
		        auto le_num_type_to_le_index_type = [](le_num_type const & nt)-> le::IndexType{
			        switch (nt){
			        case (le_num_type::eU16): return le::IndexType::eUint16;
			        case (le_num_type::eU32): return le::IndexType::eUint32;
			        case (le_num_type::eU8): return le::IndexType::eUint8Ext;
			        default:
				        assert (false);
			        }
					return le::IndexType::eUint16; // unreachable
		        };
// clang-format on

/// \brief Create image by interpreting given memory as an image.
/// \note  Image memory is decoded via stb_image.
/// \param debug_name : (optional) name to remember the image by.
/// \param mip_levels_: (optional) number of mip-levels to auto-generate:
///        0 means generate the full mip chain, any other number limits
///        the number of mip levels.
static uint32_t le_stage_create_image_from_memory(
    le_stage_o *         stage,
    unsigned char const *image_file_memory,
    uint32_t             image_file_sz,
    char const *         debug_name,
    uint32_t             mip_levels_ ) {

	assert( image_file_memory && "must point to memory" );
	assert( image_file_sz && "must have size > 0" );

	assert( stage->images.size() == stage->image_handles.size() );

	using namespace le_pixels;

	le_resource_handle_t res;

	res.handle.as_handle.meta.as_meta.type = LeResourceType::eImage;
	res.handle.as_handle.name_hash         = SpookyHash::Hash32( image_file_memory, image_file_sz, 0 );

#if LE_RESOURCE_LABEL_LENGTH > 0
	if ( debug_name ) {
		// Copy debug name if such was given, and handle has debug name field.
		strncpy( res.debug_name, debug_name, LE_RESOURCE_LABEL_LENGTH - 1 );
	}
#endif

	uint32_t image_handle_idx = 0;
	for ( auto &h : stage->image_handles ) {
		if ( h == res ) {
			break;
		}
		image_handle_idx++;
	}

	if ( image_handle_idx == stage->image_handles.size() ) {

		stage_image_o *img = new stage_image_o{};

		// We want to find out whether this image uses a 16 bit type.
		// further, if this image uses a single channel, we are fine with it,
		le_pixels_i.get_info_from_memory( image_file_memory, image_file_sz, &img->info );

		// If image more than 1 channel, we will request 4 channels, as
		// we cannot sample from RGB images (must be RGBA).
		if ( img->info.num_channels > 1 ) {
			img->info.num_channels = 4;
		}

		// update pixel information after load, since load hints/requests may have changed
		// how image was decoded in the end.

		img->pixels          = le_pixels_i.create_from_memory( image_file_memory, image_file_sz, img->info.num_channels, img->info.type );
		img->info            = le_pixels_i.get_info( img->pixels );
		img->handle          = res;
		img->was_transferred = false;

		le::Format imageFormat{};

		if ( img->info.type == le_pixels_info::Type::eUInt8 ) {
			if ( img->info.num_channels == 1 ) {
				imageFormat = le::Format::eR8Unorm;
			} else if ( img->info.num_channels == 4 ) {
				imageFormat = le::Format::eR8G8B8A8Unorm;
			}
		}

		uint32_t mip_levels =
		    mip_levels_
		        ? mip_levels_
		        : uint32_t( ceilf( log2f( std::max( img->info.width, img->info.height ) ) ) );

		img->resource_info =
		    le::ImageInfoBuilder()
		        .setExtent( img->info.width, img->info.height, img->info.depth )
		        .setFormat( imageFormat )
		        .setUsageFlags( {LeImageUsageFlagBits::LE_IMAGE_USAGE_SAMPLED_BIT |
		                         LeImageUsageFlagBits::LE_IMAGE_USAGE_TRANSFER_DST_BIT} )
		        .setMipLevels( mip_levels )
		        .build();

		stage->images.emplace_back( img );
		stage->image_handles.emplace_back( res );
	}

	return image_handle_idx;
}

/// \brief create image by loading file at given filepath into memory,
/// then handing over to `create_image_from_memory`
static uint32_t le_stage_create_image_from_file_path( le_stage_o *stage, char const *image_file_path, char const *debug_name, uint32_t mip_levels ) {

	void * image_file_memory = nullptr;
	size_t image_file_sz     = 0;

	FILE *file = fopen( image_file_path, "r" );
	assert( file );

	{
		fseek( file, 0, SEEK_END );
		long tell_sz = ftell( file );
		rewind( file );
		assert( tell_sz > 0 && "file cannot be empty" );
		image_file_sz = size_t( tell_sz );
	}

	image_file_memory = malloc( image_file_sz );

	size_t num_bytes_read = fread( image_file_memory, 1, image_file_sz, file );

	assert( num_bytes_read == image_file_sz );

	// load into memory, call create_image_from_memory
	uint32_t result =
	    le_stage_create_image_from_memory(
	        stage,
	        static_cast<unsigned char const *>( image_file_memory ),
	        uint32_t( image_file_sz ), debug_name, mip_levels );

	free( image_file_memory );

	fclose( file );
	// free file memory

	return result;
}

/// \brief add a sampler to stage, return index to sampler within this stage.
///
static uint32_t le_stage_create_sampler( le_stage_o *stage, LeSamplerInfo const *info ) {

	uint32_t sampler_idx = uint32_t( stage->samplers.size() );

	stage->samplers.emplace_back( *info );

	return sampler_idx;
}

/// \brief add a texture to stage, return index to texture within stage.
///
static uint32_t le_stage_create_texture( le_stage_o *stage, le_texture_info const *info ) {
	uint32_t texture_idx = uint32_t( stage->textures.size() );

	le_texture_o texture{};

	if ( info->name ) {
		texture.name = std::string{info->name};
	}

	texture.image_idx   = info->image_idx;
	texture.sampler_idx = info->sampler_idx;

	{
		// Create a unique handle from image id and sampler id

		char tex_id_str[ 32 ]{}; // 31 characters 6+1+6+1+17, plus one extra character for terminating \0
		snprintf( tex_id_str, 31, "%06u:%06u:%s", info->image_idx, info->sampler_idx, info->name );

		texture.texture_handle = LE_IMAGE_SAMPLER_RESOURCE( tex_id_str );

#if LE_RESOURCE_LABEL_LENGTH > 0
		if ( info->name ) {
			strncpy( texture.texture_handle.debug_name, info->name, LE_RESOURCE_LABEL_LENGTH );
		} else {
			strncpy( texture.texture_handle.debug_name, tex_id_str, LE_RESOURCE_LABEL_LENGTH );
		}
#endif
	}

	stage->textures.emplace_back( texture );

	return texture_idx;
}

/// \brief Add a buffer to stage, return index to buffer within this stage.
///
static uint32_t le_stage_create_buffer( le_stage_o *stage, void *mem, uint32_t sz, char const *debug_name ) {

	assert( mem && "must point to memory" );
	assert( sz && "must have size > 0" );

	assert( stage->buffers.size() == stage->buffer_handles.size() );

	le_resource_handle_t res{};

#if LE_RESOURCE_LABEL_LENGTH > 0
	if ( debug_name ) {
		// Copy debug name if such was given, and handle has debug name field.
		strncpy( res.debug_name, debug_name, LE_RESOURCE_LABEL_LENGTH );
	}
#endif

	res.handle.as_handle.name_hash         = SpookyHash::Hash32( mem, sz, 0 );
	res.handle.as_handle.meta.as_meta.type = LeResourceType::eBuffer;

	uint32_t buffer_handle_idx = 0;
	for ( auto &h : stage->buffer_handles ) {
		if ( h == res ) {
			break;
		}
		buffer_handle_idx++;
	}

	// ----------| Invariant: buffer_handle_idx == index for buffer handle inside stage

	if ( buffer_handle_idx == stage->buffer_handles.size() ) {

		// Buffer with this hash was not yet seen before
		// - we must allocate a new buffer.

		le_buffer_o *buffer = new le_buffer_o{};

		buffer->handle = res;
		buffer->mem    = malloc( sz );

		if ( buffer->mem ) {
			memcpy( buffer->mem, mem, sz );
			buffer->owns_mem = true;
			buffer->size     = sz;
		} else {
			// TODO: handle out-of-memory error.
			delete buffer;
			assert( false );
		}

		// TODO: check if we can narrow usage flags based on whether bufferview
		// which uses this buffer specifies index, or vertex for usage.

		buffer->resource_info = le::BufferInfoBuilder()
		                            .setSize( buffer->size )
		                            .addUsageFlags( {LE_BUFFER_USAGE_TRANSFER_DST_BIT |
		                                             LE_BUFFER_USAGE_INDEX_BUFFER_BIT |
		                                             LE_BUFFER_USAGE_VERTEX_BUFFER_BIT} )
		                            .build();

		stage->buffer_handles.push_back( res );
		stage->buffers.push_back( buffer );
	}

	return buffer_handle_idx;
}

/// \brief add buffer view to stage, return index of added buffer view inside of stage
static uint32_t le_stage_create_buffer_view( le_stage_o *self, le_buffer_view_info const *info ) {
	le_buffer_view_o view{};

	view.buffer_idx  = info->buffer_idx;
	view.byte_offset = info->byte_offset;
	view.byte_length = info->byte_length;
	view.byte_stride = info->byte_stride;
	view.type        = info->type;

	uint32_t idx = uint32_t( self->buffer_views.size() );
	self->buffer_views.emplace_back( view );
	return idx;
}

/// \brief add accessor to stage, return index of newly added accessor as it appears in stage.
/// since this refers to buffers and bufferviews, any buffers and bufferviews referred to must
/// already be stored within the stage.
static uint32_t le_stage_create_accessor( le_stage_o *self, le_accessor_info const *info ) {

	le_accessor_o accessor{};

	accessor.component_type  = info->component_type;
	accessor.type            = info->type;
	accessor.byte_offset     = uint16_t( info->byte_offset );
	accessor.count           = info->count;
	accessor.buffer_view_idx = info->buffer_view_idx;
	accessor.has_min         = info->has_min;
	accessor.has_max         = info->has_max;

	if ( info->has_min ) {
		memcpy( accessor.min, info->min, sizeof( float ) * 16 );
	}
	if ( info->has_max ) {
		memcpy( accessor.max, info->max, sizeof( float ) * 16 );
	}

	accessor.is_normalized = info->is_normalized;
	accessor.is_sparse     = info->is_sparse;

	if ( accessor.is_sparse ) {

		// We must resolve buffer data for sparse accessors so that the data referred
		// to by the accessor is a copy of the original data, modified by the sparse
		// accessor.
		//
		// This means that sparse accessors create new buffers (To store the modified
		// data), and new bufferviews (to point at the modified data).
		//

		le_buffer_view_o src_buffer_view = self->buffer_views[ accessor.buffer_view_idx ];
		le_buffer_o *    src_buffer      = self->buffers[ src_buffer_view.buffer_idx ];

		// Duplicate memory referred to in bufferview into new buffer, so that we may
		// update its contents.
		// Creating a new buffer will copy memory.

		uint32_t dst_buffer_idx =
		    le_stage_create_buffer(
		        self,
		        static_cast<char *>( src_buffer->mem ) + src_buffer_view.byte_offset,
		        src_buffer_view.byte_length,
		        "" );

		// We must also create a bufferview so that we can in the future refer to this data -
		// our accessor will use the new bufferview to refer to its sparsely modified data.

		le_buffer_view_info view_info{};
		view_info.type        = src_buffer_view.type;
		view_info.buffer_idx  = dst_buffer_idx;
		view_info.byte_offset = 0;
		view_info.byte_stride = size_of( accessor.component_type ) * get_num_components( accessor.type );
		view_info.byte_length = accessor.count * view_info.byte_stride;
		uint32_t dst_view_idx = le_stage_create_buffer_view( self, &view_info );

		// -- Now substitute sparse data by seeking to sparse data indices
		// and patching data from sparse data source.

		le_buffer_o *dst_buffer = self->buffers[ dst_buffer_idx ];

		// First fetch indices which need substitution

		le_buffer_view_o   indices_buffer_view = self->buffer_views[ info->sparse_accessor.indices_buffer_view_idx ];
		le_buffer_o const *indices_buffer      = self->buffers[ indices_buffer_view.buffer_idx ];

		le_buffer_view_o   sparse_data_view   = self->buffer_views[ info->sparse_accessor.values_buffer_view_idx ];
		le_buffer_o const *sparse_data_buffer = self->buffers[ sparse_data_view.buffer_idx ];

		char *const index_ptr       = static_cast<char *>( indices_buffer->mem ) + indices_buffer_view.byte_offset;
		void *const sparse_data_src = static_cast<char *>( sparse_data_buffer->mem ) + sparse_data_view.byte_offset;

		uint32_t stride       = view_info.byte_stride;
		uint32_t index_stride = size_of( info->sparse_accessor.indices_component_type );

		for ( uint32_t src_index = 0; src_index != info->sparse_accessor.count; src_index++ ) {

			uint32_t dst_index = 0;

			if ( info->sparse_accessor.indices_component_type == le_num_type::eU16 ) {
				dst_index = static_cast<uint16_t const &>( index_ptr[ index_stride * src_index ] );
			} else if ( info->sparse_accessor.indices_component_type == le_num_type::eU32 ) {
				dst_index = static_cast<uint32_t const &>( index_ptr[ index_stride * src_index ] );
			} else {
				assert( false && "index type must be one of u16 or u32" );
			}

			memcpy( static_cast<char *>( dst_buffer->mem ) + stride * dst_index, // change in dest at sparse index
			        static_cast<char *>( sparse_data_src ) + stride * src_index, // from data
			        stride );
		}

		// we patch accessor here
		accessor.buffer_view_idx = dst_view_idx;
	}

	uint32_t idx = uint32_t( self->accessors.size() );
	self->accessors.emplace_back( accessor );
	return idx;
}

/// \brief create textureview from `le_texture_view_info*`
/// \return nullptr if info was nullptr
static le_texture_view_o *create_texture_view( le_texture_view_info const *info ) {

	if ( nullptr == info ) {
		return nullptr;
	}

	auto const &src_tex = info;
	auto        tex     = new le_texture_view_o{};

	tex->uv_set = src_tex->uv_set;
	tex->scale  = src_tex->scale;

	tex->texture_id = src_tex->texture_idx;

	if ( src_tex->transform ) {
		tex->has_transform    = true;
		tex->transform_uv_set = src_tex->transform->uv_set;
		tex->transform =
		    glm::translate( glm::identity<glm::mat4>(), glm::vec3{src_tex->transform->offset[ 0 ], src_tex->transform->offset[ 1 ], 0} ) * // translate
		    glm::rotate( glm::identity<glm::mat4>(), src_tex->transform->rotation, glm::vec3{0.f, 0.f, 1.f} ) *                            // rotate
		    glm::scale( glm::identity<glm::mat4>(), glm::vec3{src_tex->transform->scale[ 0 ], src_tex->transform->scale[ 1 ], 0} );        // scale
	} else {
		tex->has_transform = false;
	}

	return tex;
};

/// \brief add material to stage, return index of newly created material as it appears in stage
static uint32_t le_stage_create_material( le_stage_o *stage, le_material_info const *info ) {
	uint32_t      idx = uint32_t( stage->materials.size() );
	le_material_o material{};

	if ( info->name ) {
		material.name = info->name;
	}

	if ( info->pbr_metallic_roughness_info ) {
		material.metallic_roughness = new le_material_pbr_metallic_roughness_o{};
		auto &src_mr_info           = info->pbr_metallic_roughness_info;

		material.metallic_roughness->metallic_factor  = src_mr_info->metallic_factor;
		material.metallic_roughness->roughness_factor = src_mr_info->roughness_factor;

		memcpy( material.metallic_roughness->base_color_factor, src_mr_info->base_color_factor, sizeof( float ) * 4 );

		material.metallic_roughness->base_color         = create_texture_view( src_mr_info->base_color_texture_view );
		material.metallic_roughness->metallic_roughness = create_texture_view( src_mr_info->metallic_roughness_texture_view );
	}

	material.normal_texture    = create_texture_view( info->normal_texture_view_info );
	material.emissive_texture  = create_texture_view( info->emissive_texture_view_info );
	material.occlusion_texture = create_texture_view( info->occlusion_texture_view_info );

	memcpy( &material.emissive_factor, info->emissive_factor, sizeof( material.emissive_factor ) );

	stage->materials.emplace_back( material );

	return idx;
}

/// \brief add mesh to stage, return index of newly added mesh as it appears in stage.
static uint32_t le_stage_create_mesh( le_stage_o *self, le_mesh_info const *info ) {

	le_mesh_o mesh;

	{
		le_primitive_info const *primitive_info_begin = info->primitives;
		auto                     primitive_infos_end  = primitive_info_begin + info->primitive_count;

		for ( auto p = primitive_info_begin; p != primitive_infos_end; p++ ) {

			le_primitive_o primitive{};

			le_primitive_attribute_info const *attr_info_begin = p->attributes;
			auto                               attr_info_end   = attr_info_begin + p->attributes_count;

			for ( auto attr = attr_info_begin; attr != attr_info_end; attr++ ) {
				le_attribute_o attribute{};
				if ( attr->name ) {
					attribute.name = std::string( attr->name );
				}
				attribute.index        = attr->index;
				attribute.accessor_idx = attr->accessor_idx;
				attribute.type         = attr->type;
				primitive.attributes.emplace_back( attribute );
			}

			auto attr_sort_criteria = []( le_attribute_o const &lhs, le_attribute_o const &rhs ) -> bool {
				// Sort by type first, then index, then morph_target data
				return ( lhs.type != rhs.type
				             ? lhs.type < rhs.type
				             : lhs.index != rhs.index
				                   ? lhs.index < rhs.index
				                   : lhs.morph.target_data < rhs.morph.target_data );
			};

			// -- Parse primitive morph targets (if any)
			//
			// For each morph target fetch attributes, then sort.
			//
			// TODO: conformance checking: number and type or attributes must be the same
			// for each morph target for a given primitive.
			//
			le_morph_target_info_t const *mti_begin = p->morph_targets;
			auto const                    mti_end   = mti_begin + p->morph_targets_count;

			uint32_t morph_target_idx = 0;
			for ( auto mti = mti_begin; mti != mti_end; mti++, morph_target_idx++ ) {

				// Iterate over all attributes within current morph target.

				le_primitive_attribute_info const *attr_info_begin = mti->attributes;
				auto                               attr_info_end   = attr_info_begin + mti->attributes_count;

				for ( auto attr = attr_info_begin; attr != attr_info_end; attr++ ) {
					le_attribute_o attribute{};
					if ( attr->name ) {
						attribute.name = std::string( attr->name );
					}
					attribute.index                  = attr->index;
					attribute.accessor_idx           = attr->accessor_idx;
					attribute.type                   = attr->type;
					attribute.morph.target.is_target = true;
					attribute.morph.target.idx       = uint16_t( morph_target_idx );
					primitive.attributes.emplace_back( attribute );
				}
			}

			primitive.morph_target_count = p->morph_targets_count;

			// -- Sort attributes by type, and morph target, so that they are in
			//    correct order for shader bindings.
			std::sort( primitive.attributes.begin(), primitive.attributes.end(), attr_sort_criteria );

			if ( p->has_indices ) {
				primitive.has_indices          = true;
				primitive.indices_accessor_idx = p->indices_accessor_idx;
			}

			if ( p->has_material ) {
				primitive.has_material = true;
				primitive.material_idx = p->material_idx;
			}

			{
				// -- Calculate the number of joints sets and weights sets used by this
				// primitve using attribute information. We use this to set aside memory
				// for joint matrices; Each joints set requires 4 matrices, one mat4 for
				// each joint. There are 4 joints in a joints set.

				auto count_joints_sets =
				    std::count_if( primitive.attributes.begin(),
				                   primitive.attributes.end(),
				                   []( le_attribute_o const &attr ) {
					                   return attr.type == le_primitive_attribute_info::Type::eJoints;
				                   } );
				auto count_weights_sets =
				    std::count_if( primitive.attributes.begin(),
				                   primitive.attributes.end(),
				                   []( le_attribute_o const &attr ) {
					                   return attr.type == le_primitive_attribute_info::Type::eJointWeights;
				                   } );

				assert( count_joints_sets == count_weights_sets && "number of joint weights and joint sets must be identical for mesh." );

				primitive.num_joints_sets = uint32_t( count_joints_sets );
			}

#ifdef LE_FEATURE_RTX
			{

				{
					// Calculate name (and subsequently name_hash=id) for rtx_blas handle so that we can refer to it
					// as a regular symbolic resource.
					//
					// FIXME: handle name / id should be more unique, and somehow at least reference the scene.

					char rtx_blas_resource_name[ 21 ]{};
					snprintf( rtx_blas_resource_name, sizeof( rtx_blas_resource_name ), "blas_m%06lu_p%06lu", self->meshes.size(), mesh.primitives.size() );

					primitive.rtx_blas_handle = LE_RESOURCE( rtx_blas_resource_name, LeResourceType::eRtxBlas );
				}

				le_rtx_geometry_t geo{};
				auto const &      vertex_accessor    = self->accessors[ primitive.attributes.front().accessor_idx ];
				auto const &      vertex_buffer_view = self->buffer_views[ vertex_accessor.buffer_view_idx ];
				auto const &      vertex_buffer      = self->buffers[ vertex_buffer_view.buffer_idx ];

				geo.vertex_buffer = vertex_buffer->handle;
				geo.vertex_count  = vertex_accessor.count;

				if ( vertex_accessor.component_type == le_num_type::eF32 ) {
					switch ( vertex_accessor.type ) {
					case ( le_compound_num_type::eVec3 ):
						geo.vertex_format = le::Format::eR32G32B32Sfloat;
						break;
					case ( le_compound_num_type::eVec4 ):
						geo.vertex_format = le::Format::eR32G32B32A32Sfloat;
						break;
					default:
						assert( false && "vertex type must be either vec3 or vec4" );
					}
				} else {
					assert( false && "component type other than f32 not implemented" );
				}

				geo.vertex_offset = vertex_buffer_view.byte_offset + vertex_accessor.byte_offset;
				geo.vertex_stride = vertex_buffer_view.byte_length / vertex_accessor.count; // CHECK this is valid.

				if ( primitive.has_indices ) {
					auto &      index_accessor    = self->accessors[ primitive.indices_accessor_idx ];
					auto const &index_buffer_view = self->buffer_views[ index_accessor.buffer_view_idx ];
					auto const &index_buffer      = self->buffers[ index_buffer_view.buffer_idx ];

					geo.index_type   = le_num_type_to_le_index_type( index_accessor.component_type );
					geo.index_count  = index_accessor.count;
					geo.index_buffer = index_buffer->handle;
					geo.index_offset = index_buffer_view.byte_offset + index_accessor.byte_offset;
				}

				LeBuildAccelerationStructureFlags blas_flags = {LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV |
				                                                LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV};

				using namespace le_renderer;
				auto blas_info =
				    renderer_i.create_rtx_blas_info( self->renderer, &geo, 1, &blas_flags );

				le_resource_info_t resource_info{};
				resource_info.type      = LeResourceType::eRtxBlas;
				resource_info.blas.info = blas_info;
				primitive.rtx_blas_info = std::move( resource_info );
			}
#endif

			mesh.primitives.emplace_back( primitive );
		}
	}

	uint32_t idx = uint32_t( self->meshes.size() );
	self->meshes.emplace_back( mesh );
	return idx;
}

/// \brief create nodes graph from list of nodes.
/// nodes may refer to each other by index via their children property - indices may only refer
/// to nodes passed within info. you cannot refer to nodes which are already inside the scene graph.
static uint32_t le_stage_create_nodes( le_stage_o *self, le_node_info const *info, size_t num_nodes ) {
	uint32_t idx = uint32_t( self->nodes.size() );

	// create all these nodes.
	self->nodes.reserve( self->nodes.size() + num_nodes );

	le_node_info const *n_begin = info;
	auto                n_end   = n_begin + num_nodes;

	for ( auto n = n_begin; n != n_end; n++ ) {
		le_node_o *node = new le_node_o{};

		node->local_scale       = n->local_scale->data;
		node->local_rotation    = glm::quat{n->local_rotation->data};
		node->local_translation = n->local_translation->data;

		node->local_transform = n->local_transform->data;

		if ( n->has_mesh ) {
			node->has_mesh = true;
			node->mesh_idx = n->mesh;
		}

		if ( n->has_camera ) {
			node->has_camera = true;
			node->camera_idx = n->camera;
		}

		if ( n->name ) {
			strncpy( node->name, n->name, sizeof( node->name ) );
		}

		self->nodes.push_back( node );
	}

	// -- Resolve child references
	// these are relative to the first index, because we assume
	// that the array of nodes is self-contained.

	for ( size_t i = 0; i != num_nodes; i++ ) {

		if ( info[ i ].child_indices && info[ i ].child_indices_count ) {

			uint32_t const *ci_begin = info[ i ].child_indices;
			auto            ci_end   = ci_begin + info[ i ].child_indices_count;

			self->nodes[ i + idx ]->children.reserve( info[ i ].child_indices_count );

			for ( auto ci = ci_begin; ci != ci_end; ci++ ) {
				self->nodes[ i + idx ]->children.push_back( self->nodes[ ( *ci + idx ) ] );
			}
		}
	}

	return idx;
}

// ----------------------------------------------------------------------

static uint32_t le_stage_create_camera_settings( le_stage_o *self, le_camera_settings_info const *camera_infos, size_t num_cameras ) {

	le_camera_settings_info const *infos_begin = camera_infos;
	auto                           infos_end   = infos_begin + num_cameras;

	uint32_t idx = uint32_t( self->camera_settings.size() );

	self->camera_settings.reserve( self->camera_settings.size() + num_cameras );

	for ( auto info = infos_begin; info != infos_end; info++ ) {

		le_camera_settings_o camera{};

		switch ( info->type ) {
		case ( le_camera_settings_info::Type::ePerspective ): {
			camera.type            = le_camera_settings_o::Type::ePerspective;
			auto &persp_cam        = camera.data.as_perspective;
			persp_cam.fov_y_rad    = info->data.as_perspective.fov_y_rad;
			persp_cam.aspect_ratio = info->data.as_perspective.aspect_ratio;
			persp_cam.z_far        = info->data.as_perspective.z_far;
			persp_cam.z_near       = info->data.as_perspective.z_near;
			break;
		}
		case ( le_camera_settings_info::Type::eOrthographic ): {
			camera.type      = le_camera_settings_o::Type::eOrthographic;
			auto &ortho_cam  = camera.data.as_orthographic;
			ortho_cam.x_mag  = info->data.as_orthographic.x_mag;
			ortho_cam.y_mag  = info->data.as_orthographic.y_mag;
			ortho_cam.z_far  = info->data.as_orthographic.z_far;
			ortho_cam.z_near = info->data.as_orthographic.z_near;
			break;
		}
		default:
			assert( false && "Camera must be either perspective or orthographic" );
			break;
		}

		self->camera_settings.emplace_back( camera );
	}

	return idx;
}

// ----------------------------------------------------------------------

static void le_node_o_set_scene_bit( le_node_o *node, uint8_t bit ) {

	node->scene_bit_flags |= ( 1 << bit );

	for ( le_node_o *n : node->children ) {
		le_node_o_set_scene_bit( n, bit );
	}
}

// ----------------------------------------------------------------------
// An animation sampler is a vector of keyframes. A keyframe contains a time-mapped
// target value, together with two optional interpolation value parameters, and
// an enum signaling the type of interpolation to apply.
static std::vector<le_keyframe_o> le_stage_create_animation_sampler( le_stage_o *self, le_animation_sampler_info *info, LeAnimationTargetType const &target_type ) {

	// We must sample data from accessors and store it into keyframe so that we can
	// apply it faster.

	// Output accessor decides about how many values are loaded and interpolated.
	// This depends on the type of what the accessor points to.

	// Animation data will always only cover one of: T,R,S; or a scalar
	// value (telling us how to interpolate between morph targets).

	// Build keyframes by iterating over accessors and resolving their data.
	//
	// We want to store keyframe data locally, so that the buffer can be discarded, once it
	// has been uploaded.

	// Note that the interpolation type is the same for all
	// elements within a channel.

	std::vector<le_keyframe_o> keyframes;

	// A le_sampler_o is a vector of keyframes
	// we build the sampler by loading keyframe data by resolving accessors

	auto &input_accessor  = self->accessors[ info->input_accesstor_idx ];
	auto &output_accessor = self->accessors[ info->output_accessor_idx ];

	{
		// -- Conformance checking: number of input elements available must match output elements,
		// based on type of interpolation.

		if ( info->interpolation_type == le_animation_sampler_info::InterpolationType::eCubicSpline ) {
			// cubic spline has a multiple of three outputs per input.
			assert( ( output_accessor.count / input_accessor.count ) / 3 > 0 );
		} else {
			// linear, step has one output per input
			assert( output_accessor.count % input_accessor.count == 0 );
		}

		assert( input_accessor.type == le_compound_num_type::eScalar && "animation input accessor type must be scalar (time)" );
		assert( output_accessor.component_type == le_num_type::eFloat && "output num type must be float - other types not yet implemented." );
	}

	le_num_type num_type             = output_accessor.component_type;
	uint32_t    num_output_per_input = output_accessor.count / input_accessor.count;

	assert( num_output_per_input > 0 && "There must be at least one output per input." );

	le_compound_num_type compound_type = output_accessor.type;

	if ( target_type == LeAnimationTargetType::eRotation ) {

		assert( get_num_components( compound_type ) == get_num_components( le_compound_num_type::eQuat4 ) );

		// If type is rotation we change the component type to quaternion
		// so that interpolations can use slerp rather than lerp on this sampler.

		compound_type = le_compound_num_type::eQuat4;
	}

	le_buffer_view_o const &input_buffer_view  = self->buffer_views[ input_accessor.buffer_view_idx ];
	le_buffer_view_o const &output_buffer_view = self->buffer_views[ output_accessor.buffer_view_idx ];

	le_buffer_o const *input_buffer  = self->buffers[ input_buffer_view.buffer_idx ];
	le_buffer_o const *output_buffer = self->buffers[ output_buffer_view.buffer_idx ];

	// Calculate input, and output stride, in case these are not given explicitly

	uint32_t input_stride =
	    input_buffer_view.byte_stride
	        ? input_buffer_view.byte_stride
	        : size_of( input_accessor.component_type ) * get_num_components( input_accessor.type );

	uint32_t output_stride =
	    output_buffer_view.byte_stride
	        ? output_buffer_view.byte_stride
	        : size_of( num_type ) * get_num_components( compound_type );

	char *input  = static_cast<char *>( input_buffer->mem );
	char *output = static_cast<char *>( output_buffer->mem );

	input += input_buffer_view.byte_offset;
	output += output_buffer_view.byte_offset;

	// TODO: check for overflow
	for ( uint32_t ia = 0; ia != input_accessor.count; ia++ ) {
		auto input_data  = input + input_accessor.byte_offset;
		auto output_data = output + output_accessor.byte_offset;

		le_keyframe_o keyframe{};
		keyframe.compound_num_type = compound_type;
		keyframe.num_type          = num_type;
		keyframe.array_size        = uint16_t( num_output_per_input );

		float input_time_seconds = *reinterpret_cast<float *>( input_data );
		keyframe.delta_ticks     = uint64_t( lroundf( LE_TIME_TICKS_PER_SECOND * input_time_seconds ) );

		// For each element in output accessor: load data.
		//
		// This generalises to the case when there is just one accessor,
		// as we assume that the 0 element of an array of type `T` within a union is at
		// the same offset as the scalar version of type `T` within the union.
		for ( uint32_t i = 0; i != num_output_per_input; i++ ) {

			switch ( compound_type ) {
			case le_compound_num_type::eScalar:
				keyframe.data.as_scalar[ i ] = *reinterpret_cast<float *>( output_data );
				break;
			case le_compound_num_type::eVec2:
				keyframe.data.as_vec2[ i ] = *reinterpret_cast<glm::vec2 *>( output_data );
				break;
			case le_compound_num_type::eVec3:
				keyframe.data.as_vec3[ i ] = *reinterpret_cast<glm::vec3 *>( output_data );
				break;
			case le_compound_num_type::eVec4:
				keyframe.data.as_vec4[ i ] = *reinterpret_cast<glm::vec4 *>( output_data );
				break;
			case le_compound_num_type::eQuat4:
				keyframe.data.as_quat[ i ] = *reinterpret_cast<glm::quat *>( output_data );
				break;
			default:
				assert( false ); //unreacahble
			}

			output_data += output_stride;
		}

		keyframes.emplace_back( keyframe );

		input += input_stride;
		output += output_stride * num_output_per_input;
	}

	return keyframes;
}

// ----------------------------------------------------------------------

static uint32_t le_stage_create_animation( le_stage_o *self, le_animation_info const *info ) {

	le_animation_o animation{};

	le_animation_channel_info const *const channel_infos_begin = info->channels;
	le_animation_channel_info const *const channel_infos_end   = info->channels + info->channels_count;

	for ( auto c = channel_infos_begin; c != channel_infos_end; c++ ) {

		// Channel maps an animated value to a specific node field.
		le_animation_channel_o channel{};

		assert( c->animation_sampler_idx < info->samplers_count );

		channel.sampler     = le_stage_create_animation_sampler( self, info->samplers + c->animation_sampler_idx, c->animation_target_type );
		channel.target_node = self->nodes[ c->node_idx ];

		switch ( c->animation_target_type ) {
		case LeAnimationTargetType::eTranslation:
			channel.target_node_element  = &channel.target_node->local_translation[ 0 ];
			channel.target_compound_type = le_compound_num_type::eVec3;
			break;
		case LeAnimationTargetType::eScale:
			channel.target_node_element  = &channel.target_node->local_scale[ 0 ];
			channel.target_compound_type = le_compound_num_type::eVec3;
			break;
		case LeAnimationTargetType::eRotation:
			channel.target_node_element  = &channel.target_node->local_rotation[ 0 ];
			channel.target_compound_type = le_compound_num_type::eQuat4;
			break;
		case LeAnimationTargetType::eWeights:
			channel.target_compound_type = le_compound_num_type::eScalar;
			channel.target_node_element  = channel.target_node->morph_target_weights;
			break;
		default:
			assert( false ); // unreachable
			break;
		}

		if ( !channel.sampler.empty() ) {

			assert( channel.target_compound_type == channel.sampler.front().compound_num_type );

			channel.ticks_offset   = channel.sampler.front().delta_ticks;
			channel.ticks_duration = channel.sampler.back().delta_ticks;

			// For each animation we must find out when it begins, and how long it lasts.
			// we use this to skip over animations if they don't fall within our current
			// time base time, and we also use this to loop/bounce animations depending
			// on where within the repeat cycle the current animation time base falls.

			animation.ticks_offset   = std::min( channel.ticks_offset, animation.ticks_offset );
			animation.ticks_duration = std::max( channel.ticks_duration, animation.ticks_duration );
		}

		animation.channels.emplace_back( channel );
	}

	uint32_t idx = uint32_t( self->animations.size() );
	self->animations.emplace_back( animation );
	return idx;
}

// ----------------------------------------------------------------------

static uint32_t le_stage_create_skin( le_stage_o *self, le_skin_info const *info ) {

	le_skin_o *skin = new le_skin_o{};

	uint32_t const *joint_indices_begin = info->node_indices;
	uint32_t const *joint_indices_end   = joint_indices_begin + info->node_indices_count;

	for ( auto j = joint_indices_begin; j != joint_indices_end; j++ ) {
		skin->joints.push_back( self->nodes.at( *j ) );
	}

	if ( info->has_skeleton_node_index ) {
		skin->skeleton = self->nodes.at( info->skeleton_node_index );
	}

	skin->inverse_bind_matrices.resize( info->node_indices_count );

	if ( info->has_inverse_bind_matrices_accessor_idx ) {
		// we must extract data from accessor and store it locally.
		auto &acc      = self->accessors.at( info->inverse_bind_matrices_accessor_idx );
		auto &buffView = self->buffer_views.at( acc.buffer_view_idx );
		auto &buf      = self->buffers.at( buffView.buffer_idx );

		size_t mat_byte_count = sizeof( glm::mat4 ) * info->node_indices_count;

		assert( buffView.byte_length = uint32_t( mat_byte_count ) && "Buffer must hold enough bytes of memory for joints matrices" );
		assert( buf->owns_mem && "Buffer must own its own memory" );

		glm::mat4 *matrices = reinterpret_cast<glm::mat4 *>( static_cast<char *>( buf->mem ) + buffView.byte_offset + acc.byte_offset );
		memcpy( skin->inverse_bind_matrices.data(), matrices, mat_byte_count );

	} else {

		// If no inverse bind matrices were given, this means that the matrices
		// must be set to identity matrices.

		for ( auto &m : skin->inverse_bind_matrices ) {
			m = glm::identity<glm::mat4>();
		}
	}

	uint32_t skin_idx = uint32_t( self->skins.size() );
	self->skins.emplace_back( skin );
	return skin_idx;
}

// ----------------------------------------------------------------------

static void le_stage_node_set_skin( le_stage_o *self, uint32_t node_idx, uint32_t skin_idx ) {
	self->nodes.at( node_idx )->skin = self->skins.at( skin_idx );
}

// ----------------------------------------------------------------------

static uint32_t le_stage_create_scene( le_stage_o *self, uint32_t *node_idx, uint32_t node_idx_count ) {
	le_scene_o scene;

	uint32_t idx   = uint32_t( self->scenes.size() );
	scene.scene_id = uint8_t( idx );
	scene.root_nodes.reserve( node_idx_count );

	uint32_t const *node_idx_begin = node_idx;
	auto            node_idx_end   = node_idx_begin + node_idx_count;

	for ( auto n = node_idx_begin; n != node_idx_end; n++ ) {
		auto root_node = self->nodes[ *n ];
		scene.root_nodes.push_back( root_node );
		le_node_o_set_scene_bit( root_node, scene.scene_id );
	}

	self->scenes.emplace_back( scene );

	return idx;
}

// ----------------------------------------------------------------------

/// \brief
static bool pass_xfer_setup_resources( le_renderpass_o *pRp, void *user_data ) {
	le::RenderPass rp{pRp};
	auto           stage = static_cast<le_stage_o *>( user_data );

	bool needsUpload = false;

	for ( auto &b : stage->buffers ) {
		needsUpload |= !b->was_transferred;
		if ( !b->was_transferred ) {
			rp.useBufferResource( b->handle, {LE_BUFFER_USAGE_TRANSFER_DST_BIT} );
		}
	}

	for ( auto &img : stage->images ) {
		needsUpload |= !img->was_transferred;
		if ( !img->was_transferred ) {
			rp.useImageResource( img->handle, {LE_IMAGE_USAGE_TRANSFER_DST_BIT} );
		}
	}

	return needsUpload; // false means not to execute the execute callback.
}

// ----------------------------------------------------------------------

static void pass_xfer_resources( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto stage   = static_cast<le_stage_o *>( user_data );
	auto encoder = le::Encoder{encoder_};

	for ( auto &b : stage->buffers ) {
		if ( !b->was_transferred ) {

			// upload buffer
			encoder.writeToBuffer( b->handle, 0, b->mem, b->size );

			// we could possibly free mem once that's done.
			free( b->mem );
			b->mem             = nullptr;
			b->owns_mem        = false;
			b->was_transferred = true;
		}
	}

	for ( auto &img : stage->images ) {
		if ( !img->was_transferred && img->pixels ) {
			using namespace le_pixels;
			void *pix_data = le_pixels_i.get_data( img->pixels );

			auto write_info = le::WriteToImageSettingsBuilder()
			                      .setImageW( img->info.width )
			                      .setImageH( img->info.height )
			                      .setNumMiplevels( img->resource_info.image.mipLevels )
			                      .build();

			encoder.writeToImage( img->handle, write_info, pix_data, img->info.byte_count );

			le_pixels_i.destroy( img->pixels );
			img->pixels          = nullptr;
			img->was_transferred = true;
		}
	}
}

// ----------------------------------------------------------------------

/// \brief add setup and execute callbacks to rendermodule so that rendermodule
/// knows which resources are needed to render the stage.
/// There are two resource types which potentially need uploading: buffers,
/// and images.
static void le_stage_update_render_module( le_stage_o *stage, le_render_module_o *module ) {

	using namespace le_renderer;

	auto rp = le::RenderPass( "Stage_Xfer", LeRenderPassType::LE_RENDER_PASS_TYPE_TRANSFER )
	              .setSetupCallback( stage, pass_xfer_setup_resources )
	              .setExecuteCallback( stage, pass_xfer_resources )
	              .setIsRoot( true );

	// declare buffers
	//
	for ( auto &b : stage->buffers ) {
		render_module_i.declare_resource( module, b->handle, b->resource_info );
	}

	// declare images
	//
	for ( auto &img : stage->images ) {
		render_module_i.declare_resource( module, img->handle, img->resource_info );
	}

	// declare rtx blas resources

	for ( auto &msh : stage->meshes ) {
		for ( auto &p : msh.primitives ) {
			render_module_i.declare_resource( module, p.rtx_blas_handle, p.rtx_blas_info );
		}
	}

	// declare rtx tlas resources

	for ( auto &s : stage->scenes ) {
		render_module_i.declare_resource( module, s.rtx_tlas_handle, s.rtx_tlas_info );
	}

	render_module_i
	    .add_renderpass( module, rp );

#ifdef LE_FEATURE_RTX

	auto cp =
	    le::RenderPass(
	        "Create Accel Structures", LeRenderPassType::LE_RENDER_PASS_TYPE_COMPUTE )
	        .setSetupCallback( stage, []( le_renderpass_o *pRp, void *user_data ) -> bool {
		        // we need to return false after the first time that this pass has been executed,
		        // unless we want to update our bottom level acceleration structures.

		        le::RenderPass rp{pRp};
		        auto           stage = static_cast<le_stage_o *>( user_data );

		        for ( auto &b : stage->buffers ) {
			        rp.useBufferResource( b->handle, {LeBufferUsageFlagBits::LE_BUFFER_USAGE_TRANSFER_SRC_BIT} );
		        }

		        // We don't want to execute this pass by default, but needsUpdate switches to
		        // true if any blas resource needs update, or was not yet uploaded.
		        bool needsUpdate = false;

		        // We define acceleration structures by telling the renderer that we want to write to
		        // the acceleration structure. The renderer will then make sure that the acceleration structure is
		        // actually allocated by the time the update call happens so that it can be written into.

		        for ( auto &msh : stage->meshes ) {
			        for ( auto &p : msh.primitives ) {
				        if ( p.rtx_was_transferred ) {
					        continue;
				        }
				        LeResourceUsageFlags usage{};
				        usage.type                    = LeResourceType::eRtxBlas;
				        usage.as.rtx_blas_usage_flags = {LE_RTX_BLAS_USAGE_WRITE_BIT};
				        rp.useResource( p.rtx_blas_handle, usage );
				        needsUpdate = true;
			        }
		        }

		        // Signal that we will want to update top level acceleration structures for this scene.
		        for ( auto &s : stage->scenes ) {
			        LeResourceUsageFlags usage{};
			        usage.type                    = LeResourceType::eRtxTlas;
			        usage.as.rtx_blas_usage_flags = {LE_RTX_TLAS_USAGE_WRITE_BIT};
			        rp.useResource( s.rtx_tlas_handle, usage );
		        }

		        // TODO: figure out a way to signal that we don't need to upload/update geometries
		        return needsUpdate;
	        } )
	        .setExecuteCallback( stage, []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
		        auto stage = static_cast<le_stage_o *>( user_data );

		        le::Encoder encoder{encoder_};

		        // Build geometries by iterating over all primitives in each mesh.
		        // mesh is referenced by node, and contains n primitives.
		        // primitive describes geometry, and associated material.

		        using namespace le_renderer;
		        std::vector<le_resource_handle_t> blas_infos;

		        // collect all handles over all meshes, primitives so that we may build them in a
		        // next step.

		        for ( auto &m : stage->meshes ) {
			        for ( auto &p : m.primitives ) {
				        // build blas for each primitive.
				        if ( p.rtx_was_transferred == false ) {
					        blas_infos.push_back( p.rtx_blas_handle );
					        p.rtx_was_transferred = true;
				        }
			        }
		        }
		        using namespace le_renderer;
		        encoder_i.build_rtx_blas( encoder_, blas_infos.data(), uint32_t( blas_infos.size() ) );

		        {
			        // Build top level acceleration structures. We build one per scene.
			        //
			        // A top-level acceleration structure is the entry point for a ray trace operation.
			        // There is only one top-level acceleration structure per ray trace call.

			        // A Top-Level acceleration structure has-many instances of bottom-level acceleration structures.
			        // each instance has-a:
			        //
			        //  + transform
			        //  + instanceShaderBindingTableRecordOffset (which is a record count offset into the hit shader binding table)
			        //
			        // We can use instanceShaderBindingTableRecordOffset as a way to record which rtx_material should be associated
			        // with a particular instance. This can depend on materials.
			        //
			        // + in our current setup we have *exactly one* geometry per instance.
			        //
			        // This means we don't have to worry about sbt_record_stride and sbt_record_offset inside our raytracing shaders,
			        // because these values will never be used.

			        size_t scene_index = 0;
			        for ( auto const &scene : stage->scenes ) {

				        // Collect instance data over all instances for the current scene.
				        std::vector<le_rtx_geometry_instance_t> instances;
				        std::vector<le_resource_handle_t>       blas_handles;

				        for ( auto const &n : stage->nodes ) {
					        if ( ( n->scene_bit_flags & ( 1 << scene_index ) ) && n->has_mesh ) {
						        le_rtx_geometry_instance_t instance{};
						        instance.mask                                   = 0xff;
						        instance.flags                                  = 0;
						        instance.instanceShaderBindingTableRecordOffset = 0;                                     // TODO: set this to material-specific offset, based on array of hit shader groups in pipeline.
						        instance.instanceCustomIndex                    = 0;                                     // TODO: set this to material?
						        glm::mat4 transform                             = glm::transpose( n->global_transform ); // must transpose so that
						        memcpy( &instance.transform, &transform, sizeof( instance.transform ) );                 // only copy 12 floats
						        for ( auto const &p : stage->meshes[ n->mesh_idx ].primitives ) {
							        // TODO: set instanceCustomIndex based on material...
							        blas_handles.push_back( p.rtx_blas_handle );
							        instances.push_back( instance );
						        }
					        }
				        }

				        if ( !instances.empty() ) {
					        encoder_i.build_rtx_tlas( encoder_, &scene.rtx_tlas_handle, instances.data(), blas_handles.data(), uint32_t( instances.size() ) );
				        }
				        scene_index++;
			        }
		        }
	        } );

	render_module_i
	    .add_renderpass( module, cp );
#endif
}

static le::IndexType index_type_from_num_type( le_num_type const &tp ) {

	// clang-format off
	switch (tp)
	{
		case le_num_type::eI8 : return le::IndexType::eUint8Ext;
		case le_num_type::eU32: return le::IndexType::eUint32;
		case le_num_type::eU16: return le::IndexType::eUint16;
		default: assert(false);
	}
	// clang-format on
	assert( false ); // unreachable
	return le::IndexType::eUint16;
}

/// \brief update camera matrices given scene and camera index
/// \details searches for camera in stage given scene index, and in-scene camera index.
/// calculates view matrix and projection matrix based on camera type and aspect ratio (w_over_h)
/// if any of `camera_view_matrix` or `camera_projection_matrix` is nullptr, that value will
/// not be calculated and updated.
static bool stage_get_camera( le_stage_o const *stage, uint32_t scene_idx, uint32_t camera_idx, float w_over_h,
                              glm::mat4 *camera_world_matrix,
                              glm::mat4 *camera_view_matrix,
                              glm::mat4 *camera_projection_matrix ) {

	if ( stage->scenes.empty() || scene_idx >= stage->scenes.size() ) {
		return false;
	}

	// ---------| invariant: Scene at `scene_idx` exists.

	auto primary_scene_id = stage->scenes[ scene_idx ].scene_id;

	le_node_o const *found_camera_node = nullptr;

	uint32_t camera_count = 0;

	// find first node which has a camera, and which matches our scene id.
	for ( le_node_o *const node : stage->nodes ) {
		if ( node->has_camera && ( node->scene_bit_flags & ( 1 << primary_scene_id ) ) ) {
			if ( camera_idx == camera_count ) {
				found_camera_node = node;
				break;
			} else {
				camera_count++;
				continue;
			}
		}
	}

	if ( !found_camera_node ) {
		return false;
	}

	// ---------| invariant camera node was found

	// Fetch camera settings based on camera node's camera index.
	le_camera_settings_o const &camera = stage->camera_settings[ found_camera_node->camera_idx ];

	if ( camera_world_matrix ) {
		*camera_world_matrix = found_camera_node->global_transform;
	}

	// Calculate: View Matrix is inverse global transform of the camera's node matrix.

	if ( camera_view_matrix ) {
		*camera_view_matrix = found_camera_node->inverse_global_transform;
	}

	// Calculate: Projection Matrix depends on type of camera.

	if ( camera_projection_matrix && camera.type == le_camera_settings_o::Type::ePerspective ) {
		*camera_projection_matrix =
		    glm::perspective( camera.data.as_perspective.fov_y_rad,
		                      w_over_h,
		                      camera.data.as_perspective.z_near,
		                      camera.data.as_perspective.z_far );
	} else if ( camera_projection_matrix && camera.type == le_camera_settings_o::Type::eOrthographic ) {
		*camera_projection_matrix =
		    glm::ortho( -camera.data.as_orthographic.x_mag,
		                +camera.data.as_orthographic.x_mag,
		                -camera.data.as_orthographic.y_mag,
		                +camera.data.as_orthographic.y_mag,
		                camera.data.as_perspective.z_near,
		                camera.data.as_perspective.z_far );
	}

	return true; // unreachable
}

// ----------------------------------------------------------------------

static void pass_draw( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto draw_params = static_cast<le_stage_api::draw_params_t *>( user_data );
	auto camera      = draw_params->camera;
	auto stage       = draw_params->stage;
	auto encoder     = le::Encoder{encoder_};

	auto extents = encoder.getRenderpassExtent();

	le::Viewport viewports[ 2 ] = {
	    {0.f, float( extents.height ), float( extents.width ), -float( extents.height ), -0.f, 1.f}, // negative viewport means to flip y axis in screen space
	    {0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f},
	};

	// we set projection matrix and view matrix to somehow sensible defaults.
	glm::mat4 camera_projection_matrix = glm::ortho( -0.5f, 0.5f, -0.5f, 0.5f, -1000.f, 1000.f );
	glm::mat4 camera_view_matrix       = glm::identity<glm::mat4>();
	glm::mat4 camera_world_matrix      = glm::identity<glm::mat4>(); // global transform for camera node (==inverse view matrix)

	// update camera from interactive camera
	// if no stage camera is given, and an interactive camera is available
	if ( camera ) {

		using namespace le_camera;
		le_camera_i.set_viewport( camera, viewports[ 0 ] );
		camera_view_matrix       = le_camera_i.get_view_matrix_glm( camera );
		camera_projection_matrix = le_camera_i.get_projection_matrix_glm( camera );
		camera_world_matrix      = glm::inverse( camera_view_matrix );
	} else {
		// Attempt to apply first camera from scene if no interactive camera was set.
		//
		// FIXME: We should cache position of camera node, otherwise we have to iterate
		// the full scenegraph to find the camera.
		stage_get_camera( stage, 0, 0, float( extents.width ) / float( extents.height ),
		                  &camera_world_matrix,
		                  &camera_view_matrix,
		                  &camera_projection_matrix );
	}

	glm::vec4 camera_in_world_space = camera_world_matrix * glm::vec4{0, 0, 0, 1};
	camera_in_world_space /= camera_in_world_space.w;

	struct UboMatrices {
		glm::mat4 viewProjectionMatrix; // (projection * view) matrix
		glm::mat4 normalMatrix;         // given in world-space, which means normalmatrix does not depend on camera, but is transpose(inverse(globalMatrix))
		glm::mat4 modelMatrix;
		glm::vec3 camera_position; // camera position in world space
	};

	UboMatrices mvp_ubo;
	mvp_ubo.viewProjectionMatrix = camera_projection_matrix * camera_view_matrix;
	mvp_ubo.camera_position      = camera_in_world_space;

	struct UboMaterialParams {
		glm::vec4 base_color_factor{1, 1, 1, 1}; // 4*4 = 16 byte alignment, which is largest alignment, and as such forms the struct's base alignment
		float     metallic_factor{1};            // 4 byte alignment, must be at mulitple of 4
		float     roughness_factor{1};           // 4 byte alignment, must be at multiple of 4
	};

	// static_assert(offsetof(UboMaterialParams,metallic_factor)%sizeof(UboMaterialParams::metallic_factor)==0, "must be at offset which is multiple of its size.");

	UboMaterialParams material_params_ubo{};

	struct UboPostProcessing {
		float exposure{1.f};
	};

	UboPostProcessing post_processing_params{};

	// We reserve pretty large amount of memory for joints data - so that we
	// don't have to make allocations inside of the draw loop.
	std::vector<glm::mat4> joints_data( 256 );
	std::vector<glm::mat4> joints_normal_data( 256 );

	for ( le_scene_o const &s : stage->scenes ) {
		for ( le_node_o *n : stage->nodes ) {

			if ( ( n->scene_bit_flags & ( 1 << s.scene_id ) ) && n->has_mesh ) {

				uint32_t joints_count = n->skin ? uint32_t( n->skin->joints.size() ) : 0;

				if ( joints_count ) {
					// Calculate joints matrices for all given joints.
					//
					// TODO: if skin has a skeleton, it should be possible to cache skin data -
					// because it won't change based on what node it is associated to.
					// A skin ideally only needs to be calculated once, and should be re-used.
					//
					// Q: What does GLTF specify must happen if a skin does not specify its skeleton property
					// A: This is not really well defined.
					//
					glm::mat4 const &rootInv =
					    n->skin->skeleton
					        ? n->skin->skeleton->inverse_global_transform
					        : n->inverse_global_transform;

					for ( size_t i = 0; i != n->skin->joints.size(); i++ ) {
						joints_data[ i ] =
						    rootInv *
						    n->skin->joints[ i ]->global_transform *
						    n->skin->inverse_bind_matrices[ i ];
					}

					// Calculate joints normals data - this is basically calculating the
					// normal matrix for each of the world transform matrices in each joint.
					for ( size_t i = 0; i != n->skin->joints.size(); i++ ) {
						joints_normal_data[ i ] = transpose( inverse( joints_data[ i ] ) );
					}
				}

				auto const &mesh = stage->meshes[ n->mesh_idx ];
				for ( auto const &primitive : mesh.primitives ) {

					if ( !primitive.pipeline_state_handle ) {
						std::cerr << "missing pipeline state object for primitive - did you call setup_pipelines on the stage after adding the mesh/primitive?" << std::endl;
						continue;
					}

					mvp_ubo.modelMatrix  = n->global_transform;
					mvp_ubo.normalMatrix = glm::transpose( n->inverse_global_transform );

					encoder
					    .bindGraphicsPipeline( primitive.pipeline_state_handle )
					    .setArgumentData( LE_ARGUMENT_NAME( "UboMatrices" ), &mvp_ubo, sizeof( UboMatrices ) )
					    .setViewports( 0, 1, &viewports[ 0 ] );

					if ( primitive.num_joints_sets && joints_count ) {
						// we must apply joints matrices.
						encoder.setArgumentData( LE_ARGUMENT_NAME( "UboJointMatrices" ), joints_data.data(), sizeof( glm::mat4 ) * joints_count );
						encoder.setArgumentData( LE_ARGUMENT_NAME( "UboJointNormalMatrices" ), joints_normal_data.data(), sizeof( glm::mat4 ) * joints_count );
					}

					if ( primitive.morph_target_count > 0 ) {

						// This primitive has morph targets - we must upload the current weigths for the morph targets.
						//
						// NOTE: We upload the morph target weights tightly packed -
						// this means the shader will receive them as vec4s, which
						// every 4 floats (if available) grouped together into one vec4.
						encoder.setArgumentData( LE_ARGUMENT_NAME( "UboMorphTargetWeights" ), n->morph_target_weights,
						                         sizeof( glm::vec4 ) * ( ( primitive.morph_target_count + 3 ) / 4 ) );

						if ( false ) {
							std::cout << "weights: " << std::dec;
							for ( auto i = 0; i != primitive.morph_target_count; i++ ) {
								std::cout << std::setw( 8 ) << n->morph_target_weights[ i ] << ", ";
							}
							std::cout << std::endl
							          << std::flush;
						}
					}

					if ( primitive.has_material ) {

						auto const &material = stage->materials[ primitive.material_idx ];

						{
							// bind all textures
							uint32_t tex_id = 0;
							for ( auto const &tex : material.texture_handles ) {
								encoder.setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit" ), tex, tex_id++ );
							}
						}

						if ( !material.cached_texture_params.empty() ) {
							// has cached texture parameters
							encoder.setArgumentData( LE_ARGUMENT_NAME( "UboTextureParams" ),
							                         material.cached_texture_params.data(),
							                         sizeof( le_material_o::UboTextureParamsSlice ) * material.cached_texture_params.size() );
						}

						if ( material.metallic_roughness ) {
							auto &      mr         = material.metallic_roughness;
							auto const &base_color = mr->base_color_factor;

							material_params_ubo.base_color_factor =
							    glm::vec4( base_color[ 0 ],
							               base_color[ 1 ],
							               base_color[ 2 ],
							               base_color[ 3 ] );

							material_params_ubo.metallic_factor  = mr->metallic_factor;
							material_params_ubo.roughness_factor = mr->roughness_factor;

							encoder.setArgumentData( LE_ARGUMENT_NAME( "UboMaterialParams" ),
							                         &material_params_ubo, sizeof( UboMaterialParams ) );
						}
					}

					encoder.setArgumentData( LE_ARGUMENT_NAME( "UboPostProcessing" ),
					                         &post_processing_params, sizeof( UboPostProcessing ) );

					// ---- invariant: primitive has pipeline, bindings.

					encoder.bindVertexBuffers( 0, uint32_t( primitive.bindings_buffer_handles.size() ),
					                           primitive.bindings_buffer_handles.data(),
					                           primitive.bindings_buffer_offsets.data() );

					if ( primitive.has_indices ) {

						auto &indices_accessor = stage->accessors[ primitive.indices_accessor_idx ];
						auto &buffer_view      = stage->buffer_views[ indices_accessor.buffer_view_idx ];
						auto &buffer           = stage->buffers[ buffer_view.buffer_idx ];

						encoder.bindIndexBuffer( buffer->handle,
						                         buffer_view.byte_offset,
						                         index_type_from_num_type( indices_accessor.component_type ) );

						encoder.drawIndexed( primitive.index_count );
					} else {

						encoder.draw( primitive.vertex_count );
					}

				} // end for all mesh.primitives
			}
		}
	}
}

// ----------------------------------------------------------------------

/// \brief add setup and execute callbacks to rendermodule so that rendermodule
/// knows which resources are needed to render the stage.
/// There are two resource types which potentially need uploading: buffers,
/// and images.
static void le_stage_draw_into_render_module( le_stage_api::draw_params_t *draw_params, le_render_module_o *module ) {

	using namespace le_renderer;

#ifdef LE_FEATURE_RTX

	{

		auto rtx_pass =
		    le::RenderPass( "Stage Rtx", LeRenderPassType::LE_RENDER_PASS_TYPE_COMPUTE )
		        .setExecuteCallback( draw_params, []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
			        auto draw_params = static_cast<le_stage_api::draw_params_t *>( user_data );
			        auto camera      = draw_params->camera;
			        auto stage       = draw_params->stage;
			        auto encoder     = le::Encoder{encoder_};

			        auto extents = encoder.getRenderpassExtent();

			        le::Viewport viewports[ 2 ] = {
			            {0.f, float( extents.height ), float( extents.width ), -float( extents.height ), -0.f, 1.f}, // negative viewport means to flip y axis in screen space
			            {0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f},
			        };

			        // we set projection matrix and view matrix to somehow sensible defaults.
			        glm::mat4 camera_projection_matrix = glm::ortho( -0.5f, 0.5f, -0.5f, 0.5f, -1000.f, 1000.f );
			        glm::mat4 camera_view_matrix       = glm::identity<glm::mat4>();
			        glm::mat4 camera_world_matrix      = glm::identity<glm::mat4>(); // global transform for camera node (==inverse view matrix)

			        // update camera from interactive camera
			        // if no stage camera is given, and an interactive camera is available
			        if ( camera ) {

				        using namespace le_camera;
				        le_camera_i.set_viewport( camera, viewports[ 0 ] );
				        camera_view_matrix       = le_camera_i.get_view_matrix_glm( camera );
				        camera_projection_matrix = le_camera_i.get_projection_matrix_glm( camera );
				        camera_world_matrix      = glm::inverse( camera_view_matrix );
			        }

			        auto pipeline_manager = encoder.getPipelineManager();

			        // -- Create rtx pso
			        static le_rtxpso_handle rtx_pipeline = []( le_stage_o *stage, le_pipeline_manager_o *pipeline_manager ) {
				        auto shader_raygen      = renderer_i.create_shader_module( stage->renderer, "./resources/shaders/le_stage/rtx/raygen.rgen", {le::ShaderStage::eRaygenBitNv}, nullptr );
				        auto shader_miss        = renderer_i.create_shader_module( stage->renderer, "./resources/shaders/le_stage/rtx/miss.rmiss", {le::ShaderStage::eMissBitNv}, nullptr );
				        auto shader_shadow_miss = renderer_i.create_shader_module( stage->renderer, "./resources/shaders/le_stage/rtx/shadow.rmiss", {le::ShaderStage::eMissBitNv}, nullptr );
				        auto shader_closest_hit = renderer_i.create_shader_module( stage->renderer, "./resources/shaders/le_stage/rtx/closesthit.rchit", {le::ShaderStage::eClosestHitBitNv}, nullptr );

				        // Create rtx pipeline

				        LeRtxPipelineBuilder builder( pipeline_manager );

				        // add shader groups.
				        builder
				            .setShaderGroupRayGen( shader_raygen )                    // shader group handles entry 0
				            .addShaderGroupTriangleHit( shader_closest_hit, nullptr ) // shader group handles entry 1
				            .addShaderGroupMiss( shader_miss )                        // shader group handles entry 2
				            .addShaderGroupMiss( shader_shadow_miss )                 // shader group handles entry 3
				            ;

				        return builder.build();
			        }( stage, pipeline_manager );

			        // At this point we should be able to build the shader binding table for the pipeline.
			        // The SBT is basically a sequence of:
			        // 1 RayGen Group
			        // 1+ Hit Groups
			        // 1+ Miss Groups
			        // Each group may have parameters

			        // Shader binding table is built on encoder - this way we can use the encoder's scratch GPU memory
			        // as SBT memory. SBT is rebuilt every frame.

			        le_shader_binding_table_o *sbt =
			            le::Encoder::ShaderBindingTableBuilder( encoder, rtx_pipeline )
			                .setRayGenIdx( 0 )
			                .addHitIdx( 1 )
			                .addMissIdx( 2 )
			                .addMissIdx( 3 )
			                .build();

			        // shader_binding_table_o* already contains a reference to pipeline,
			        // which means pipeline does not need to be referenced again.
			        encoder.bindRtxPipeline( sbt );

			        // -- set ray tracing arguments

			        // -- call trace rays

			        // encoder.traceRays();
		        } )
		        .setIsRoot( true );

		{

			// -- Signal that we want to use an image to write to.

			rtx_pass
			    .useImageResource( RTX_IMAGE_TARGET_HANDLE, {LE_IMAGE_USAGE_STORAGE_BIT} ); // write

			le_resource_info_t rtx_target_info =
			    le::ImageInfoBuilder()
			        .setFormat( le::Format::eR8G8B8A8Uint ) // 1 byte per cell, 1024x1024 cells
			        .setExtent( 1024, 1024 )                // FIXME: size should match image size - or at least camera.
			        .addUsageFlags( {LE_IMAGE_USAGE_STORAGE_BIT | LE_IMAGE_USAGE_SAMPLED_BIT} )
			        .build();

			render_module_i.declare_resource( module, RTX_IMAGE_TARGET_HANDLE, rtx_target_info );

			// -- Signal that we want to read from bottom-level acceleration structures.

			LeResourceUsageFlags usage_flags{};
			usage_flags.type                    = LeResourceType::eRtxBlas;
			usage_flags.as.rtx_blas_usage_flags = {LeRtxBlasUsageFlagBits::LE_RTX_BLAS_USAGE_READ_BIT};

			for ( auto const &m : draw_params->stage->meshes ) {
				for ( auto const &p : m.primitives ) {
					rtx_pass.useResource( p.rtx_blas_handle, usage_flags );
				}
			}

			// -- Signal that we want to read from top-level acceleration structures.

			usage_flags.type                    = LeResourceType::eRtxTlas;
			usage_flags.as.rtx_tlas_usage_flags = {LeRtxTlasUsageFlagBits::LE_RTX_TLAS_USAGE_READ_BIT};
			for ( auto const &s : draw_params->stage->scenes ) {
				rtx_pass.useResource( s.rtx_tlas_handle, usage_flags );
			}
		}

		render_module_i.add_renderpass( module, rtx_pass );
	}

#endif

	auto stage_draw_pass =
	    le::RenderPass( "Stage Draw", LeRenderPassType::LE_RENDER_PASS_TYPE_DRAW )
	        .setExecuteCallback( draw_params, pass_draw )
	        .addColorAttachment(
	            LE_SWAPCHAIN_IMAGE_HANDLE,
	            le::ImageAttachmentInfoBuilder()
	                .setColorClearValue( LeClearValue( {0.125f, 0.125f, 0.125f, 1.f} ) )
	                .build() )
	        .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_STENCIL_IMAGE" ) );

	for ( auto &b : draw_params->stage->buffers ) {
		stage_draw_pass.useBufferResource( b->handle, {LE_BUFFER_USAGE_INDEX_BUFFER_BIT |
		                                               LE_BUFFER_USAGE_VERTEX_BUFFER_BIT} );
	}

	for ( auto &t : draw_params->stage->textures ) {
		// We must create texture handles for this renderpass.
		stage_draw_pass.sampleTexture(
		    t.texture_handle, {
		                          draw_params->stage->samplers[ t.sampler_idx ],           // samplerInfo
		                          {draw_params->stage->images[ t.image_idx ]->handle, {}}, // imageViewInfo
		                      } );
	}

	render_module_i.add_renderpass( module, stage_draw_pass );
}

/// \brief initialises pipeline state objects associated with each primitive
/// \details pipeline contains materials, vertex and index binding information on each primitive.
/// this will also cache handles for vertex and index data with each primitive.
static void le_stage_setup_pipelines( le_stage_o *stage ) {

	using namespace le_renderer;

	le_pipeline_manager_o *pipeline_manager = renderer_i.get_pipeline_manager( stage->renderer );

	// First, collect all possible shader define permutations based on shader #defines.
	// since this will control how many instances of our shader we must send to the shader compiler.

	std::unordered_map<uint64_t, std::string, IdentityHash> materials_defines_hash_to_defines_str; // map from materials defines hash to define string
	std::vector<uint64_t>                                   defines_hash_at_material_idx;          // table from material inded to materials define hash

	std::unordered_map<uint64_t, std::string, IdentityHash> vertex_input_defines_hash_to_defines_str; // map from vertex input defines hash to define string

	struct shader_defines_signature {
		uint64_t hash_vertex_input_defines;
		uint64_t hash_materials_defines;
	};

	struct shader_programs {
		shader_defines_signature signature;
		le_shader_module_o *     vert;
		le_shader_module_o *     frag;
	};

	std::unordered_map<uint64_t, shader_programs, IdentityHash> shader_map;

	// -- First build up a map of all defines for materials

	defines_hash_at_material_idx.reserve( stage->materials.size() );

	for ( auto &material : stage->materials ) {

		/** -- Update material properties - cache material texture transform matrices.
		*  
		* For each texture, we add a define, and we cache the texture handle and the associated 
		* texture info data with the material. 
		* 
		* For the material ubos we combine these so that all material data can be uploded in a 
		* single ubo.
		* 
		* We do this so that the material has a local cache of all the information
		* it needs when it gets bound on a primitive.
		*
		*/

		std::stringstream defines;

		uint32_t num_textures = 0;

		auto add_texture = [&]( char const *texture_name, const le_texture_view_o *tex_info ) {
			defines << "HAS_" << texture_name << "_MAP,";

			material.texture_handles.push_back( stage->textures[ tex_info->texture_id ].texture_handle );

			if ( tex_info->has_transform ) {
				defines << "HAS_" << texture_name << "_UV_TRANSFORM,";
				// must push back 3 vec3 for the transform matrix
				le_material_o::UboTextureParamsSlice vec_0{};
				le_material_o::UboTextureParamsSlice vec_1{};
				le_material_o::UboTextureParamsSlice vec_2{};
				vec_0.slice.vec = glm::vec4( tex_info->transform[ 0 ], 0 );
				vec_1.slice.vec = glm::vec4( tex_info->transform[ 1 ], 0 );
				vec_2.slice.vec = glm::vec4( tex_info->transform[ 2 ], 0 );
				material.cached_texture_params.emplace_back( vec_0 );
				material.cached_texture_params.emplace_back( vec_1 );
				material.cached_texture_params.emplace_back( vec_2 );
			}

			le_material_o::UboTextureParamsSlice params{};
			params.slice.data.scale   = tex_info->scale;
			params.slice.data.uv_set  = tex_info->uv_set;
			params.slice.data.tex_idx = num_textures;
			material.cached_texture_params.emplace_back( params );

			num_textures++;
		};

		// clang-format off
					if ( material.normal_texture )    add_texture( "NORMAL"     , material.normal_texture    );
					if ( material.occlusion_texture ) add_texture( "OCCLUSION"  , material.occlusion_texture );
		// clang-format on

		if ( material.emissive_texture ) {
			add_texture( "EMISSIVE", material.emissive_texture );
			le_material_o::UboTextureParamsSlice emissive_factor{};
			emissive_factor.slice.vec = glm::vec4( material.emissive_factor, 1 );
			material.cached_texture_params.emplace_back( emissive_factor );
		}

		if ( material.metallic_roughness ) {
			defines << "MATERIAL_METALLICROUGHNESS,";
			if ( material.metallic_roughness->base_color ) {
				add_texture( "BASE_COLOR", material.metallic_roughness->base_color );
			}
			if ( material.metallic_roughness->metallic_roughness ) {
				add_texture( "METALLIC_ROUGHNESS", material.metallic_roughness->metallic_roughness );
			}
		}

		if ( num_textures ) {
			defines << "HAS_TEXTURES=" << num_textures << ",";
		}

		std::string defines_str  = defines.str();
		uint64_t    defines_hash = SpookyHash::Hash64( defines_str.data(), defines_str.size(), 0 );

		defines_hash_at_material_idx.push_back( defines_hash );
		materials_defines_hash_to_defines_str.insert( {defines_hash, std::move( defines_str )} );
	}

	// -- Then build a map of all vertex input defines per primitive

	for ( auto &mesh : stage->meshes ) {

		for ( auto &primitive : mesh.primitives ) {

			std::stringstream defines;

			auto const attr_begin = primitive.attributes.data();
			auto const attr_end   = attr_begin + primitive.attributes.size();

			{
				// Find out if the primitive has morph targets - as this will
				// inform the number of locations which each attribute occupies.
				//
				uint32_t morph_target_count = uint32_t( primitive.morph_target_count );
				uint32_t location           = 0; // current location for attribute.

				if ( morph_target_count ) {
					defines << "MORPH_TARGET_COUNT=" << morph_target_count << ",";
				}

				// TODO: check number of requested locations against device limits.
				//
				// uint32_t max_location = primitive.attributes.size() ;
				// assert(max_location < 17 && "cannot not bind more than 16 locations.");

				for ( auto attr = attr_begin; attr != attr_end; ) {

					// Find out how how many attibutes of the same type each exist
					// (this gives us the array size per attribute)

					uint32_t num_array_elements = 0;
					for ( auto a = attr; a != attr_end && a->type == attr->type; a++, num_array_elements++ ) {
					}

					switch ( attr->type ) {
					case ( le_primitive_attribute_info::Type::ePosition ):
						defines << "LOC_POSITIONS=" << location << ",";
						defines << "NUM_POSITIONS=" << num_array_elements << ",";
						if ( num_array_elements > 1 ) {
							assert( num_array_elements == ( morph_target_count + 1 ) && "number of array elements must match 1 + morph_target_count" );
						}
						break;
					case ( le_primitive_attribute_info::Type::eNormal ):
						defines << "LOC_NORMALS=" << location << ",";
						defines << "NUM_NORMALS=" << num_array_elements << ",";
						if ( num_array_elements > 1 ) {
							assert( num_array_elements == ( morph_target_count + 1 ) && "number of array elements must match 1 + morph_target_count" );
						}
						break;
					case ( le_primitive_attribute_info::Type::eTangent ):
						defines << "LOC_TANGENTS=" << location << ",";
						defines << "NUM_TANGENTS=" << num_array_elements << ",";
						if ( num_array_elements > 1 ) {
							assert( num_array_elements == ( morph_target_count + 1 ) && "number of array elements must match 1 + morph_target_count" );
						}
						break;
					case ( le_primitive_attribute_info::Type::eTexcoord ):
						defines << "LOC_TEXCOORDS=" << location << ",";
						defines << "NUM_TEXCOORDS=" << num_array_elements << ",";
						break;
					case ( le_primitive_attribute_info::Type::eColor ):
						defines << "LOC_COLORS=" << location << ",";
						defines << "NUM_COLORS=" << num_array_elements << ",";
						break;
					case ( le_primitive_attribute_info::Type::eJoints ):
						defines << "LOC_JOINT_SETS=" << location << ",";
						defines << "NUM_JOINT_SETS=" << num_array_elements << ",";
						break;
					case ( le_primitive_attribute_info::Type::eJointWeights ):
						defines << "LOC_JOINT_WEIGHTS_SET=" << location << ",";
						defines << "NUM_JOINT_WEIGHTS_SET=" << num_array_elements << ",";
						break;
					default:
						break;
					}

					location += num_array_elements;
					attr += num_array_elements;
				}
			}

			std::string vertex_input_defines = defines.str();

			uint64_t vertex_input_defines_hash = SpookyHash::Hash64( vertex_input_defines.data(),
			                                                         vertex_input_defines.size(), 0 );

			// Store shader defines string if not yet present
			vertex_input_defines_hash_to_defines_str.insert( {vertex_input_defines_hash, std::move( vertex_input_defines )} );

			// Build a shader defines signature from vertex input defines and materials defines for this primitive.
			// We will use this later to look up the correct shader for the primitive.

			shader_defines_signature signature{};
			if ( primitive.has_material ) {
				signature.hash_materials_defines = defines_hash_at_material_idx[ primitive.material_idx ];
			}
			signature.hash_vertex_input_defines = vertex_input_defines_hash;

			primitive.all_defines_hash = SpookyHash::Hash64( &signature, sizeof( signature ), 0 );

			// Inserting an element with null shader module pointers prepares for the next step, where
			// we will iterate through the map of unique shader signatures, and instantiate shaders
			// based on signatures.
			shader_map.insert( {primitive.all_defines_hash, {signature, nullptr, nullptr}} );
		}
	}

	// Get set of unique combinations of vertex inputs
	// and material defines, and associate a shader with each.

	// Create shaders from unique defines

	for ( auto &shader : shader_map ) {

		std::string defines = vertex_input_defines_hash_to_defines_str[ shader.second.signature.hash_vertex_input_defines ];
		defines             = defines + materials_defines_hash_to_defines_str[ shader.second.signature.hash_materials_defines ];

		std::cout << "Creating shader instance using defines: \n\t'-D" << defines << "'" << std::endl
		          << std::flush;

		shader.second.vert = renderer_i.create_shader_module(
		    stage->renderer,
		    "./resources/shaders/le_stage/gltf.vert",
		    {le::ShaderStage::eVertex}, defines.c_str() );

		shader.second.frag = renderer_i.create_shader_module(
		    stage->renderer,
		    "./resources/shaders/le_stage/metallic-roughness.frag",
		    {le::ShaderStage::eFragment}, defines.c_str() );
	}

	std::unordered_map<le_gpso_handle, uint64_t> pipelineCount; // Only used for debug purposes, count number of unique pipelines

	// associate each primitive with shader matching defines id

	for ( auto &mesh : stage->meshes ) {

		for ( auto &primitive : mesh.primitives ) {

			if ( !primitive.pipeline_state_handle ) {

				// We must create a graphics pipeline state object (GPSO) for this primitive.
				// The PSO captures everything needed for a material.

				// We use an uber-shader to render materials; therefore our shader needs to simulate/handle
				// missing attributes.
				// We deactivate missing attributes via the shader preprocessor.

				// -- Precondition: primitive.attributes are pre-sorted by type, then name,
				// so that name "TEX_COORD_0" appears before "TEX_COORD_1",
				// and normal attributes appear before tangent attributes etc.

				LeGraphicsPipelineBuilder builder( pipeline_manager );

				le_shader_module_o *shader_frag{};
				le_shader_module_o *shader_vert{};

				auto shaders = shader_map.find( primitive.all_defines_hash );

				assert( shaders != shader_map.end() && "shader must be existing, and valid" );

				shader_frag = shaders->second.frag;
				shader_vert = shaders->second.vert;

				if ( shader_frag ) {
					builder.addShaderStage( shader_frag );
				}

				if ( shader_vert ) {
					builder.addShaderStage( shader_vert );
				}

				assert( shader_frag && "shader_frag must be valid" );
				assert( shader_vert && "shader_vert must be valid" );

				// builder
				//				    .withRasterizationState()
				//				    .setCullMode( le::CullModeFlagBits::eBack )
				//				    .setFrontFace( le::FrontFace::eClockwise )
				// 			    .end();

				primitive.bindings_buffer_handles.clear();
				primitive.bindings_buffer_offsets.clear();

				// Calculate Attribute Bindings for this PSO.

				auto &abs =
				    builder.withAttributeBindingState();

				// We must group our attributes by bufferviews.
				//
				// Only if there is interleaving we have more than one attribute per buffer binding,
				// otherwise each binding takes its own buffer.
				//
				// + We must detect interleaving:
				// - 1. gltf requirement: if bufferview.byteStride != 0, then there is interleaving.
				// - 2. if more than one accessor refer to the same bufferview, we have interleaving.
				//
				// We must group by bufferViews: Each bufferview will mean one binding -
				// as each bufferview refers to a buffer, and an offset into that buffer
				//
				// Q: if there is interleaving, does this mean that two or more accessors refer to the same
				// bufferview?
				//
				// A: yes, in that case multiple accessors may refer to the same bufferView, in which case each accessor
				//    defines a byteOffset to specify where it starts within the bufferView.

				// Note: iterator is increased in inner do-while loop
				for ( auto it = primitive.attributes.begin(); it != primitive.attributes.end(); ) {

					le_accessor_o const *accessor        = &stage->accessors[ it->accessor_idx ];
					auto const &         buffer_view     = stage->buffer_views[ accessor->buffer_view_idx ];
					uint32_t             buffer_view_idx = accessor->buffer_view_idx;

					auto &binding = abs.addBinding( uint16_t( buffer_view.byte_stride ) );

					// If no explicit buffer_view.byte_stride was given, we accumulate each accessor's
					// storage size so that we can set the stride of the binding based on the sum total
					// of a bindning's accessors at the end.
					//
					uint16_t accessors_total_byte_count = 0;

					do {

						if ( 0 == buffer_view.byte_stride ) {
							accessors_total_byte_count += size_of( accessor->component_type ) *
							                              get_num_components( accessor->type );
						}

						// Add attributes until buffer_view_idx changes.
						// in which case we want to open the next binding.
						//
						// if the buffer_view_idx doesn't change, this means that we are still within the same
						// binding, because then we have interleaving.
						//
						// every accessor mapping the same buffer will go into the same binding number
						// because that's what the encoder will bind in the end.
						binding.addAttribute( uint16_t( accessor->byte_offset ),
						                      accessor->component_type,
						                      get_num_components( accessor->type ), // calculate number of components
						                      accessor->is_normalized );

						it++;

						// prepare accessor for next iteration.
						if ( it != primitive.attributes.end() ) {
							accessor = &stage->accessors[ it->accessor_idx ];
						}

					} while ( it != primitive.attributes.end() &&
					          buffer_view_idx == accessor->buffer_view_idx );

					// Cache binding for primitive so that we can bind faster.

					primitive.bindings_buffer_handles.push_back( stage->buffers[ buffer_view.buffer_idx ]->handle );
					primitive.bindings_buffer_offsets.push_back( buffer_view.byte_offset );

					if ( 0 == buffer_view.byte_stride ) {
						// If stride was not explicitly specified - this will be non-zero,
						// telling us that we must set stride here.
						binding.setStride( accessors_total_byte_count );
					}

					binding.end();
				}

				// Fill in number of vertices for primitive
				if ( !primitive.attributes.empty() ) {
					primitive.vertex_count = stage->accessors[ primitive.attributes.front().accessor_idx ].count;
				}

				if ( primitive.has_indices ) {
					primitive.index_count = stage->accessors[ primitive.indices_accessor_idx ].count;
				}

				primitive.pipeline_state_handle = builder.build();
				pipelineCount[ primitive.pipeline_state_handle ]++;
			}

		} // end for all mesh.primitives
	}     // end for all meshes

	std::cout << "Pipelines in use: \n";
	for ( auto &p : pipelineCount ) {
		std::cout << std::hex << p.first << ": " << std::dec << p.second << std::endl;
	}
	std::cout << std::flush;

#ifdef LE_FEATURE_RTX
	{
		// -- Create top level acceleration structure for each scene.

		// For each TLAS we must know the number of nodes which contribute,
		// as each node maps to one instance.

		std::vector<uint32_t> node_count_per_scene;

		uint32_t const scenes_count = uint32_t( stage->scenes.size() );
		node_count_per_scene.resize( scenes_count, 0 );

		// -- Count mesh nodes per scene:
		for ( auto const &n : stage->nodes ) {
			if ( n->has_mesh ) {
				for ( uint32_t i = 0; i != scenes_count; i++ ) {
					if ( 1 << i & n->scene_bit_flags ) {
						node_count_per_scene[ i ]++;
					}
				}
			}
		}

		// Associate a TLAS with each scene.
		for ( uint32_t i = 0; i != scenes_count; i++ ) {
			char rtx_tlas_resource_name[ 17 ]{};
			snprintf( rtx_tlas_resource_name, sizeof( rtx_tlas_resource_name ), "tlas_%08u", i );

			// -- Create top-level accelerator for this scene
			stage->scenes[ i ].rtx_tlas_handle = LE_RESOURCE( rtx_tlas_resource_name, LeResourceType::eRtxTlas );

			LeBuildAccelerationStructureFlags tlas_flags =
			    {LE_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV |
			     LE_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV};

			le_resource_info_t resource_info{};

			resource_info.type      = LeResourceType::eRtxTlas;
			resource_info.tlas.info = renderer_i.create_rtx_tlas_info( stage->renderer, node_count_per_scene[ i ], &tlas_flags );

			stage->scenes[ i ].rtx_tlas_info = std::move( resource_info );
		}
	}

#endif
}

// ----------------------------------------------------------------------

template <typename T>
void lerp_animation_target( T *target, T const &val_previous, T const &val_next, float norm_t ) {
	T blend = glm::mix( val_previous, val_next, norm_t );
	if ( target ) {
		*target = blend;
	}
}

// Quaternions need to be slerped instead of mixed. They also must be normalised before application.
template <>
void lerp_animation_target<glm::quat>( glm::quat *target, glm::quat const &val_previous, glm::quat const &val_next, float norm_t ) {
	glm::quat blend = glm::slerp( val_previous, val_next, norm_t );
	blend           = glm::normalize( blend );
	if ( target ) {
		*target = blend;
	}
}

// ----------------------------------------------------------------------

static void apply_animation_channel( le_animation_channel_o const &channel, uint64_t ticks ) {

	if ( channel.sampler.size() < 2 ) {
		return;
	}

	// -------- invariant: sampler has at least two elements.

	le_keyframe_o const *sampler_begin = channel.sampler.data();
	le_keyframe_o const *sampler_end   = sampler_begin + channel.sampler.size();

	le_keyframe_o const *previous_key = sampler_begin;
	le_keyframe_o const *next_key     = sampler_begin + 1;

	while ( ( next_key->delta_ticks < ticks ) && ( next_key + 1 != sampler_end ) ) {
		std::swap( previous_key, next_key );
		next_key++;
	}

	if ( ticks > next_key->delta_ticks ) {
		// we're done here.

		// TODO:
		// we should probably make sure that the target value is
		// set to the value of the last keyframe, just in case
		// this channel gets oversampled.

		return;
	}

	float norm_t = 0.f; // normalised time in domain [previous_key..[next_key

	// -- calculate normalised time.

	norm_t = ( ticks - previous_key->delta_ticks ) /
	         float( next_key->delta_ticks - previous_key->delta_ticks );

	norm_t = glm::clamp( norm_t, 0.f, 1.f );

	// apply data to node pointed in channel, based on type.

	assert( previous_key->array_size == next_key->array_size && "keys must have same array size" );

	switch ( channel.target_compound_type ) {
	case ( le_compound_num_type::eScalar ): {
		for ( size_t i = 0; i != previous_key->array_size; i++ ) {
			// If more than one scalar element, this most likely means that
			// we're updating weights.
			lerp_animation_target<float>( static_cast<float *>( channel.target_node_element ) + i,
			                              previous_key->data.as_scalar[ i ], next_key->data.as_scalar[ i ], norm_t );
		}
		break;
	}
	case ( le_compound_num_type::eVec2 ): {
		lerp_animation_target<glm::vec2>( static_cast<glm::vec2 *>( channel.target_node_element ),
		                                  previous_key->data.as_vec2[ 0 ], next_key->data.as_vec2[ 0 ], norm_t );
		break;
	}
	case ( le_compound_num_type::eVec3 ): {
		lerp_animation_target<glm::vec3>( static_cast<glm::vec3 *>( channel.target_node_element ),
		                                  previous_key->data.as_vec3[ 0 ], next_key->data.as_vec3[ 0 ], norm_t );
		break;
	}
	case ( le_compound_num_type::eVec4 ): {
		lerp_animation_target<glm::vec4>( static_cast<glm::vec4 *>( channel.target_node_element ),
		                                  previous_key->data.as_vec4[ 0 ], next_key->data.as_vec4[ 0 ], norm_t );
		break;
	}
	case ( le_compound_num_type::eQuat4 ): {
		// note that we distinguish between quat and vec, because interpolation type is different
		lerp_animation_target<glm::quat>( static_cast<glm::quat *>( channel.target_node_element ),
		                                  previous_key->data.as_quat[ 0 ], next_key->data.as_quat[ 0 ], norm_t );
		break;
	}
	default:
		break;
	}

	channel.target_node->local_transform_cached = false;
}

// ----------------------------------------------------------------------

static void traverse_node( le_node_o *parent ) {

	for ( le_node_o *c : parent->children ) {
		c->global_transform         = parent->global_transform * c->local_transform;
		c->inverse_global_transform = glm::inverse( c->global_transform );
		traverse_node( c );
		c->global_transform_chached = true;
	}
}

// ----------------------------------------------------------------------

/// \brief updates scene graph - call this exactly once per frame.
static void le_stage_update( le_stage_o *self ) {

	// -- update animations if these exist - animations apply to
	// node's local transforms TRS, W

	if ( self->timebase ) {
		using namespace le_timebase;

		uint64_t current_ticks = le_timebase_i.get_current_ticks( self->timebase );

		if ( !self->animations.empty() ) {
			// for each animation: find current keyframe

			for ( auto const &a : self->animations ) {

				uint64_t animation_time = current_ticks - a.ticks_offset;

				auto play_mode = le_animation_o::PlaybackMode::eBounce;

				switch ( play_mode ) {
					//				switch ( a.playback_mode ) {

				case le_animation_o::PlaybackMode::eForward:
					break;
				case le_animation_o::PlaybackMode::eLoop:
					animation_time = ( animation_time ) % a.ticks_duration;
					break;
				case le_animation_o::PlaybackMode::eBounce:
					animation_time = a.ticks_duration - abs( ( animation_time % ( 2 * a.ticks_duration ) - a.ticks_duration ) );
					break;
				}

				for ( auto const &c : a.channels ) {
					apply_animation_channel( c, animation_time );
				}
			}

			// apply keyframe value to node.
		}
	}

	// -- Update node's local transform matrices from node's T,R,S properties.

	for ( le_node_o *n : self->nodes ) {
		if ( false == n->local_transform_cached ) {

			glm::mat4 m =
			    glm::translate( glm::mat4( 1.f ), n->local_translation ) * // translate
			    glm::mat4_cast( n->local_rotation ) *                      // rotate
			    glm::scale( glm::mat4( 1.f ), n->local_scale )             // scale
			    ;

			n->local_transform = m;

			n->local_transform_cached = true;
		}
	}

	// -- Update global transform matrices.
	// -- while we are at it, we also calculate inverse global transforms.
	// -- recurse over nodes, starting with root nodes of scene.

	for ( le_scene_o const &s : self->scenes ) {
		for ( le_node_o *n : s.root_nodes ) {
			n->global_transform         = n->local_transform;
			n->inverse_global_transform = glm::inverse( n->global_transform );
			traverse_node( n );
		}
	}
}

// ----------------------------------------------------------------------

static le_stage_o *le_stage_create( le_renderer_o *renderer, le_timebase_o *timebase ) {
	auto self      = new le_stage_o{};
	self->renderer = renderer;
	self->timebase = timebase;
	return self;
}

// ----------------------------------------------------------------------

static void le_stage_destroy( le_stage_o *self ) {

	for ( auto &img : self->images ) {
		if ( img->pixels ) {
			le_pixels::le_pixels_i.destroy( img->pixels );
			img->pixels = nullptr;
		}
		delete img;
	}

	for ( auto &b : self->buffers ) {
		if ( b->owns_mem && b->mem && b->size ) {
			free( b->mem );
		}
		delete b;
	}

	for ( auto &n : self->nodes ) {
		delete n;
	}

	for ( auto &s : self->skins ) {
		delete s;
	}

	for ( auto &m : self->materials ) {
		if ( m.metallic_roughness ) {
			delete ( m.metallic_roughness->base_color );
			delete ( m.metallic_roughness->metallic_roughness );
			delete m.metallic_roughness;
			delete m.normal_texture;
			delete m.occlusion_texture;
			delete m.emissive_texture;
		}
	}

	self->buffers.clear();
	self->buffer_handles.clear();

	delete self;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_stage, api ) {
	auto &le_stage_i = static_cast<le_stage_api *>( api )->le_stage_i;

	le_stage_i.create  = le_stage_create;
	le_stage_i.destroy = le_stage_destroy;

	le_stage_i.update = le_stage_update;

	le_stage_i.update_rendermodule = le_stage_update_render_module;
	le_stage_i.draw_into_module    = le_stage_draw_into_render_module;

	le_stage_i.setup_pipelines = le_stage_setup_pipelines;

	le_stage_i.create_image_from_memory    = le_stage_create_image_from_memory;
	le_stage_i.create_image_from_file_path = le_stage_create_image_from_file_path;

	le_stage_i.create_texture         = le_stage_create_texture;
	le_stage_i.create_sampler         = le_stage_create_sampler;
	le_stage_i.create_buffer          = le_stage_create_buffer;
	le_stage_i.create_buffer_view     = le_stage_create_buffer_view;
	le_stage_i.create_accessor        = le_stage_create_accessor;
	le_stage_i.create_material        = le_stage_create_material;
	le_stage_i.create_mesh            = le_stage_create_mesh;
	le_stage_i.create_camera_settings = le_stage_create_camera_settings;
	le_stage_i.create_nodes           = le_stage_create_nodes;
	le_stage_i.create_animation       = le_stage_create_animation;
	le_stage_i.create_skin            = le_stage_create_skin;
	le_stage_i.node_set_skin          = le_stage_node_set_skin;
	le_stage_i.create_scene           = le_stage_create_scene;
}
