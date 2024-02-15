#include "le_mesh.h"
#include "le_core.h"
#include "le_log.h"

#include <vector>
#include <cstring> // for memcopy
#include <map>

static auto logger = le::Log( "le_mesh" );

struct attribute_descriptor_t {
    uint32_t num_bytes = {};
};

/*

  TODO: We want a way to convert our mesh from SOA to AOS, so that we may interleave
  attributes. this only makes sense if we know which attributes we will need when drawing.

*/

struct le_mesh_o {

	//	std::vector<le_mesh_api::default_vertex_type>  vertices; // 3d position in model space
	//	std::vector<le_mesh_api::default_normal_type>  normals;  // normalised normal, per-vertex
	//	std::vector<le_mesh_api::default_tangent_type> tangents; // normalised tangents, per-vertex
	//	std::vector<le_mesh_api::default_colour_type>  colours;  // rgba colour, per-vertex
	//	std::vector<le_mesh_api::default_uv_type>      uvs;      // uv coordintates    , per-vertex

	// yes, map, we want this to be sorted by attribute name when we iterate over it,
	// and the key is an int, and there are not many elements.
	size_t                                                          num_vertices = 0; // number of vertices - all attributes must have this count
	std::map<le_mesh_api::attribute_name_t, std::vector<uint8_t>>   attributes;
	std::map<le_mesh_api::attribute_name_t, attribute_descriptor_t> attribute_descriptors; // currently only holds size in bytes

	uint32_t             indices_num_bytes_per_index = 0;
	std::vector<uint8_t> indices_data; // indices, can be u16, or u32 - depends on greatest index
};

// ----------------------------------------------------------------------

static le_mesh_o* le_mesh_create() {
	auto self = new le_mesh_o();

	return self;
}

// ----------------------------------------------------------------------

