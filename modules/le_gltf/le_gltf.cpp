#include "le_gltf.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/cgltf/cgltf.h"
#include "le_stage/le_stage.h"
#include "le_stage/le_stage_types.h"

#include <vector>
#include <string>
#include <unordered_map>
#include "string.h" // for memcpy

#define GLM_ENABLE_EXPERIMENTAL
#include "glm.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"
#include <glm/gtx/matrix_decompose.hpp>

// Wrappers so that we can pass data via opaque pointers across header boundaries
struct glm_vec3_t {
	glm::vec3 data;
};
struct glm_vec4_t {
	glm::vec4 data;
};
struct glm_quat_t {
	glm::quat data;
};
struct glm_mat4_t {
	glm::mat4 data;
};

/*

How to use this library:

1. load file (this will also load associated assets into memory)
2. upload assets (this will free associated assets once uploaded)

*/

// when you create a mesh, you do it through the stage - which manages/stores the data for that mesh
// the stage may also optimise data

struct le_gltf_o {
	cgltf_options options = {};
	cgltf_data *  data    = nullptr;
	cgltf_result  result  = {};
};

// ----------------------------------------------------------------------

static void le_gltf_destroy( le_gltf_o *self ) {
	if ( self ) {
		if ( self->data )
			cgltf_free( self->data );
		delete self;
	}
}

// ----------------------------------------------------------------------

static le_gltf_o *le_gltf_create( char const *path ) {
	auto self    = new le_gltf_o();
	self->result = cgltf_parse_file( &self->options, path, &self->data );

	if ( self->result == cgltf_result_success ) {

		// This will load buffers from file, or data URIs,
		// and will allocate memory inside the cgltf module.
		//
		// Memory will be freed when calling `cgltf_free(self->data)`
		cgltf_result buffer_load_result = cgltf_load_buffers( &self->options, self->data, path );

		if ( buffer_load_result != cgltf_result_success ) {
			le_gltf_destroy( self );
			return nullptr;
		}

	} else {
		le_gltf_destroy( self );
		return nullptr;
	}
	struct le_accessor_info {
		le_num_type          component_type;
		le_compound_num_type type;
		uint32_t             offset;
		uint32_t             count;
		uint32_t             stride;
		uint32_t             buffer_view_idx;
		float                min[ 16 ];
		float                max[ 16 ];
		bool                 normalized;
		bool                 has_min;
		bool                 has_max;
		bool                 is_sparse;
	};
	return self;
}

static le_buffer_view_type get_le_buffer_view_type_from_cgltf( cgltf_buffer_view_type const &tp ) {

	// clang-format off
	switch ( tp ) {
		case cgltf_buffer_view_type_invalid: return le_buffer_view_type::eUndefined;
		case cgltf_buffer_view_type_indices: return le_buffer_view_type::eIndex;
		case cgltf_buffer_view_type_vertices: return le_buffer_view_type::eIndex;
	}
	// clang-format on

	assert( false );
	return le_buffer_view_type::eUndefined; //unreachable
}

// ----------------------------------------------------------------------

static le_compound_num_type get_compound_num_type_from_cgltf( cgltf_type const &tp ) {

	// clang-format off
	switch ( tp ) {
		case cgltf_type_invalid : return le_compound_num_type::eUndefined ;
		case cgltf_type_scalar  : return le_compound_num_type::eScalar;
		case cgltf_type_vec2    : return le_compound_num_type::eVec2;
		case cgltf_type_vec3    : return le_compound_num_type::eVec3;
		case cgltf_type_vec4    : return le_compound_num_type::eVec4;
		case cgltf_type_mat2    : return le_compound_num_type::eMat2;
		case cgltf_type_mat3    : return le_compound_num_type::eMat3;
		case cgltf_type_mat4    : return le_compound_num_type::eMat4;
	}
	// clang-format on

	assert( false );
	return le_compound_num_type::eUndefined; //unreachable
}

