#include "le_mesh.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include <math.h>
#include <vector>

#include "le_mesh_types.h"
// ----------------------------------------------------------------------

static le_mesh_o *le_mesh_create() {
	auto self = new le_mesh_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_mesh_destroy( le_mesh_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_mesh_clear( le_mesh_o *self ) {
	self->vertices.clear();
	self->normals.clear();
	self->uvs.clear();
	self->tangents.clear();
	self->indices.clear();
}

// ----------------------------------------------------------------------

static void le_mesh_get_vertices( le_mesh_o *self, size_t &count, float **vertices ) {
	count = self->vertices.size();
	if ( vertices ) {
		*vertices = static_cast<float *>( &self->vertices[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_tangents( le_mesh_o *self, size_t &count, float **tangents ) {
	count = self->tangents.size();
	if ( tangents ) {
		*tangents = static_cast<float *>( &self->tangents[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_indices( le_mesh_o *self, size_t &count, uint16_t **indices ) {
	count = self->indices.size();
	if ( indices ) {
		*indices = self->indices.data();
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_normals( le_mesh_o *self, size_t &count, float **normals ) {
	count = self->normals.size();
	if ( normals ) {
		*normals = static_cast<float *>( &self->normals[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_uvs( le_mesh_o *self, size_t &count, float **uvs ) {
	count = self->normals.size();
	if ( uvs ) {
		*uvs = static_cast<float *>( &self->uvs[ 0 ].x );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_get_data( le_mesh_o *self, size_t &numVertices, size_t &numIndices, float **vertices, float **normals, float **uvs, uint16_t **indices ) {
	numVertices = self->vertices.size();
	numIndices  = self->indices.size();

	if ( vertices ) {
		*vertices = static_cast<float *>( &self->vertices[ 0 ].x );
	}
	if ( normals ) {
		*normals = static_cast<float *>( &self->normals[ 0 ].x );
	}
	if ( uvs ) {
		*uvs = static_cast<float *>( &self->uvs[ 0 ].x );
	}
	if ( indices ) {
		*indices = self->indices.data();
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_mesh_api( void *api ) {
	auto &le_mesh_i = static_cast<le_mesh_api *>( api )->le_mesh_i;

	le_mesh_i.get_vertices = le_mesh_get_vertices;
	le_mesh_i.get_indices  = le_mesh_get_indices;
	le_mesh_i.get_uvs      = le_mesh_get_uvs;
	le_mesh_i.get_tangents = le_mesh_get_tangents;
	le_mesh_i.get_normals  = le_mesh_get_normals;
	le_mesh_i.get_data     = le_mesh_get_data;

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;
}
