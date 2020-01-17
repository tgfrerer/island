#ifndef LE_STAGE_TYPES_GUARD
#define LE_STAGE_TYPES_GUARD

#include "le_renderer/private/le_renderer_types.h"

enum class le_buffer_view_type : uint8_t {
	eUndefined = 0,
	eIndex,
	eVertex,
};

struct le_accessor_info {
	le_num_type          component_type;
	le_compound_num_type type;
	uint32_t             byte_offset;
	uint32_t             count;
	uint32_t             buffer_view_idx;
	float                min[ 16 ];
	float                max[ 16 ];
	bool                 is_normalized;
	bool                 has_min;
	bool                 has_max;
	bool                 is_sparse;
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

struct le_primitive_info {
	uint32_t                     index_accessor_idx;
	bool                         has_indices;
	le_primitive_attribute_info *attributes;
	uint32_t                     attribute_count;
};

struct le_mesh_info {
	le_primitive_info *primitives;
	uint32_t           primitive_count;
};

#endif