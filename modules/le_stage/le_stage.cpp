#include "le_stage.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"
#include "le_stage_types.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#include "le_camera/le_camera.h"

#include "3rdparty/src/spooky/SpookyV2.h"

#include "string.h" // for memcpy

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

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
	std::string                       name;
	uint32_t                          index;
	uint32_t                          accessor_idx;
};

struct le_primitive_o {

	std::vector<uint64_t>             bindings_buffer_offsets;
	std::vector<le_resource_handle_t> bindings_buffer_handles; // cached bufferviews sorted and grouped based on accessors

	uint32_t vertex_count; // number of POSITION vertices, used to figure out draw call param
	uint32_t index_count;  // number of INDICES, if any.

	le_gpso_handle_t *          pipeline_state_handle; // contains material shaders, and vertex input state
	std::vector<le_attribute_o> attributes;
	bool                        has_indices;
	uint32_t                    indices_accessor_idx;
};

// has many primitives
struct le_mesh_o {
	std::vector<le_primitive_o> primitives;
};

struct le_node_o {
	glm::mat4 global_transform;
	glm::mat4 local_transform;

	glm::vec3 local_translation;
	glm::quat local_rotation;
	glm::vec3 local_scale;

	char name[ 32 ];

	bool local_transform_cached;   // whether local transform is accurate wrt local[translation|rotation|scale]
	bool global_transform_chached; // whether global transform is current

	bool     has_mesh;
	uint32_t mesh_idx;

	bool     has_camera;
	uint32_t camera_idx;

	uint64_t scene_bit_flags; // one bit for every scene this node is included in

	std::vector<le_node_o *> children; // non-owning
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
};

// Owns all the data
struct le_stage_o {

	le_renderer_o *renderer; // non-owning.

	std::vector<le_scene_o> scenes;

	std::vector<le_node_o *> nodes; // owning.

	std::vector<le_camera_settings_o> camera_settings;

	std::vector<le_mesh_o> meshes;

	std::vector<le_accessor_o>    accessors;
	std::vector<le_buffer_view_o> buffer_views;

	std::vector<le_buffer_o *>        buffers; // owning
	std::vector<le_resource_handle_t> buffer_handles;
};

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
		} else {
			buffer_handle_idx++;
		}
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
	accessor.byte_offset     = info->byte_offset;
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

		if ( true ) {

			char *const index_ptr       = static_cast<char *>( indices_buffer->mem ) + indices_buffer_view.byte_offset;
			void *const sparse_data_src = static_cast<char *>( sparse_data_buffer->mem ) + sparse_data_view.byte_offset;

			uint32_t stride       = view_info.byte_stride;
			uint32_t index_stride = size_of( info->sparse_accessor.indices_component_type );

			for ( uint32_t src_index = 0; src_index != info->sparse_accessor.count; src_index++ ) {

				uint32_t dst_index = 0;

				if ( info->sparse_accessor.indices_component_type == le_num_type::eU16 ) {
					dst_index = ( uint16_t & )index_ptr[ index_stride * src_index ];
				} else if ( info->sparse_accessor.indices_component_type == le_num_type::eU32 ) {
					dst_index = ( uint32_t & )index_ptr[ index_stride * src_index ];
				} else {
					assert( false && "index type must be one of u16 or u32" );
				}

				memcpy( static_cast<char *>( dst_buffer->mem ) + stride * dst_index, // change in dest at sparse index
				        static_cast<char *>( sparse_data_src ) + stride * src_index, // from data
				        stride );
			}
		}

		// we patch accessor here
		accessor.buffer_view_idx = dst_view_idx;
	}

	uint32_t idx = uint32_t( self->accessors.size() );
	self->accessors.emplace_back( accessor );
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
			auto                               attr_info_end   = attr_info_begin + p->attribute_count;

			for ( auto attr = attr_info_begin; attr != attr_info_end; attr++ ) {
				le_attribute_o attribute{};
				//				attribute.name = attr->name; // TODO: copy name if available
				attribute.name         = "";
				attribute.index        = attr->index;
				attribute.accessor_idx = attr->accessor_idx;
				attribute.type         = attr->type;
				primitive.attributes.emplace_back( attribute );
			}

			// sort attributes by type so that they are in the correct order for shader bindings.

			std::sort( primitive.attributes.begin(), primitive.attributes.end(),
			           []( le_attribute_o const &lhs, le_attribute_o const &rhs ) -> bool {
				           return ( lhs.type < rhs.type );
			           } );

			if ( p->has_indices ) {
				primitive.has_indices          = true;
				primitive.indices_accessor_idx = p->indices_accessor_idx;
			}

			mesh.primitives.emplace_back( primitive );
		}
	}

	uint32_t idx = uint32_t( self->meshes.size() );
	self->meshes.emplace_back( mesh );
	return idx;
}

