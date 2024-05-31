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
	// yes, `map`, and not `unordered_map`, we want this to be sorted by attribute name when we iterate over it,
	// and the key is an int, and there are not many elements.
	size_t                                                          num_vertices = 0; // number of vertices - all attributes must have this count
	std::map<le_mesh_api::attribute_name_t, std::vector<uint8_t>>   attributes;
	std::map<le_mesh_api::attribute_name_t, attribute_descriptor_t> attribute_descriptors; // currently only holds size in bytes

	uint32_t             indices_num_bytes_per_index = 0;
	std::vector<uint8_t> indices_data; // indices, can be u16, or u32 - depends on greatest index
};

// ----------------------------------------------------------------------

static le_mesh_o* le_mesh_create() {
	auto self = new le_mesh_o{};

	return self;
}

// ----------------------------------------------------------------------

static void le_mesh_destroy( le_mesh_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_mesh_clear( le_mesh_o* self ) {
	self->attributes.clear();
	self->attribute_descriptors.clear();
	self->indices_data.clear();

	self->num_vertices                = 0;
	self->indices_num_bytes_per_index = 0;
}

// ----------------------------------------------------------------------
// write contents of
static void le_mesh_read_attribute_data_into( le_mesh_o const* self, void* target, size_t target_capacity_num_bytes,
                                              le_mesh_api::attribute_name_t attribute_name,
                                              uint32_t* num_bytes_per_vertex, size_t* num_vertices, size_t first_vertex, uint32_t stride ) {

	// ---------| invariant: stride is 0

	if ( !self->attributes.contains( attribute_name ) ) {
		logger.error( "mesh does not have an attribute for this type: %d", attribute_name );
		return;
	}

	// ---------| invariant: mesh contains this attribute

	auto& bytes_vec = self->attributes.at( attribute_name );
	auto& attr_desc = self->attribute_descriptors.at( attribute_name );

	size_t num_vertices_available = self->num_vertices;
	// If num_vertices is not set, all available vertices should be used.
	size_t num_vertices_requested = ( num_vertices ) ? ( *num_vertices ) : num_vertices_available;

	if ( first_vertex >= num_vertices_available ) {
		return;
	}

	//---------- | invariant:  vertex_offset < vertices_available

	size_t num_vertices_to_copy = std::min( num_vertices_requested, num_vertices_available - first_vertex );

	// Now, limit number of vertices to however many as we can store back into the target
	num_vertices_to_copy = std::min( target_capacity_num_bytes / attr_desc.num_bytes, num_vertices_to_copy );

	if ( num_vertices ) {
		*num_vertices = num_vertices_to_copy; // write back number of vertices that were actually copied
	}

	if ( num_bytes_per_vertex ) {
		*num_bytes_per_vertex = attr_desc.num_bytes; // write back number of bytes per vertex
	}

	if ( stride == 0 ) {
		// If no stride has been specified, assume tightly packed
		stride = attr_desc.num_bytes;
	}

	if ( stride < attr_desc.num_bytes ) {
		logger.error( "stride may not be lower than attribute byte count: %d < %d", stride, attr_desc.num_bytes );
	}

	size_t requested_bytes_count = ( stride ) * ( num_vertices_to_copy - 1 ) + attr_desc.num_bytes;
	if ( requested_bytes_count > target_capacity_num_bytes ) {
		logger.error( "not enough capacity in target (%d) to store requested bumber of bytes (%d)", target_capacity_num_bytes, requested_bytes_count );
		return;
	}

	if ( target ) {

		if ( stride == attr_desc.num_bytes || stride == 0 ) {
			// source and target are contiguous - we can use a straightforward memcpy
			size_t num_bytes_to_copy = num_vertices_to_copy * attr_desc.num_bytes;
			size_t offset_in_bytes   = first_vertex * attr_desc.num_bytes;
			memcpy( target, bytes_vec.data() + offset_in_bytes, num_bytes_to_copy );
		} else {
			// target is not contiguous, but strided, we must manually copy
			size_t offset_in_bytes = first_vertex * attr_desc.num_bytes;

			uint8_t const* data_source = bytes_vec.data() + offset_in_bytes;
			uint8_t*       data_target = reinterpret_cast<uint8_t*>( target );

			for ( size_t i = 0; i != num_vertices_to_copy; i++ ) {
				memcpy(
				    data_target,
				    data_source,
				    attr_desc.num_bytes );
				data_target += stride;
				data_source += attr_desc.num_bytes;
			}
		}
	}
}

// ----------------------------------------------------------------------

static void le_mesh_read_index_data_into( le_mesh_o const* self, void* target, size_t target_capacity_num_bytes, uint32_t* num_bytes_per_index, size_t* num_indices, size_t first_index ) {

	auto& bytes_vec = self->indices_data;

	size_t num_indices_available = bytes_vec.size() / self->indices_num_bytes_per_index;
	size_t num_indices_requested = ( num_indices ) ? ( *num_indices ) : num_indices_available;

	if ( first_index >= num_indices_available ) {
		return;
	}

	size_t num_indices_to_copy = std::min( num_indices_requested, num_indices_available - first_index );

	// Now, limit number of vertices to however many as we can store back into the target
	num_indices_to_copy = std::min( target_capacity_num_bytes / self->indices_num_bytes_per_index, num_indices_to_copy );

	if ( num_indices ) {
		*num_indices = num_indices_to_copy;
	}
	if ( num_bytes_per_index ) {
		*num_bytes_per_index = self->indices_num_bytes_per_index;
	}
	if ( target ) {
		size_t offset_in_bytes   = first_index * self->indices_num_bytes_per_index;
		size_t num_bytes_to_copy = num_indices_to_copy * self->indices_num_bytes_per_index;
		memcpy( target, bytes_vec.data() + offset_in_bytes, num_bytes_to_copy );
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

static void* le_mesh_allocate_index_data( le_mesh_o* self, size_t num_indices, uint32_t* num_bytes_per_index ) {

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

	self->indices_data.resize( self->indices_num_bytes_per_index * num_indices );

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
static void le_mesh_read_attribute_infos_into( le_mesh_o* self, le_mesh_api::attribute_info_t* target, size_t* num_attributes_in_target ) {

	if ( nullptr == num_attributes_in_target ) {
		return;
	}

	// ----------| invariant: num_attributes_in_target was set

	size_t num_available_slots = *num_attributes_in_target;

	// write back the number of attributes that this mesh contains.
	*num_attributes_in_target = self->attributes.size();

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

ISL_API_ATTR void le_module_register_le_mesh_load_from_ply( void* api ); // ffdecl.

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh, api ) {
	auto& le_mesh_i = static_cast<le_mesh_api*>( api )->le_mesh_i;

	le_module_register_le_mesh_load_from_ply( api );

	le_mesh_i.allocate_attribute_data  = le_mesh_allocate_attribute_data;
	le_mesh_i.allocate_index_data      = le_mesh_allocate_index_data;
	le_mesh_i.read_attribute_data_into = le_mesh_read_attribute_data_into;

	le_mesh_i.set_vertex_count = le_mesh_set_vertex_count;
	le_mesh_i.get_vertex_count = le_mesh_get_vertex_count;

	le_mesh_i.get_index_count           = le_mesh_get_index_count;
	le_mesh_i.read_attribute_infos_into = le_mesh_read_attribute_infos_into;
	le_mesh_i.read_index_data_into      = le_mesh_read_index_data_into;

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;
}