static le_num_type get_num_type_from_cgltf( cgltf_component_type const &tp ) {

	// clang-format off
	switch ( tp ) {
		case cgltf_component_type_invalid : return le_num_type::eUndefined;
		case cgltf_component_type_r_8     : return le_num_type::eI8;        /* BYTE */
		case cgltf_component_type_r_8u    : return le_num_type::eU8;        /* UNSIGNED_BYTE */
		case cgltf_component_type_r_16    : return le_num_type::eI16;       /* SHORT */
		case cgltf_component_type_r_16u   : return le_num_type::eU16;       /* UNSIGNED_SHORT */
		case cgltf_component_type_r_32u   : return le_num_type::eU32;       /* UNSIGNED_INT */
		case cgltf_component_type_r_32f   : return le_num_type::eF32;       /* FLOAT */
	}
	// clang-format on

	assert( false );
	return le_num_type::eUndefined; //unreachable
}

static le_primitive_attribute_info::Type get_primitive_attribute_type_from_cgltf( cgltf_attribute_type const &tp ) {

	// clang-format off
	switch ( tp ) {
		    case cgltf_attribute_type_invalid  : return le_primitive_attribute_info::Type::eUndefined; 
		    case cgltf_attribute_type_position : return le_primitive_attribute_info::Type::ePosition; 
		    case cgltf_attribute_type_normal   : return le_primitive_attribute_info::Type::eNormal; 
		    case cgltf_attribute_type_tangent  : return le_primitive_attribute_info::Type::eTangent; 
		    case cgltf_attribute_type_texcoord : return le_primitive_attribute_info::Type::eTexcoord; 
		    case cgltf_attribute_type_color    : return le_primitive_attribute_info::Type::eColor; 
		    case cgltf_attribute_type_joints   : return le_primitive_attribute_info::Type::eJoints; 
		    case cgltf_attribute_type_weights  : return le_primitive_attribute_info::Type::eWeights; 
	}
	// clang-format on

	assert( false );
	return le_primitive_attribute_info::Type::eUndefined; // unreachable
}

// ----------------------------------------------------------------------