static void le_mesh_destroy( le_mesh_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_mesh_clear( le_mesh_o* self ) {
	// self->vertices.clear();
	// self->normals.clear();
	// self->uvs.clear();
	// self->tangents.clear();
	// self->colours.clear();
	// self->indices_data.clear();

	self->attributes.clear();
	self->attribute_descriptors.clear();
	self->indices_data.clear();

	self->num_vertices                = 0;
	self->indices_num_bytes_per_index = 0;
}

/*
// ----------------------------------------------------------------------

static void le_mesh_get_vertices( le_mesh_o* self, size_t* count, le_mesh_api::default_vertex_type const** vertices ) {
    if ( count ) {
        *count = self->vertices.size();
    }
    if ( vertices ) {
        *vertices = self->vertices.data();
    }
}

// ----------------------------------------------------------------------

static void le_mesh_get_tangents( le_mesh_o* self, size_t* count, le_mesh_api::default_tangent_type const** tangents ) {
    if ( count ) {
        *count = self->tangents.size();
    }
    if ( tangents ) {
        *tangents = self->tangents.data();
    }
}

// ----------------------------------------------------------------------

static void le_mesh_get_indices( le_mesh_o* self, size_t* count, le_mesh_api::default_index_type const** indices ) {
    if ( count ) {
        *count = self->indices_data.size();
    }
    if ( indices ) {
        *indices = reinterpret_cast<le_mesh_api::default_index_type const*>( self->indices_data.data() );
    }
}

// ----------------------------------------------------------------------

static void le_mesh_get_normals( le_mesh_o* self, size_t* count, le_mesh_api::default_normal_type const** normals ) {
    if ( count ) {
        *count = self->normals.size();
    }
    if ( normals ) {
        *normals = self->normals.data();
    }
}

// ----------------------------------------------------------------------

static void le_mesh_get_colours( le_mesh_o* self, size_t* count, le_mesh_api::default_colour_type const** colours ) {
    if ( count ) {
        *count = self->colours.size();
    }
    if ( colours ) {
        *colours = self->colours.data();
    }
}

// ----------------------------------------------------------------------

static void le_mesh_get_uvs( le_mesh_o* self, size_t* count, le_mesh_api::default_uv_type const** uvs ) {
    if ( count ) {
        *count = self->normals.size();
    }
    if ( uvs ) {
        *uvs = self->uvs.data();
    }
}

// ----------------------------------------------------------------------

static void le_mesh_get_data( le_mesh_o* self, size_t* numVertices, size_t* numIndices, float const** vertices, float const** normals, float const** uvs, float const** colours, uint16_t const** indices ) {
    if ( numVertices ) {
        *numVertices = self->vertices.size();
    }
    if ( numIndices ) {
        *numIndices = self->indices_data.size() / sizeof( le_mesh_api::default_index_type );
    }

    if ( vertices ) {
        *vertices = self->vertices.empty() ? nullptr : reinterpret_cast<float const*>( self->vertices.data() );
    }

    if ( colours ) {
        *colours = self->colours.empty() ? nullptr : reinterpret_cast<float const*>( self->colours.data() );
    }

    if ( normals ) {
        *normals = self->normals.empty() ? nullptr : reinterpret_cast<float const*>( self->normals.data() );
    }

    if ( uvs ) {
        *uvs = self->uvs.empty() ? nullptr : reinterpret_cast<float const*>( self->uvs.data() );
    }

    if ( indices ) {
        *indices = reinterpret_cast<uint16_t*>( self->indices_data.data() );
    }
}
*/
// ----------------------------------------------------------------------

// you can use this to write data to any position you like - you
// can use this to combine mesh data by appending into a data buffer, where you left off
//
// write(vertices, num_bytes)
// write(vertices+num_bytes/sizeof(float), num_bytes);
// static void le_mesh_write_into_vertices( le_mesh_o* self, le_mesh_api::default_vertex_type* const vertices, size_t* num_bytes ) {
//
// 	if ( nullptr == num_bytes ) {
// 		return;
// 	}
//
// 	// ----------| invariant: num_bytes must be a valid pointer
//
// 	size_t max_src_bytes = self->vertices.size() * sizeof( le_mesh_api::default_vertex_type );
//
// 	// update num_bytes with the number of bytes that we intend to write.
// 	*num_bytes = ( *num_bytes < max_src_bytes ) ? *num_bytes : max_src_bytes;
//
// 	if ( vertices ) {
// 		memcpy( vertices, self->vertices.data(), *num_bytes );
// 	}
// }

// ----------------------------------------------------------------------
// write contents of
static void le_mesh_read_attribute_data_into(
    le_mesh_o* self,
    void* target, size_t target_capacity_num_bytes,
    le_mesh_api::attribute_name_t attribute_name,
    size_t first_vertex, size_t* num_vertices, uint32_t* num_bytes_per_vertex, uint32_t stride ) {

	if ( nullptr == num_vertices ) {
		logger.error( "You must set num_vertices ptr" );
		return;
	}

	if ( stride != 0 ) {
		logger.error( "writing stride other than 0 not implemented yet" );
		return;
	}

	// ---------| invariant: stride is 0

	if ( !self->attributes.contains( attribute_name ) ) {
		logger.error( "mesh does not have an attribute for this type: %d", attribute_name );
		return;
	}

	// ---------| invariant: mesh contains this attribute

	auto& bytes_vec = self->attributes[ attribute_name ];
	auto& attr_desc = self->attribute_descriptors[ attribute_name ];

	size_t vertices_available = self->num_vertices;

	if ( first_vertex >= vertices_available ) {
		return;
	}

	//---------- | invariant:  vertex_offset < vertices_available

	size_t num_vertices_to_copy = std::min( *num_vertices, vertices_available - first_vertex );

	// limit number of vertices to however as many as we can store back into the target
	num_vertices_to_copy = std::min( target_capacity_num_bytes / attr_desc.num_bytes, num_vertices_to_copy );

	size_t num_bytes_to_copy = num_vertices_to_copy * attr_desc.num_bytes;
	size_t offset_in_bytes   = first_vertex * attr_desc.num_bytes;

	*num_vertices         = num_vertices_to_copy; // write back number of vertices that were actually copied
	*num_bytes_per_vertex = attr_desc.num_bytes;  // write back number of bytes per vertex

	if ( target ) {
		memcpy( target, self->attributes[ attribute_name ].data() + offset_in_bytes, num_bytes_to_copy );
	}
}
// ----------------------------------------------------------------------

static void* le_mesh_allocate_attribute_data( le_mesh_o* self, le_mesh_api::attribute_name_t attribute_name, uint32_t num_bytes_per_vertex ) {

	// allocate memory from the mesh -
	// make sure that all vertex attributes have the same number of vertices.

	auto& attr            = self->attributes[ attribute_name ];
	auto& attr_descriptor = self->attribute_descriptors[ attribute_name ];

	if ( attr_descriptor.num_bytes == 0 ) {
		attr_descriptor.num_bytes = num_bytes_per_vertex;
	}

	if ( attr_descriptor.num_bytes != num_bytes_per_vertex ) {
		logger.error( "Attribute size does not match. "
		              "Requested: %d, was declared previously as: %d",
		              num_bytes_per_vertex, attr_descriptor.num_bytes );
		return nullptr;
	}

	attr_descriptor.num_bytes = num_bytes_per_vertex;
	attr.resize( num_bytes_per_vertex * self->num_vertices );

	// if we were successful, then return pointer to memory

	return attr.data();
};

// ----------------------------------------------------------------------

void* le_mesh_allocate_index_data( le_mesh_o* self, size_t num_indices, uint32_t* num_bytes_per_index ) {

	if ( nullptr == num_bytes_per_index ) {
		logger.error( "You must specify the number of bytes per index pointer." );
		return nullptr;
	}

	// If number of vertices is greater than what can be represented with an 16 bit index, we must use
	// 32 bit indices.

	{
		uint32_t required_num_bytes_per_index = ( self->num_vertices <= ( 1 << 16 ) ) ? 2 : 4;

		// Go for the lowest number of bytes per index that you can get away with,
		// but respect the client's request if they want a higher number of indices.
		*num_bytes_per_index              = std::min( std::max( required_num_bytes_per_index, *num_bytes_per_index ), uint32_t( 4 ) );
		self->indices_num_bytes_per_index = *num_bytes_per_index;
	}

	self->indices_data.resize( ( self->indices_num_bytes_per_index ) * num_indices );

	return self->indices_data.data();
}

// ----------------------------------------------------------------------

static void le_mesh_set_vertex_count( le_mesh_o* self, size_t num_vertices, bool* did_reallocate ) {

	self->num_vertices = num_vertices;

	for ( auto& a : self->attributes ) {

		auto& [ key, attribute_data ] = a;
		auto& attribute_descriptor    = self->attribute_descriptors.at( key );

		// Find size, make sure that it matches with num-vertices.
		//
		// If it doesn't re-allocate this attribute so that it fits
		// fill with zeroes, if necessary.

		if ( ( attribute_data.size() / attribute_descriptor.num_bytes ) < num_vertices ) {
			*did_reallocate = true;
			attribute_data.resize( num_vertices * attribute_descriptor.num_bytes, {} );
		}
	}

	*did_reallocate = true;
};

// ----------------------------------------------------------------------

static size_t le_mesh_get_vertex_count( le_mesh_o* self ) {
	return self->num_vertices;
}

// ----------------------------------------------------------------------

static size_t le_mesh_get_index_count( le_mesh_o* self, uint32_t* num_bytes_per_index ) {

	if ( num_bytes_per_index ) {
		*num_bytes_per_index = self->indices_num_bytes_per_index;
	}

	return self->indices_data.size() / self->indices_num_bytes_per_index;
};

// ----------------------------------------------------------------------
// read attribute info into a given array of data
static void le_mesh_read_attribute_info_into( le_mesh_o* self, le_mesh_api::attribute_info_t* target, size_t* num_attributes_in_target ) {

	size_t num_available_slots = *num_attributes_in_target;

	if ( num_available_slots < self->attributes.size() ) {
		// write back the number of attributes that this mesh contains.
		*num_attributes_in_target = self->attributes.size();
	}

	if ( target ) {

		for ( auto const& a_e : self->attribute_descriptors ) {
			if ( num_available_slots == 0 ) {
				break;
			}

			auto& [ key, a ] = a_e;

			*target++ = {
			    .name             = key,
			    .bytes_per_vertex = a.num_bytes,
			};

			num_available_slots--;
		}
	}
}

// ----------------------------------------------------------------------

// ISL_API_ATTR void le_module_register_le_mesh_load_from_ply( void* api ); // ffdecl.

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh, api ) {
	auto& le_mesh_i = static_cast<le_mesh_api*>( api )->le_mesh_i;

	// le_mesh_i.get_vertices            = le_mesh_get_vertices;
	// le_mesh_i.get_indices             = le_mesh_get_indices;
	// le_mesh_i.get_uvs                 = le_mesh_get_uvs;
	// le_mesh_i.get_tangents            = le_mesh_get_tangents;
	// le_mesh_i.get_normals             = le_mesh_get_normals;
	// le_mesh_i.get_colours             = le_mesh_get_colours;
	// le_mesh_i.get_data                = le_mesh_get_data;

	// le_mesh_i.write_into_vertices = le_mesh_write_into_vertices;

	// le_module_register_le_mesh_load_from_ply( api );

	le_mesh_i.allocate_attribute_data  = le_mesh_allocate_attribute_data;
	le_mesh_i.allocate_index_data      = le_mesh_allocate_index_data;
	le_mesh_i.read_attribute_data_into = le_mesh_read_attribute_data_into;

	le_mesh_i.set_vertex_count = le_mesh_set_vertex_count;
	le_mesh_i.get_vertex_count = le_mesh_get_vertex_count;

	le_mesh_i.get_index_count          = le_mesh_get_index_count;
	le_mesh_i.read_attribute_info_into = le_mesh_read_attribute_info_into;

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;
}