/// \brief creat nodes graph from list of nodes.
/// nodes may refer to each other by index via their children property - indices may only refer
/// to nodes passed within info. you cannot refer to nodes which are already inside the scene graph.
static uint32_t le_stage_create_nodes( le_stage_o *self, le_node_info *info, size_t num_nodes ) {
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

static uint32_t le_stage_create_camera_settings( le_stage_o *self, le_camera_settings_info *camera_infos, size_t num_cameras ) {

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

	for ( auto &b : stage->buffers ) {
		render_module_i.declare_resource( module, b->handle, b->resource_info );
	}

	render_module_i.add_renderpass( module, rp );
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

// ----------------------------------------------------------------------

static void traverse_node( le_node_o *parent ) {

	for ( le_node_o *c : parent->children ) {
		c->global_transform = parent->global_transform * c->local_transform;
		traverse_node( c );
		c->global_transform_chached = true;
	}
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

	{
		using namespace le_camera;
		le_camera_i.set_viewport( camera, viewports[ 0 ] );
		camera_view_matrix       = le_camera_i.get_view_matrix_glm( camera );
		camera_projection_matrix = le_camera_i.get_projection_matrix_glm( camera );
	}

	// -- find the first available camera within the node graph which is
	// tagged as belonging to the first scene.

	if ( false && !stage->scenes.empty() ) {
		auto primary_scene_id = stage->scenes.front().scene_id;

		le_node_o const *found_camera_node = nullptr;

		// find first node which has a camera, and which matches our scene id.
		for ( le_node_o *const node : stage->nodes ) {
			if ( node->has_camera && ( node->scene_bit_flags & ( 1 << primary_scene_id ) ) ) {
				found_camera_node = node;
				break;
			}
		}

		if ( found_camera_node ) {
			le_camera_settings_o const &camera = stage->camera_settings[ found_camera_node->camera_idx ];

			// view matrix is inverse global camera found_camera_node matrix.

			camera_view_matrix = glm::inverse( found_camera_node->global_transform );

			// projection matrix depends on type of camera.

			if ( camera.type == le_camera_settings_o::Type::ePerspective ) {
				camera_projection_matrix =
				    glm::perspective( camera.data.as_perspective.fov_y_rad,
				                      float( extents.width ) / float( extents.height ),
				                      camera.data.as_perspective.z_near,
				                      camera.data.as_perspective.z_far );
			} else if ( camera.type == le_camera_settings_o::Type::eOrthographic ) {
				camera_projection_matrix =
				    glm::ortho( -camera.data.as_orthographic.x_mag,
				                +camera.data.as_orthographic.x_mag,
				                -camera.data.as_orthographic.y_mag,
				                +camera.data.as_orthographic.y_mag,
				                camera.data.as_perspective.z_near,
				                camera.data.as_perspective.z_far );
			}
		}
	}

	for ( le_scene_o const &s : stage->scenes ) {
		for ( le_node_o *n : stage->nodes ) {
			if ( ( n->scene_bit_flags & ( 1 << s.scene_id ) ) && n->has_mesh ) {

				auto const &mesh = stage->meshes[ n->mesh_idx ];
				for ( auto const &primitive : mesh.primitives ) {

					if ( !primitive.pipeline_state_handle ) {
						std::cerr << "missing pipeleine state object for primitive - did you call setup_pipelines on the stage after adding the mesh/primitive?" << std::endl;
						continue;
					}

					glm::mat4 mvp = camera_projection_matrix * camera_view_matrix * glm::scale( glm::mat4{1}, glm::vec3( 1 ) ) * n->global_transform;

					encoder
					    .bindGraphicsPipeline( primitive.pipeline_state_handle )
					    .setArgumentData( LE_ARGUMENT_NAME( "MvpUbo" ), &mvp, sizeof( glm::mat4 ) )
					    .setViewports( 0, 1, &viewports[ 0 ] );

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

	auto rp = le::RenderPass( "Stage Draw", LeRenderPassType::LE_RENDER_PASS_TYPE_DRAW )
	              .setExecuteCallback( draw_params, pass_draw )
	              .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE )
	              .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_STENCIL_IMAGE" ) )
	              .setIsRoot( true );

	for ( auto &b : draw_params->stage->buffers ) {
		rp.useBufferResource( b->handle, {LE_BUFFER_USAGE_INDEX_BUFFER_BIT |
		                                  LE_BUFFER_USAGE_VERTEX_BUFFER_BIT} );
	}

	render_module_i.add_renderpass( module, rp );
}

/// \brief initialises pipeline state objects associated with each primitive
/// \details pipeline contains materials, vertex and index binding information on each primitive.
/// this will also cache handles for vertex and index data with each primitive.
static void le_stage_setup_pipelines( le_stage_o *stage ) {

	using namespace le_renderer;

	le_pipeline_manager_o *pipeline_manager = renderer_i.get_pipeline_manager( stage->renderer );

	for ( auto &mesh : stage->meshes ) {

		for ( auto &primitive : mesh.primitives ) {

			if ( !primitive.pipeline_state_handle ) {
				// primitive does not yet have pipeline - we must create a pipeline
				// for this primitive.

				std::stringstream defines;

				uint32_t location = 0;
				for ( auto it : primitive.attributes ) {
					// clang-format off
					switch ( it.type ) {
					case ( le_primitive_attribute_info::Type::eNormal    ): defines << "HAS_NORMALS="   << ++location << "," ; break;
					case ( le_primitive_attribute_info::Type::eTangent   ): defines << "HAS_TANGENTS="  << ++location << "," ; break;
					case ( le_primitive_attribute_info::Type::eTexcoord  ): defines << "HAS_TEXCOORDS=" << ++location << "," ; break;
					case ( le_primitive_attribute_info::Type::eColor     ): defines << "HAS_COLORS="    << ++location << "," ; break;
					case ( le_primitive_attribute_info::Type::eJoints    ): defines << "HAS_JOINTS="    << ++location << "," ; break;
					case ( le_primitive_attribute_info::Type::eWeights   ): defines << "HAS_WEIGHTS="   << ++location << "," ; break;
					default: break;
					}
					// clang-format on
				}

				// std::cout << "adding the following defines: " << defines.str() << std::flush << std::endl;

				auto shader_vert = renderer_i.create_shader_module( stage->renderer, "./local_resources/shaders/gltf.vert", {le::ShaderStage::eVertex}, defines.str().c_str() );
				auto shader_frag = renderer_i.create_shader_module( stage->renderer, "./local_resources/shaders/gltf.frag", {le::ShaderStage::eFragment}, defines.str().c_str() );

				LeGraphicsPipelineBuilder builder( pipeline_manager );

				builder
				    .addShaderStage( shader_frag )
				    .addShaderStage( shader_vert )
				    //				    .withRasterizationState()
				    //				    .setCullMode( le::CullModeFlagBits::eBack )
				    //				    .setFrontFace( le::FrontFace::eClockwise )
				    // 			    .end()
				    ;

				primitive.bindings_buffer_handles.clear();
				primitive.bindings_buffer_offsets.clear();

				auto &abs =
				    builder.withAttributeBindingState();

				// We must group our attributes by bufferviews.

				// only if there is interleaving we have more than one attribute per buffer binding
				// otherwise each binding takes its own buffer.

				// + We must detect interleaving:
				// - 1. gltf requirement: if bufferview.byteStride != 0, then there is interleaving.
				// - 2. if more than one accessor refer to the same bufferview, we have interleaving.

				// + we must group by bufferViews.
				//   each bufferview will mean one binding - as a bufferview refers to a buffer, and an offset into the buffer

				// Q: if there is interleaving, does this mean that two or more accessors refer to the same
				// bufferview?

				//	+ multiple accessors may refer to the same bufferView, in which case each accessor
				//    defines a byteOffset to specify where it starts within the bufferView.

				// we must also detect attribute types - so that we can make sure that our shader has
				// the exact number of attributes.

				// our shader needs to simulate missing attributes, and we deactivate missing attributes via the shader preprocessor.

				// attributes are pre-sorted by type.

				// Note: iterator is increased in inner loop
				for ( auto it = primitive.attributes.begin(); it != primitive.attributes.end(); ) {

					le_accessor_o const *accessor        = &stage->accessors[ it->accessor_idx ];
					auto const &         buffer_view     = stage->buffer_views[ accessor->buffer_view_idx ];
					uint32_t             buffer_view_idx = accessor->buffer_view_idx;

					auto &binding = abs.addBinding( buffer_view.byte_stride );

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

						// if the buffer_view_idx doesn't change, this means that we are still within the same
						// binding, because then we have interleaving.

						// every accessor mapping the same buffer will go into the same binding number
						// because that's what the encoder will bind in the end.
						// if things are interleaved we
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
			}

		} // end for all mesh.primitives
	}     // end for all meshes
}

// ----------------------------------------------------------------------

/// \brief updates scene graph - call this exactly once per frame.
static void le_stage_update( le_stage_o *self ) {
	// -- ensure all nodes have local matrices which reflect their T,R,S properties.

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

	// -- we need to update global transform matrices.
	// -- recurse over nodes, starting with root nodes of scene.

	for ( le_scene_o const &s : self->scenes ) {
		for ( le_node_o *n : s.root_nodes ) {
			n->global_transform = n->local_transform;
			traverse_node( n );
		}
	}
}

// ----------------------------------------------------------------------

static le_stage_o *le_stage_create( le_renderer_o *renderer ) {
	auto self      = new le_stage_o{};
	self->renderer = renderer;
	return self;
}

// ----------------------------------------------------------------------

static void le_stage_destroy( le_stage_o *self ) {

	for ( auto &b : self->buffers ) {
		if ( b->owns_mem && b->mem && b->size ) {
			free( b->mem );
		}
		delete b;
	}

	for ( auto &n : self->nodes ) {
		if ( n ) {
			delete n;
		}
	}

	self->buffers.clear();
	self->buffer_handles.clear();

	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_stage_api( void *api ) {
	auto &le_stage_i = static_cast<le_stage_api *>( api )->le_stage_i;

	le_stage_i.create  = le_stage_create;
	le_stage_i.destroy = le_stage_destroy;

	le_stage_i.update = le_stage_update;

	le_stage_i.update_rendermodule = le_stage_update_render_module;
	le_stage_i.draw_into_module    = le_stage_draw_into_render_module;

	le_stage_i.setup_pipelines = le_stage_setup_pipelines;

	le_stage_i.create_buffer          = le_stage_create_buffer;
	le_stage_i.create_buffer_view     = le_stage_create_buffer_view;
	le_stage_i.create_accessor        = le_stage_create_accessor;
	le_stage_i.create_mesh            = le_stage_create_mesh;
	le_stage_i.create_nodes           = le_stage_create_nodes;
	le_stage_i.create_camera_settings = le_stage_create_camera_settings;
	le_stage_i.create_scene           = le_stage_create_scene;
}
