#include "le_gltf.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/cgltf/cgltf.h"
#include "le_stage/le_stage.h"
#include "le_stage/le_stage_types.h"

#include <vector>
#include <string>
#include <unordered_map>
#include "string.h" // for memcpy
#include <filesystem>

#define GLM_FORCE_RIGHT_HANDED // glTF uses right handed coordinate system, and we're following its lead.
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
	cgltf_options         options = {};
	cgltf_data *          data    = nullptr;
	cgltf_result          result  = {};
	std::filesystem::path gltf_file_path; // owning
};

// ----------------------------------------------------------------------

static void le_gltf_destroy( le_gltf_o *self ) {
	if ( self ) {
		if ( self->data ) {
			cgltf_free( self->data );
		}
		delete self;
	}
}

// ----------------------------------------------------------------------

static le_gltf_o *le_gltf_create( char const *path ) {

	assert( path && "valid path must be set" );

	auto self    = new le_gltf_o{};
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

	self->gltf_file_path = std::filesystem::path{path};

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
	std::unordered_map<cgltf_image const *, uint32_t>       images_map;
	std::unordered_map<cgltf_buffer_view const *, uint32_t> buffer_view_map;
	std::unordered_map<cgltf_accessor const *, uint32_t>    accessor_map;
	std::unordered_map<cgltf_material const *, uint32_t>    materials_map;
	std::unordered_map<cgltf_mesh const *, uint32_t>        mesh_map;
	std::unordered_map<cgltf_camera const *, uint32_t>      camera_map;
	std::unordered_map<cgltf_node const *, uint32_t>        nodes_map;
	std::unordered_map<cgltf_scene const *, uint32_t>       scenes_map;

	using namespace le_stage;

	{
		// Upload image data.

		// Note that we don't decode image data - we read in the image data but don't decode it yet - this is for the
		// stage to do. We don't do this here because we don't want to allocate memory to store the decoded image twice.

		// if we decoded the image inside this module, we would have to copy the decoded memory across the api boundary.
		// we must copy because we cannot otherwise guarantee that the image data will still be available when stage
		// uploads it to the gpu, as the upload step happens in another method than the import step.

		cgltf_image const *images_begin = self->data->images;
		auto               images_end   = images_begin + self->data->images_count;

		for ( auto img = images_begin; img != images_end; img++ ) {

			uint32_t stage_idx = 0;

			if ( img->uri ) {

				// We need to know the file basename, because the file path is most likely relative.

				std::filesystem::path img_path{img->uri};

				if ( img_path.is_relative() ) {
					img_path = self->gltf_file_path.parent_path() / img_path;
				}

				stage_idx = le_stage_i.create_image_from_file_path( stage, img_path.c_str(), img->name ? img->name : img->uri );

			} else if ( img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data ) {

				unsigned char const *data = static_cast<unsigned char const *>( img->buffer_view->buffer->data );
				data += img->buffer_view->offset;
				size_t data_sz = img->buffer_view->size;
				stage_idx      = le_stage_i.create_image_from_memory( stage, data, uint32_t( data_sz ), img->name ? img->name : img->uri );

			} else {
				assert( false && "image must either have inline data or provide an uri" );
			}

			images_map.insert( {img, stage_idx} );
		}
	}

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

			if ( a->is_sparse ) {
				auto &sparse                   = info.sparse_accessor;
				sparse.count                   = uint32_t( a->sparse.count );
				sparse.values_byte_offset      = uint32_t( a->sparse.values_byte_offset );
				sparse.indices_byte_offset     = uint32_t( a->sparse.indices_byte_offset );
				sparse.indices_component_type  = get_num_type_from_cgltf( a->sparse.indices_component_type );
				sparse.values_buffer_view_idx  = buffer_view_map.at( a->sparse.values_buffer_view );
				sparse.indices_buffer_view_idx = buffer_view_map.at( a->sparse.indices_buffer_view );
			}

			uint32_t stage_idx = le_stage_i.create_accessor( stage, &info );
			accessor_map.insert( {a, stage_idx} );
		}
	}

	{
		// Upload material info

		cgltf_material const *materials_begin = self->data->materials;
		auto                  materials_end   = materials_begin + self->data->materials_count;

		std::vector<le_pbr_metallic_roughness_info *> metallic_roughness_infos;
		std::vector<le_pbr_metallic_roughness_info *> specular_glossiness_infos;

		for ( auto m = materials_begin; m != materials_end; m++ ) {
			le_material_info info{};

			if ( m->has_pbr_metallic_roughness ) {
				auto        pbr_info = new le_pbr_metallic_roughness_info{};
				auto const &pbr_src  = m->pbr_metallic_roughness;
				// TODO: attach texture views

				memcpy( pbr_info->base_color_factor, pbr_src.base_color_factor, sizeof( float ) * 4 );
				pbr_info->metallic_factor  = pbr_src.metallic_factor;
				pbr_info->roughness_factor = pbr_src.roughness_factor;

				info.pbr_metallic_roughness_info = pbr_info;
				metallic_roughness_infos.push_back( pbr_info ); // so that we can cleanup after api call.
			}

			if ( m->has_pbr_specular_glossiness ) {
				// TODO
				assert( false && "import for specular glossiness materials not yet implemented." );
			}

			// TODO: import normal texture
			// TODO: import occlusion texture
			// TODO: import emissive texture

			memcpy( info.emissive_factor, m->emissive_factor, sizeof( float ) * 3 );

			// Create stage resources via api call

			uint32_t material_idx = le_stage_i.create_material( stage, &info );
			materials_map.insert( {m, material_idx} );
		}

		// Cleanup

		for ( auto &i : metallic_roughness_infos ) {
			delete i;
		}

		for ( auto &i : specular_glossiness_infos ) {
			delete i;
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

				if ( prim->material ) {
					prim_info.has_material = true;
					prim_info.material_idx = materials_map.at( prim->material );
				}

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
		// -- Upload cameras

		std::vector<le_camera_settings_info> camera_infos;

		camera_infos.reserve( self->data->cameras_count );

		cgltf_camera const *cameras_begin = self->data->cameras;
		auto                cameras_end   = cameras_begin + self->data->cameras_count;

		for ( auto c = cameras_begin; c != cameras_end; c++ ) {

			le_camera_settings_info info{};

			switch ( c->type ) {
			case ( cgltf_camera_type_perspective ): {
				info.type        = le_camera_settings_info::Type::ePerspective;
				auto &cam        = info.data.as_perspective;
				cam.fov_y_rad    = c->data.perspective.yfov;
				cam.aspect_ratio = c->data.perspective.aspect_ratio;
				cam.z_far        = c->data.perspective.zfar;
				cam.z_near       = c->data.perspective.znear;
				break;
			}
			case ( cgltf_camera_type_orthographic ): {
				info.type  = le_camera_settings_info::Type::eOrthographic;
				auto &cam  = info.data.as_orthographic;
				cam.x_mag  = c->data.orthographic.xmag;
				cam.y_mag  = c->data.orthographic.ymag;
				cam.z_far  = c->data.orthographic.zfar;
				cam.z_near = c->data.orthographic.znear;
				break;
			}
			default:
				assert( false && "Camera must be either perspective or orthographic" );
				break;
			}

			camera_infos.emplace_back( info );
		}

		uint32_t camera_idx = le_stage_i.create_camera_settings( stage, camera_infos.data(), camera_infos.size() );

		// -- Store {camera -> camera stage index } in map for each camera:
		//
		// Since camera_idx received the first stage index for our added cameras,
		// and we know that cameras were added in sequence to stage, we can update
		// our camera_map accordingly.

		for ( auto c = cameras_begin; c != cameras_end; c++ ) {
			camera_map.insert( {c, camera_idx++} );
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

			if ( n->camera ) {
				info.camera     = camera_map.at( n->camera );
				info.has_camera = true;
			} else {
				info.has_camera = false;
			}

			// -- Apply transformation calculations:
			//    Our goal is to have SRT, as well as local transform matrix for each node.

			glm::mat4 m = glm::identity<glm::mat4>();

			if ( false == n->has_matrix ) {

				info.local_scale->data       = n->has_scale ? reinterpret_cast<glm::vec3 const &>( n->scale ) : glm::vec3( 1 );
				info.local_rotation->data    = n->has_rotation ? reinterpret_cast<glm::quat const &>( n->rotation ) : glm::identity<glm::quat>();
				info.local_translation->data = n->has_translation ? reinterpret_cast<glm::vec3 const &>( n->translation ) : glm::vec3( 0 );

				m = glm::translate( glm::mat4( 1.f ), info.local_translation->data ) * // translate
				    glm::mat4_cast( info.local_rotation->data ) *                      // rotate
				    glm::scale( glm::mat4( 1.f ), info.local_scale->data );            // Scale

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

			info.name = n->name;

			node_infos.emplace_back( info );
		}

		le_stage_i.create_nodes( stage, node_infos.data(), node_infos.size() );

		for ( auto &n : node_infos ) {

			// N.b.: we must manually free node child indices becasue these were allocated via malloc.
			//
			// It is safe to call `delete` even if nothing is pointed to by the pointers it is being
			// called upon.
			//
			// The standard says: "The value of the first argument supplied to a  deallocation function
			// may be a null pointer value; if so, and if the deallocation function is one supplied in
			// the standard library, the call has no effect."

			delete n.local_scale;
			delete n.local_rotation;
			delete n.local_translation;
			delete n.local_transform;

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
