#ifndef LE_STAGE_TYPES_GUARD
#define LE_STAGE_TYPES_GUARD

#include "private/le_renderer_types.h"

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
		eJointWeights,
	};

	uint32_t accessor_idx;
	uint32_t index;
	Type     type;
	char*    name;
};

struct le_texture_info {
	char*    name;
	uint32_t image_idx;
	uint32_t sampler_idx;
};

struct le_texture_transform_info {
	float    offset[ 2 ];
	float    rotation;
	float    scale[ 2 ];
	uint32_t uv_set;
};

struct le_texture_view_info {
	uint32_t texture_idx;
	uint32_t uv_set; // which uv set to use

	float scale;

	le_texture_transform_info* transform; // optional
};

struct le_pbr_metallic_roughness_info {
	le_texture_view_info* base_color_texture_view;
	le_texture_view_info* metallic_roughness_texture_view;

	float base_color_factor[ 4 ];
	float metallic_factor;
	float roughness_factor;
};

struct le_pbr_specular_glossiness_info {
	// TODO
};

struct le_material_info {
	char const* name;

	le_pbr_metallic_roughness_info*  pbr_metallic_roughness_info;
	le_pbr_specular_glossiness_info* pbr_specular_glossiness_info;
	le_texture_view_info*            normal_texture_view_info;
	le_texture_view_info*            occlusion_texture_view_info;
	le_texture_view_info*            emissive_texture_view_info;

	float emissive_factor[ 3 ];
};

struct le_morph_target_info_t {
	le_primitive_attribute_info* attributes;
	uint32_t                     attributes_count;
};

struct le_primitive_info {
	uint32_t                     indices_accessor_idx;
	bool                         has_indices;
	le_primitive_attribute_info* attributes;
	uint32_t                     attributes_count;
	le_morph_target_info_t*      morph_targets;
	uint32_t                     morph_targets_count;
	uint32_t                     material_idx;
	bool                         has_material;
};

struct le_mesh_info {
	le_primitive_info* primitives;
	uint32_t           primitive_count;
};

struct le_light_info {

	enum le_light_type : uint32_t {
		LE_LIGHT_TYPE_INVALID     = 0,
		LE_LIGHT_TYPE_DIRECTIONAL = 1,
		LE_LIGHT_TYPE_POINT       = 2,
		LE_LIGHT_TYPE_SPOT        = 3,
	};

	char const*   name;
	float         color[ 3 ];
	float         intensity;
	le_light_type type;
	float         range;
	float         spot_inner_cone_angle;
	float         spot_outer_cone_angle;
};

struct le_camera_settings_info {
	struct perspective_t {
		float fov_y_rad;    // vertical field of view in radians
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

enum class LeAnimationTargetType : uint32_t {
	eUndefined = 0,
	eTranslation,
	eRotation,
	eScale,
	eWeights,
};

struct le_animation_channel_info {
	uint32_t              animation_sampler_idx;
	uint32_t              node_idx;
	LeAnimationTargetType animation_target_type;
};

struct le_animation_sampler_info {
	enum class InterpolationType : uint32_t {
		eLinear,
		eStep,
		eCubicSpline,
	};
	uint32_t          input_accesstor_idx;
	uint32_t          output_accessor_idx;
	InterpolationType interpolation_type;
};

struct le_animation_info {
	const char*                name;
	le_animation_sampler_info* samplers;
	uint32_t                   samplers_count;
	le_animation_channel_info* channels;
	uint32_t                   channels_count;
};

struct le_skin_info {
	uint32_t* node_indices;       // joints
	uint32_t  node_indices_count; // joints count
	uint32_t  skeleton_node_index;
	bool      has_skeleton_node_index;
	uint32_t  inverse_bind_matrices_accessor_idx;
	bool      has_inverse_bind_matrices_accessor_idx;
};

struct le_node_info {
	uint32_t* child_indices;
	uint32_t  child_indices_count;

	uint32_t mesh;   // index into stage's mesh   array, only valid if has_mesh   == true
	uint32_t camera; // index into stage's camera array, only valid if has_camera == true
	uint32_t light;  // index into stage's lights array, only valid if has_light  == true
	uint32_t skin;   // index into stage's skin   array, only valid if has_skin   == true
	                 //
	bool has_mesh;   // whether this node has a mesh
	bool has_camera; // whether this node has a camera
	bool has_light;  // whether this node has a light
	bool has_skin;   // whether this node has a skin

	char* name;

	struct glm_mat4_t* local_transform;
	struct glm_vec3_t* local_translation;
	struct glm_quat_t* local_rotation;
	struct glm_vec3_t* local_scale;
};

#endif