static bool le_gltf_import( le_gltf_o *self, le_stage_o *stage ) {

	if ( nullptr == self ) {
		// TODO: warn that object was not valid
		return false;
	}

	std::unordered_map<cgltf_buffer const *, uint32_t>      buffer_map; // maps buffer by pointer to buffer index in stage
	std::unordered_map<cgltf_buffer_view const *, uint32_t> buffer_view_map;
	std::unordered_map<cgltf_accessor const *, uint32_t>    accessor_map;
	std::unordered_map<cgltf_mesh const *, uint32_t>        mesh_map;
	std::unordered_map<cgltf_node const *, uint32_t>        nodes_map;
	std::unordered_map<cgltf_scene const *, uint32_t>       scenes_map;

	using namespace le_stage;

	{
		// Upload buffers

		cgltf_buffer const *buffers_begin = self->data->buffers;
		auto                buffers_end   = self->data->buffers + self->data->buffers_count;

		char debug_name[ 32 ];

		int i = 0;
		for ( auto b = buffers_begin; b != buffers_end; b++, ++i ) {
			snprintf( debug_name, 32, "glTF_buffer_%d", i );
			uint32_t stage_idx = le_stage_i.create_buffer( stage, b->data, uint32_t( b->size ), debug_name );
			buffer_map.insert( {b, stage_idx} );
		}
	}
	{
		// Upload buffer_views

		cgltf_buffer_view const *buffer_views_begin = self->data->buffer_views;
		auto                     buffer_views_end   = buffer_views_begin + self->data->buffer_views_count;

		for ( auto bv = buffer_views_begin; bv != buffer_views_end; bv++ ) {
			le_buffer_view_info info{};
			info.buffer_idx  = buffer_map.at( bv->buffer );
			info.byte_offset = uint32_t( bv->offset );
			info.byte_length = uint32_t( bv->size );
			info.byte_stride = uint32_t( bv->stride );
			info.type        = get_le_buffer_view_type_from_cgltf( bv->type );

			uint32_t stage_idx = le_stage_i.create_buffer_view( stage, &info );
			buffer_view_map.insert( {bv, stage_idx} );
		}
	}
	{
		// Upload accessors

		cgltf_accessor const *accessors_begin = self->data->accessors;
		auto                  accessors_end   = accessors_begin + self->data->accessors_count;

		for ( auto a = accessors_begin; a != accessors_end; a++ ) {
			le_accessor_info info{};

			info.component_type  = get_num_type_from_cgltf( a->component_type );
			info.type            = get_compound_num_type_from_cgltf( a->type );
			info.byte_offset     = uint32_t( a->offset );
			info.count           = uint32_t( a->count );
			info.buffer_view_idx = buffer_view_map.at( a->buffer_view );
			if ( a->has_min ) {
				memcpy( info.min, a->min, sizeof( float ) * 16 );
			}
			if ( a->has_max ) {
				memcpy( info.max, a->max, sizeof( float ) * 16 );
			}
			info.is_normalized = a->normalized;
			info.has_min       = a->has_min;
			info.has_max       = a->has_max;
			info.is_sparse     = a->is_sparse;

			uint32_t stage_idx = le_stage_i.create_accessor( stage, &info );
			accessor_map.insert( {a, stage_idx} );
		}
	}

	{
		// Upload meshes

		cgltf_mesh const *mesh_begin = self->data->meshes;
		auto              mesh_end   = mesh_begin + self->data->meshes_count;

		for ( auto msh = mesh_begin; msh != mesh_end; msh++ ) {

			// build info data structure for this mesh
			le_mesh_info mesh_info;

			struct per_primitive_data_t {
				std::vector<le_primitive_attribute_info> attribute_infos;
			};

			std::vector<le_primitive_info>      primitive_infos;
			std::vector<per_primitive_data_t *> per_primitive_data;

			cgltf_primitive const *primitives_begin = msh->primitives;
			auto                   primitives_end   = primitives_begin + msh->primitives_count;

			for ( auto prim = primitives_begin; prim != primitives_end; prim++ ) {
				le_primitive_info     prim_info{};
				per_primitive_data_t *prim_data = new per_primitive_data_t{};
				per_primitive_data.push_back( prim_data ); // pushing info vec so it may be cleaned up

				// TODO: fill prim_data
				if ( prim->indices ) {
					prim_info.has_indices          = true;
					prim_info.indices_accessor_idx = accessor_map.at( prim->indices );
				}

				cgltf_attribute const *attributes_begin = prim->attributes;
				auto                   attributes_end   = attributes_begin + prim->attributes_count;

				for ( auto attr = attributes_begin; attr != attributes_end; attr++ ) {
					le_primitive_attribute_info attr_info;

					attr_info.accessor_idx = accessor_map.at( attr->data );
					attr_info.index        = attr->index;
					attr_info.type         = get_primitive_attribute_type_from_cgltf( attr->type );
					// TODO: attr->name;

					prim_data->attribute_infos.push_back( attr_info );
				}
				// prim_data->attribute_infos.push_back(data);

				prim_info.attributes      = prim_data->attribute_infos.data();
				prim_info.attribute_count = prim_data->attribute_infos.size();

				primitive_infos.emplace_back( prim_info );
			}

			mesh_info.primitives      = primitive_infos.data();
			mesh_info.primitive_count = primitive_infos.size();

			uint32_t stage_idx = le_stage_i.create_mesh( stage, &mesh_info );
			mesh_map.insert( {msh, stage_idx} );

			// Manual cleanup because raw pointer.

			for ( auto &d : per_primitive_data ) {
				delete ( d );
			}
		}
	}

	{

		// -- Upload nodes

		// We must build a linear structure which holds the full tree of node infos,
		// so that we can pass it via api calls to stage, where the tree will
		// get re-created.

		std::vector<le_node_info> node_infos;
		node_infos.reserve( self->data->nodes_count );

		cgltf_node const *nodes_begin = self->data->nodes;
		auto              nodes_end   = nodes_begin + self->data->nodes_count;

		{
			uint32_t i = 0;
			for ( auto n = nodes_begin; n != nodes_end; n++, i++ ) {
				nodes_map.insert( {n, i} );
			}
		}

		for ( auto n = nodes_begin; n != nodes_end; n++ ) {
			le_node_info info{};

			info.local_scale       = new glm_vec3_t;
			info.local_rotation    = new glm_quat_t;
			info.local_translation = new glm_vec3_t;
			info.local_transform   = new glm_mat4_t;

			if ( n->mesh ) {
				info.mesh     = mesh_map.at( n->mesh );
				info.has_mesh = true;
			} else {
				info.has_mesh = false;
			}

			// -- Apply transformation calculations:
			//    Our goal is to have SRT, as well as local transform matrix for each node.

			glm::mat4 m( 1 );

			if ( false == n->has_matrix ) {

				info.local_scale->data       = n->has_scale ? reinterpret_cast<glm::vec3 const &>( n->scale ) : glm::vec3( 1 );
				info.local_rotation->data    = n->has_rotation ? reinterpret_cast<glm::quat const &>( n->rotation ) : glm::quat( 0, 0, 0, 1 );
				info.local_translation->data = n->has_translation ? reinterpret_cast<glm::vec3 const &>( n->translation ) : glm::vec3( 0 );

				m = glm::scale( m, info.local_scale->data );           // Scale
				m = glm::mat4_cast( info.local_rotation->data ) * m;   // Rotate
				m = glm::translate( m, info.local_translation->data ); // Translate
			} else {

				m = reinterpret_cast<glm::mat4 const &>( n->matrix );

				glm::vec3 skew;
				glm::vec4 perspective;
				glm::vec3 scale;
				glm::vec3 translation;
				glm::quat orientation;

				if ( glm::decompose( m, scale, orientation, translation, skew, perspective ) ) {
					info.local_scale->data       = n->has_scale ? reinterpret_cast<glm::vec3 const &>( n->scale ) : scale;
					info.local_rotation->data    = n->has_rotation ? reinterpret_cast<glm::quat const &>( n->rotation ) : orientation;
					info.local_translation->data = n->has_translation ? reinterpret_cast<glm::vec3 const &>( n->translation ) : translation;
				} else {
					assert( false && "could not decompose matrix" );
				}
			}

			info.local_transform->data = m;

			info.child_indices_count = uint32_t( n->children_count );

			if ( n->children_count ) {

				// -- prepare an array for children indices for each node, so that these may be looked up
				// when re-creating the tree.
				uint32_t *child_indices = static_cast<uint32_t *>( malloc( sizeof( uint32_t ) * n->children_count ) );

				// We store the pointer to child_indices now,
				// before it ceases to be the pointer to element [0] of the children array,
				// as it will get incremented after we assign each child index element.
				info.child_indices = child_indices;

				cgltf_node const *const *children_begin = n->children;
				auto                     children_end   = children_begin + n->children_count;
				for ( auto c = children_begin; c != children_end; c++ ) {
					( *child_indices ) = nodes_map.at( *c ); // fetch index for child node.
					child_indices++;
				}
			}

			node_infos.emplace_back( info );
		}

		le_stage_i.create_nodes( stage, node_infos.data(), node_infos.size() );

		for ( auto &n : node_infos ) {
			// n.b. we must manually free node child indices becasue these were allocated via malloc.

#define DELETE_IF_SET( x ) \
	if ( x ) {             \
		delete x;          \
	}

			DELETE_IF_SET( n.local_scale )
			DELETE_IF_SET( n.local_rotation )
			DELETE_IF_SET( n.local_translation )
			DELETE_IF_SET( n.local_transform )

#undef DELETE_IF_SET

			if ( n.child_indices ) {
				free( n.child_indices );
			}
		}
	}

	{
		// -- Upload scene

		cgltf_scene const *scenes_begin = self->data->scenes;
		auto               scenes_end   = scenes_begin + self->data->scenes_count;

		for ( auto s = scenes_begin; s != scenes_end; s++ ) {
			std::vector<uint32_t> nodes_info;
			nodes_info.reserve( s->nodes_count );

			cgltf_node const *const *nodes_begin = s->nodes;
			auto                     nodes_end   = nodes_begin + s->nodes_count;

			for ( auto n = nodes_begin; n != nodes_end; n++ ) {
				nodes_info.push_back( nodes_map[ *n ] );
			}

			uint32_t scene_idx = le_stage_i.create_scene( stage, nodes_info.data(), uint32_t( nodes_info.size() ) );
			scenes_map.insert( {s, scene_idx} );
		}
	}

	return true;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_gltf_api( void *api ) {
	auto &le_gltf_i = static_cast<le_gltf_api *>( api )->le_gltf_i;

	le_gltf_i.create  = le_gltf_create;
	le_gltf_i.destroy = le_gltf_destroy;
	le_gltf_i.import  = le_gltf_import;
}
