#include "le_mesh.h"
#include "le_core.h"

#include "le_mesh_types.h" //

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

ISL_API_ATTR void le_module_register_le_mesh_load_from_ply( void* api ); // ffdecl.

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh, api ) {
	auto& le_mesh_i = static_cast<le_mesh_api*>( api )->le_mesh_i;

	le_mesh_i.get_vertices = le_mesh_get_vertices;
	le_mesh_i.get_indices  = le_mesh_get_indices;
	le_mesh_i.get_uvs      = le_mesh_get_uvs;
	le_mesh_i.get_tangents = le_mesh_get_tangents;
	le_mesh_i.get_normals  = le_mesh_get_normals;
	le_mesh_i.get_colours  = le_mesh_get_colours;
	le_mesh_i.get_data     = le_mesh_get_data;

	le_module_register_le_mesh_load_from_ply( api );

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;
}
