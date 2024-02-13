#include "le_mesh.h"
#include "le_core.h"

#include "le_mesh_types.h" //
#include <cstring>         // for memcopy

// ----------------------------------------------------------------------

static le_mesh_o* le_mesh_create() {
	auto self = new le_mesh_o();

	static_assert( sizeof( le_mesh_api::default_vertex_type ) == sizeof( le_mesh_o::vertex_t ), "vertices must have same size of declared type" );

	return self;
}

// ----------------------------------------------------------------------

static void le_mesh_destroy( le_mesh_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_mesh_clear( le_mesh_o* self ) {
	self->vertices.clear();
	self->normals.clear();
	self->uvs.clear();
	self->tangents.clear();
	self->colours.clear();
	self->indices.clear();
}

// ----------------------------------------------------------------------

static void le_mesh_get_vertices( le_mesh_o* self, size_t* count, float const** vertices ) {
	if ( count ) {
		*count = self->vertices.size();
	}
	if ( vertices ) {
		*vertices = static_cast<float*>( &self->vertices[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_tangents( le_mesh_o* self, size_t* count, float const** tangents ) {
	if ( count ) {
		*count = self->tangents.size();
	}
	if ( tangents ) {
		*tangents = static_cast<float*>( &self->tangents[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_indices( le_mesh_o* self, size_t* count, uint16_t const** indices ) {
	if ( count ) {
		*count = self->indices.size();
	}
	if ( indices ) {
		*indices = self->indices.data();
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_normals( le_mesh_o* self, size_t* count, float const** normals ) {
	if ( count ) {
		*count = self->normals.size();
	}
	if ( normals ) {
		*normals = static_cast<float*>( &self->normals[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_colours( le_mesh_o* self, size_t* count, float const** colours ) {
	if ( count ) {
		*count = self->colours.size();
	}
	if ( colours ) {
		*colours = static_cast<float*>( &self->colours[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_uvs( le_mesh_o* self, size_t* count, float const** uvs ) {
	if ( count ) {
		*count = self->normals.size();
	}
	if ( uvs ) {
		*uvs = static_cast<float*>( &self->uvs[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_data( le_mesh_o* self, size_t* numVertices, size_t* numIndices, float const** vertices, float const** normals, float const** uvs, float const** colours, uint16_t const** indices ) {
	if ( numVertices ) {
		*numVertices = self->vertices.size();
	}
	if ( numIndices ) {
		*numIndices = self->indices.size();
	}

	if ( vertices ) {
		*vertices = self->vertices.empty() ? nullptr : static_cast<float const*>( &self->vertices[ 0 ].x );
	}

	if ( colours ) {
		*colours = self->colours.empty() ? nullptr : static_cast<float const*>( &self->colours[ 0 ].x );
	}

	if ( normals ) {
		*normals = self->normals.empty() ? nullptr : static_cast<float const*>( &self->normals[ 0 ].x );
	}

	if ( uvs ) {
		*uvs = self->uvs.empty() ? nullptr : static_cast<float const*>( &self->uvs[ 0 ].x );
	}

	if ( indices ) {
		*indices = self->indices.data();
	}
}
// ----------------------------------------------------------------------

// you can use this to write data to any position you like - you
// can use this to combine mesh data by appending into a data buffer, where you left off
//
// write(vertices, num_bytes)
// write(vertices+num_bytes/sizeof(float), num_bytes);
static void le_mesh_write_into_vertices( le_mesh_o* self, float* const vertices, size_t* num_bytes ) {

	if ( nullptr == num_bytes ) {
		return;
	}

	// ----------| invariant: num_bytes must be a valid pointer

	size_t max_src_bytes = self->vertices.size() * sizeof( le_mesh_api::default_vertex_type );

	// update num_bytes with the number of bytes that we intend to write.
	*num_bytes = std::min( *num_bytes, max_src_bytes );

	if ( vertices ) {
		memcpy( vertices, self->vertices.data(), *num_bytes );
	}
}
// ----------------------------------------------------------------------

ISL_API_ATTR void le_module_register_le_mesh_load_from_ply( void* api ); // ffdecl.

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh, api ) {
	auto& le_mesh_i = static_cast<le_mesh_api*>( api )->le_mesh_i;

	le_mesh_i.get_vertices        = le_mesh_get_vertices;
	le_mesh_i.get_indices         = le_mesh_get_indices;
	le_mesh_i.get_uvs             = le_mesh_get_uvs;
	le_mesh_i.get_tangents        = le_mesh_get_tangents;
	le_mesh_i.get_normals         = le_mesh_get_normals;
	le_mesh_i.get_colours         = le_mesh_get_colours;
	le_mesh_i.get_data            = le_mesh_get_data;
	le_mesh_i.write_into_vertices = le_mesh_write_into_vertices;

	le_module_register_le_mesh_load_from_ply( api );

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;
}
