#ifndef LE_STAGE_TYPES_GUARD
#define LE_STAGE_TYPES_GUARD

#include "le_renderer/private/le_renderer_types.h"

enum class le_buffer_view_type : uint8_t {
	eUndefined = 0,
	eIndex,
	eVertex,
};

struct le_accessor_sparse_info {
	uint32_t count;

	uint32_t    indices_buffer_view_idx;
	uint32_t    indices_byte_offset;
	le_num_type indices_component_type;

	uint32_t values_buffer_view_idx;
	uint32_t values_byte_offset;
};

struct le_accessor_info {
	le_num_type             component_type;
	le_compound_num_type    type;
	uint32_t                byte_offset;
	uint32_t                count;
	uint32_t                buffer_view_idx;
	float                   min[ 16 ];
	float                   max[ 16 ];
	bool                    is_normalized;
	bool                    has_min;
	bool                    has_max;
	bool                    is_sparse;
	le_accessor_sparse_info sparse_accessor;
};

struct le_buffer_view_info {
	uint32_t            buffer_idx;
	uint32_t            byte_offset;
	uint32_t            byte_length;
	uint32_t            byte_stride;
	le_buffer_view_type type;
};

struct le_primitive_attribute_info {
	enum class Type : uint32_t {
		eUndefined = 0,
		ePosition,
		eNormal,
		eTangent,
		eTexcoord,
		eColor,
		eJoints,
		eWeights,
	};

	uint32_t accessor_idx;
	uint32_t index;
	Type     type;
};

struct le_sampler_info {
	le::Filter             min_filter;
	le::Filter             mag_filter;
	le::SamplerMipmapMode  mipmap_mode;
	le::SamplerAddressMode address_mode_u;
	le::SamplerAddressMode address_mode_v;
};

struct le_texture_info {
	char *   name;
	uint32_t image_idx;
	uint32_t sampler_idx;
};

struct le_pbr_metallic_roughness_info {
	//	uint32_t base_color_texture_view;
	//	uint32_t metallic_roughness_texture_view;

	float base_color_factor[ 4 ];
	float metallic_factor;
	float roughness_factor;
};

struct le_pbr_specular_glossiness_info {
	// TODO
};

struct le_material_info {
	char const *name;
	bool        has_normal_texture_view;
	bool        has_occlusion_texture_view;
	bool        has_emissive_texture_view;

	le_pbr_metallic_roughness_info * pbr_metallic_roughness_info;
	le_pbr_specular_glossiness_info *pbr_specular_glossiness_info;
	uint32_t                         normal_texture_view_idx;
	uint32_t                         occlusion_texture_view_idx;
	uint32_t                         emissive_texture_view_idx;

	float emissive_factor[ 3 ];
};

struct le_primitive_info {
	uint32_t                     indices_accessor_idx;
	bool                         has_indices;
	le_primitive_attribute_info *attributes;
	uint32_t                     attribute_count;
	uint32_t                     material_idx;
	bool                         has_material;
};

struct le_mesh_info {
	le_primitive_info *primitives;
	uint32_t           primitive_count;
};

struct le_camera_settings_info {
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

	enum class Type : uint32_t {
		eUndefined = 0,
		ePerspective,
		eOrthographic,
	};

	Type type;

	union {
		perspective_t  as_perspective;
		orthographic_t as_orthographic;
	} data;
};

struct le_node_info {
	uint32_t *child_indices;
	uint32_t  child_indices_count;

	uint32_t mesh; // index into stage's mesh array
	bool     has_mesh;

	uint32_t camera; // index into stage's camera array
	bool     has_camera;

	char *name;

	struct glm_mat4_t *local_transform;
	struct glm_vec3_t *local_translation;
	struct glm_quat_t *local_rotation;
	struct glm_vec3_t *local_scale;
};

#endif