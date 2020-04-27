#include "le_gltf.h"
#include "le_core/le_core.h"

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

	self->gltf_file_path = std::filesystem::path{ path };

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
		    case cgltf_attribute_type_weights  : return le_primitive_attribute_info::Type::eJointWeights; 
	}
	// clang-format on

	assert( false );
	return le_primitive_attribute_info::Type::eUndefined; // unreachable
}

// ----------------------------------------------------------------------
static le::SamplerMipmapMode cgltf_to_le_sampler_mipmap_mode( cgltf_int const &v ) {
	// clang-format off
	switch(v){
		case 9728: return le::SamplerMipmapMode::eLinear; // No mipmap mode specified, use default: Linear.
		case 9729: return le::SamplerMipmapMode::eLinear; // No mipmap mode specified, use default: Linear.
		case 9984: return le::SamplerMipmapMode::eNearest;
		case 9985: return le::SamplerMipmapMode::eNearest;
		case 9986: return le::SamplerMipmapMode::eLinear;
		case 9987: return le::SamplerMipmapMode::eLinear;
		default: return le::SamplerMipmapMode::eLinear;
	}
	// clang-format on
};

// ----------------------------------------------------------------------
static le::Filter cgltf_to_le_filter( cgltf_int const &v ) {
	// clang-format off
	switch(v){
		case 9728: return le::Filter::eNearest;
		case 9729: return le::Filter::eLinear; 
		case 9984: return le::Filter::eNearest;
		case 9985: return le::Filter::eLinear;
		case 9986: return le::Filter::eNearest;
		case 9987: return le::Filter::eLinear;
		default:  return le::Filter::eLinear; 
	}
	// clang-format on
};

static le::SamplerAddressMode cgltf_to_le_sampler_address_mode( cgltf_int const &v ) {
	// clang-format off
    switch(v){
		case 33071: return le::SamplerAddressMode::eClampToEdge;
		case 33648: return le::SamplerAddressMode::eMirroredRepeat;
		case 10497: return le::SamplerAddressMode::eRepeat;
		default: return le::SamplerAddressMode::eRepeat;         
    }
	// clang-format on
};

static le_animation_sampler_info::InterpolationType cgltf_to_le_interpolation_type( uint32_t const &t ) {

	// clang-format off
	switch ( t ) {
		case cgltf_interpolation_type_linear      : return le_animation_sampler_info::InterpolationType::eLinear;
		case cgltf_interpolation_type_step        : return le_animation_sampler_info::InterpolationType::eStep;
		case cgltf_interpolation_type_cubic_spline: return le_animation_sampler_info::InterpolationType::eCubicSpline;
	}
	// clang-format on

	return le_animation_sampler_info::InterpolationType::eLinear;
}

static LeAnimationTargetType cgltf_to_le_animation_target_type( uint32_t const &t ) {
	// clang-format off
	switch ( t ) {
		case cgltf_animation_path_type_invalid     : return LeAnimationTargetType::eUndefined;
		case cgltf_animation_path_type_translation : return LeAnimationTargetType::eTranslation;
		case cgltf_animation_path_type_rotation	   : return LeAnimationTargetType::eRotation;
		case cgltf_animation_path_type_scale       : return LeAnimationTargetType::eScale;
		case cgltf_animation_path_type_weights     : return LeAnimationTargetType::eWeights;
	}
	// clang-format on
	return LeAnimationTargetType::eUndefined; // unreachable
}

// ----------------------------------------------------------------------

static bool le_gltf_import( le_gltf_o *self, le_stage_o *stage ) {

	if ( nullptr == self ) {
		// TODO: warn that object was not valid
		return false;
	}

	std::unordered_map<cgltf_texture const *, uint32_t>           textures_map;
	std::unordered_map<cgltf_sampler const *, uint32_t>           samplers_map;
	std::unordered_map<cgltf_image const *, uint32_t>             images_map;
	std::unordered_map<cgltf_buffer const *, uint32_t>            buffer_map;
	std::unordered_map<cgltf_buffer_view const *, uint32_t>       buffer_view_map;
	std::unordered_map<cgltf_accessor const *, uint32_t>          accessor_map;
	std::unordered_map<cgltf_material const *, uint32_t>          materials_map;
	std::unordered_map<cgltf_mesh const *, uint32_t>              mesh_map;
	std::unordered_map<cgltf_camera const *, uint32_t>            camera_map;
	std::unordered_map<cgltf_light const *, uint32_t>             lights_map;
	std::unordered_map<cgltf_node const *, uint32_t>              nodes_map;
	std::unordered_map<cgltf_scene const *, uint32_t>             scenes_map;
	std::unordered_map<cgltf_animation_sampler const *, uint32_t> animation_samplers_map;
	std::unordered_map<cgltf_animation const *, uint32_t>         animations_map;
	std::unordered_map<cgltf_skin const *, uint32_t>              skins_map;

	uint32_t default_sampler_idx = 0; // id for default sampler, in case texture does not specify sampler.

	using namespace le_stage;

	{
		// Upload image data.

		// Note that we don't decode image data - we read in the image data but don't decode it yet - this is for the
		// stage to do. We don't do this here because we don't want to allocate memory to store the decoded image twice.

		// If we decoded the image inside this module, we would have to copy the decoded memory across the api boundary.
		// We must copy because we cannot otherwise guarantee that the image data will still be available when stage
		// uploads it to the gpu, as the upload step happens in another method than the import step.

		cgltf_image const *images_begin = self->data->images;
		auto               images_end   = images_begin + self->data->images_count;

		for ( auto img = images_begin; img != images_end; img++ ) {

			uint32_t stage_idx = 0;

			// TODO: must check if uri is not a data uri!

			if ( img->uri ) {

				// We need to know the file basename, because the file path is most likely relative.

				std::filesystem::path img_path{ img->uri };

				if ( img_path.is_relative() ) {
					img_path = self->gltf_file_path.parent_path() / img_path;
				}

				stage_idx = le_stage_i.create_image_from_file_path( stage, img_path.c_str(), img->name ? img->name : img->uri, 0 );

			} else if ( img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data ) {

				unsigned char const *data = static_cast<unsigned char const *>( img->buffer_view->buffer->data );
				data += img->buffer_view->offset;
				size_t data_sz = img->buffer_view->size;
				stage_idx      = le_stage_i.create_image_from_memory( stage, data, uint32_t( data_sz ), img->name ? img->name : img->uri, 0 );

			} else {
				assert( false && "image must either have inline data or provide an uri" );
			}

			images_map.insert( { img, stage_idx } );
		}
	}

	{

		{
			// Sampler used for textures which don't define a sampler.
			// Spec says: "When undefined, a sampler with repeat wrapping and auto filtering should be used."
			le_sampler_info_t default_sampler_info{};
			default_sampler_info.addressModeU = le::SamplerAddressMode::eRepeat;
			default_sampler_info.addressModeV = le::SamplerAddressMode::eRepeat;

			default_sampler_idx = le_stage_i.create_sampler( stage, &default_sampler_info );
		}

		// Upload sampler information

		cgltf_sampler *samplers_begin = self->data->samplers;
		auto           samplers_end   = samplers_begin + self->data->samplers_count;

		for ( auto s = samplers_begin; s != samplers_end; s++ ) {
			le_sampler_info_t info{};

			info.addressModeU = cgltf_to_le_sampler_address_mode( s->wrap_s );
			info.addressModeV = cgltf_to_le_sampler_address_mode( s->wrap_t );

			// We assume that min and mag filter have the same values set for
			// the Mipmap mode of their respective enums.
			info.mipmapMode = cgltf_to_le_sampler_mipmap_mode( s->min_filter );

			info.magFilter = cgltf_to_le_filter( s->mag_filter );
			info.minFilter = cgltf_to_le_filter( s->min_filter );

			// add sampler to stage
			uint32_t stage_idx = le_stage_i.create_sampler( stage, &info );
			samplers_map.insert( { s, stage_idx } );
		}
	}

	{
		// Upload texture information

		cgltf_texture *textures_begin = self->data->textures;
		auto           textures_end   = textures_begin + self->data->textures_count;

		for ( auto t = textures_begin; t != textures_end; t++ ) {
			le_texture_info info;
			if ( t->sampler ) {
				info.sampler_idx = samplers_map.at( t->sampler );
			} else {
				// Note: Sampler is optional, GLTF spec says:
				// "When undefined, a sampler with repeat wrapping and auto filtering should be used."
				info.sampler_idx = default_sampler_idx;
			}
			info.image_idx     = images_map.at( t->image );
			info.name          = t->image->name;
			uint32_t stage_idx = le_stage_i.create_texture( stage, &info );
			textures_map.insert( { t, stage_idx } );
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
			buffer_map.insert( { b, stage_idx } );
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
			buffer_view_map.insert( { bv, stage_idx } );
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
			accessor_map.insert( { a, stage_idx } );
		}
	}

	{
		// Upload material info

		cgltf_material const *materials_begin = self->data->materials;
		auto                  materials_end   = materials_begin + self->data->materials_count;

		std::vector<le_texture_transform_info *>      texture_transforms;
		std::vector<le_texture_view_info *>           texture_view_infos;
		std::vector<le_pbr_metallic_roughness_info *> metallic_roughness_infos;
		std::vector<le_pbr_metallic_roughness_info *> specular_glossiness_infos;

		auto create_texture_view_info = [ & ]( const cgltf_texture_view &tv ) -> le_texture_view_info * {
			auto tex_view_info = new le_texture_view_info{};
			texture_view_infos.push_back( tex_view_info );

			tex_view_info->texture_idx = textures_map.at( tv.texture );
			tex_view_info->uv_set      = uint32_t( tv.texcoord );
			tex_view_info->scale       = tv.scale;

			if ( tv.has_transform ) {
				auto tv_transform = new le_texture_transform_info{};
				texture_transforms.push_back( tv_transform );

				memcpy( tv_transform->scale, &tv.transform.scale, sizeof( float ) * 2 );
				tv_transform->rotation = tv.transform.rotation;
				memcpy( tv_transform->offset, &tv.transform.offset, sizeof( float ) * 2 );
				tv_transform->uv_set = uint32_t( tv.transform.texcoord );

				tex_view_info->transform = tv_transform;
			}

			return tex_view_info;
		};

		for ( auto m = materials_begin; m != materials_end; m++ ) {
			le_material_info info{};

			if ( m->has_pbr_metallic_roughness ) {
				auto mr_info = new le_pbr_metallic_roughness_info{};
				metallic_roughness_infos.push_back( mr_info ); // so that we can cleanup after api call.

				auto const &mr_src = m->pbr_metallic_roughness;

				if ( m->pbr_metallic_roughness.base_color_texture.texture ) {
					mr_info->base_color_texture_view =
					    create_texture_view_info( m->pbr_metallic_roughness.base_color_texture );
				}

				if ( m->pbr_metallic_roughness.metallic_roughness_texture.texture ) {
					mr_info->metallic_roughness_texture_view =
					    create_texture_view_info( m->pbr_metallic_roughness.metallic_roughness_texture );
				}

				memcpy( mr_info->base_color_factor, mr_src.base_color_factor, sizeof( mr_src.base_color_factor ) );

				mr_info->metallic_factor  = mr_src.metallic_factor;
				mr_info->roughness_factor = mr_src.roughness_factor;

				info.pbr_metallic_roughness_info = mr_info;
			}

			if ( m->has_pbr_specular_glossiness ) {
				// TODO
				assert( false && "import for specular glossiness materials not yet implemented." );
			}

			if ( m->normal_texture.texture ) {
				info.normal_texture_view_info = create_texture_view_info( m->normal_texture );
			}

			if ( m->emissive_texture.texture ) {
				info.emissive_texture_view_info = create_texture_view_info( m->emissive_texture );
			}
			memcpy( info.emissive_factor, m->emissive_factor, sizeof( m->emissive_factor ) );

			if ( m->occlusion_texture.texture ) {
				info.occlusion_texture_view_info = create_texture_view_info( m->occlusion_texture );
			}

			// Create stage resources via api call

			uint32_t material_idx = le_stage_i.create_material( stage, &info );
			materials_map.insert( { m, material_idx } );
		}

		// Cleanup temporary objects

		for ( auto &i : metallic_roughness_infos ) {
			delete i;
		}

		for ( auto &i : specular_glossiness_infos ) {
			delete i;
		}

		for ( auto &i : texture_view_infos ) {
			delete i;
		}

		for ( auto &i : texture_transforms ) {
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
				std::vector<le_primitive_attribute_info>              attribute_infos;
				std::vector<std::vector<le_primitive_attribute_info>> morph_targets_data; // array of attributes for each morph target
				std::vector<le_morph_target_info_t>                   morph_target_infos; // {pointer,count} for each morph target
			};

			std::vector<le_primitive_info>      primitive_infos;
			std::vector<per_primitive_data_t *> per_primitive_data;

			cgltf_primitive const *primitives_begin = msh->primitives;
			auto                   primitives_end   = primitives_begin + msh->primitives_count;

			for ( auto prim = primitives_begin; prim != primitives_end; prim++ ) {
				le_primitive_info prim_info{};

				// NOTE: We allocate prim_data explicitly on the heap, so that it
				// can't get moved, and its address remains valid until we explicitly
				// delete prim_data after the call to le_stage.
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
					attr_info.index        = uint32_t( attr->index );
					attr_info.type         = get_primitive_attribute_type_from_cgltf( attr->type );
					attr_info.name         = attr->name;

					prim_data->attribute_infos.push_back( attr_info );
				}

				prim_info.attributes       = prim_data->attribute_infos.data();
				prim_info.attributes_count = uint32_t( prim_data->attribute_infos.size() );

				// -- Parse morph target data and keep it in memory until per-primitive data
				// is explicitly deleted after the call to le_stage has been completed.

				cgltf_morph_target const *morph_targets_begin = prim->targets;
				auto const                morph_targets_end   = morph_targets_begin + prim->targets_count;

				for ( auto mt = morph_targets_begin; mt != morph_targets_end; mt++ ) {

					// For each attribute of morph targets
					// NOTE: There must be an attribute each for of every non-morph target attributes
					std::vector<le_primitive_attribute_info> morph_attributes;

					morph_attributes.reserve( mt->attributes_count );

					cgltf_attribute const *attributes_begin = mt->attributes;
					auto                   attributes_end   = attributes_begin + mt->attributes_count;

					for ( auto attr = attributes_begin; attr != attributes_end; attr++ ) {
						le_primitive_attribute_info attr_info;

						attr_info.accessor_idx = accessor_map.at( attr->data );
						attr_info.index        = uint32_t( attr->index );
						attr_info.type         = get_primitive_attribute_type_from_cgltf( attr->type );
						attr_info.name         = attr->name;

						morph_attributes.push_back( attr_info );
					}

					prim_data->morph_targets_data.emplace_back( morph_attributes );
				}

				// Create infos over morph targets, which means harvesting pointer addresses,
				// and array sizes.

				for ( auto &morph_target_data : prim_data->morph_targets_data ) {
					le_morph_target_info_t info{};
					info.attributes       = morph_target_data.data();
					info.attributes_count = uint32_t( morph_target_data.size() );
					prim_data->morph_target_infos.emplace_back( info );
				}

				prim_info.morph_targets       = prim_data->morph_target_infos.data();
				prim_info.morph_targets_count = uint32_t( prim_data->morph_target_infos.size() );

				primitive_infos.emplace_back( prim_info );
			}

			mesh_info.primitives      = primitive_infos.data();
			mesh_info.primitive_count = uint32_t( primitive_infos.size() );

			uint32_t stage_idx = le_stage_i.create_mesh( stage, &mesh_info );
			mesh_map.insert( { msh, stage_idx } );

			// Manual cleanup of per-primitive data because raw pointer.

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
			camera_map.insert( { c, camera_idx++ } );
		}
	}

	{
		// Upload Lights

		cgltf_light const *lights_begin = self->data->lights;
		auto               lights_end   = lights_begin + self->data->lights_count;
		for ( auto l = lights_begin; l != lights_end; l++ ) {
			le_light_info info{};
			info.name  = l->name;
			info.range = l->range;
			// clang-format off
			switch ( l->type ) {
			case cgltf_light_type_invalid:
				assert( false && "light type must be valid" );
				break;
			case cgltf_light_type_spot:
				info.type = le_light_info::LE_LIGHT_TYPE_SPOT;
				break;
			case cgltf_light_type_point:
				info.type = le_light_info::LE_LIGHT_TYPE_POINT;
				break;
			case cgltf_light_type_directional:
				info.type = le_light_info::LE_LIGHT_TYPE_DIRECTIONAL;
				break;
			}
			// clang-format on
			memcpy( info.color, l->color, sizeof( info.color ) );
			info.intensity             = l->intensity;
			info.spot_inner_cone_angle = l->spot_inner_cone_angle;
			info.spot_outer_cone_angle = l->spot_outer_cone_angle;

			uint32_t light_idx = le_stage_i.create_light( stage, &info );
			lights_map.insert( { l, light_idx } );
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
				nodes_map.insert( { n, i } );
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

			if ( n->light ) {
				info.light     = lights_map.at( n->light );
				info.has_light = true;
			} else {
				info.has_light = false;
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

	{ // -- upload skin info

		cgltf_skin const *      skins_begin = self->data->skins;
		cgltf_skin const *const skins_end   = skins_begin + self->data->skins_count;

		for ( auto skin = skins_begin; skin != skins_end; skin++ ) {

			le_skin_info info{};

			if ( skin->skeleton ) {
				info.has_skeleton_node_index = true;
				info.skeleton_node_index     = nodes_map.at( skin->skeleton );
			}

			if ( skin->inverse_bind_matrices ) {
				info.has_inverse_bind_matrices_accessor_idx = true;
				info.inverse_bind_matrices_accessor_idx     = accessor_map.at( skin->inverse_bind_matrices );
			}

			cgltf_node const *const *joints_begin = skin->joints;
			cgltf_node const *const *joints_end   = skin->joints + skin->joints_count;

			std::vector<uint32_t> joints_indices;
			joints_indices.reserve( skin->joints_count );

			for ( auto joint = joints_begin; joint != joints_end; joint++ ) {
				joints_indices.push_back( nodes_map.at( *joint ) );
			}

			info.node_indices       = joints_indices.data();
			info.node_indices_count = uint32_t( joints_indices.size() );

			uint32_t skin_idx = le_stage_i.create_skin( stage, &info );
			skins_map.insert( { skin, skin_idx } );
		}
	}

	{

		// Patch nodes which have skins - we must do this after uploading nodes, because
		// skins themselves refer to nodes, and we can only refer to nodes which we have
		// already created in stage.

		cgltf_node const *nodes_begin = self->data->nodes;
		auto              nodes_end   = nodes_begin + self->data->nodes_count;

		for ( auto n = nodes_begin; n != nodes_end; n++ ) {
			if ( n->skin ) {
				le_stage_i.node_set_skin( stage, nodes_map.at( n ), skins_map.at( n->skin ) );
			}
		}
	}

	if ( self->data->animations_count ) {
		// upload animations
		// for each animation:

		cgltf_animation const *const animations_begin = self->data->animations;
		auto                         animations_end   = animations_begin + self->data->animations_count;

		// For each animation:

		for ( auto a = animations_begin; a != animations_end; a++ ) {

			if ( 0 == a->channels_count ) {
				// animations without channels are noops, they can be ignored.
				continue;
			}

			// ----------| invariant: this animation has some channels.

			std::vector<le_animation_channel_info> animation_channel_infos;
			std::vector<le_animation_sampler_info> animation_sampler_infos;

			// fill in animation samplers for this animation

			cgltf_animation_sampler const *const animation_samplers_begin = a->samplers;
			auto                                 animation_samplers_end   = animation_samplers_begin + a->samplers_count;

			for ( auto s = animation_samplers_begin; s != animation_samplers_end; s++ ) {
				le_animation_sampler_info info{};

				info.input_accesstor_idx = accessor_map.at( s->input );
				info.output_accessor_idx = accessor_map.at( s->output );
				info.interpolation_type  = cgltf_to_le_interpolation_type( s->interpolation );

				animation_sampler_infos.emplace_back( info );
			}

			// Find animation index in array of animation_samplers.
			// Behaves like any find method, will return index of element past last element if not found
			auto find_animation_sampler_idx =
			    []( cgltf_animation_sampler const *const needle,
			        cgltf_animation_sampler const *const samplers_begin,
			        cgltf_animation_sampler const *const samplers_end ) -> uint32_t {
				uint32_t result = 0;
				for ( auto s = samplers_begin; s != samplers_end; s++, result++ ) {
					if ( needle == s ) {
						return result;
					}
				}
				return result;
			};

			// Fill in animation channels for this animation

			cgltf_animation_channel const *const animation_channels_begin = a->channels;
			auto                                 animation_channels_end   = animation_channels_begin + a->channels_count;

			animation_channel_infos.reserve( a->channels_count );

			for ( auto c = animation_channels_begin; c != animation_channels_end; c++ ) {
				le_animation_channel_info info{};

				info.node_idx              = nodes_map.at( c->target_node );
				info.animation_target_type = cgltf_to_le_animation_target_type( c->target_path );
				{
					uint32_t animation_sampler_idx = find_animation_sampler_idx( c->sampler, a->samplers, a->samplers + a->samplers_count );
					assert( animation_sampler_idx < a->samplers_count && "animation_sampler must exist in animation's animation_samplers array" );
					info.animation_sampler_idx = animation_sampler_idx;
				}
				animation_channel_infos.emplace_back( info );
			}

			le_animation_info info{};
			info.name           = a->name;
			info.samplers       = animation_sampler_infos.data();
			info.samplers_count = uint32_t( animation_sampler_infos.size() );
			info.channels       = animation_channel_infos.data();
			info.channels_count = uint32_t( animation_channel_infos.size() );

			// - upload animation
			uint32_t animation_idx = le_stage_i.create_animation( stage, &info );
			animations_map.insert( { a, animation_idx } );
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
			scenes_map.insert( { s, scene_idx } );
		}
	}

	return true;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_gltf, api ) {
	auto &le_gltf_i = static_cast<le_gltf_api *>( api )->le_gltf_i;

	le_gltf_i.create  = le_gltf_create;
	le_gltf_i.destroy = le_gltf_destroy;
	le_gltf_i.import  = le_gltf_import;
}
