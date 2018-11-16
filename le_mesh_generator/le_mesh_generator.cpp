#include "le_mesh_generator.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include <vector>
#include <math.h>
#include "glm.hpp"

// members
struct le_mesh_generator_o {

	std::vector<glm::vec3> vertices; // 3d position in model space
	std::vector<glm::vec3> normals;  // normalsied normal, per-vertex
	std::vector<glm::vec2> uvs;      //
	std::vector<uint16_t>  indices;  // index
};

le_mesh_generator_o *le_mesh_generator_create() {
	auto self = new le_mesh_generator_o;
	return self;
}

// ----------------------------------------------------------------------
// Adapted from: https://github.com/mrdoob/three.js/blob/dev/src/geometries/SphereGeometry.js
static void le_mesh_generator_generate_sphere( le_mesh_generator_o *self,
                                               float                radius,
                                               uint32_t             widthSegments,
                                               uint32_t             heightSegments,
                                               float                phiStart,
                                               float                phiLength,
                                               float                thetaStart,
                                               float                thetaLength ) {

	float thetaEnd = thetaStart + thetaLength;

	size_t numIndices  = 3 * 2 * heightSegments * widthSegments - widthSegments * 3 * 2;
	size_t numVertices = ( widthSegments + 1 ) * ( heightSegments + 1 );

	self->indices.clear();
	self->vertices.clear();
	self->normals.clear();
	self->uvs.clear();

	self->indices.reserve( numIndices );
	self->vertices.reserve( numVertices );
	self->normals.reserve( numVertices );
	self->uvs.reserve( numVertices );

	size_t   ix;
	size_t   iy;
	uint16_t index = 0;

	std::vector<std::vector<uint16_t>> grid; // holds indices for rows of vertices

	// Generate vertices, normals and uvs
	for ( iy = 0; iy <= heightSegments; iy++ ) {

		std::vector<uint16_t> verticesRow;

		float v = iy / float( heightSegments );

		for ( ix = 0; ix <= widthSegments; ix++ ) {
			glm::vec3 vertex;
			glm::vec3 normal;

			float u = ix / float( widthSegments );

			// vertex
			vertex.x = -radius * cosf( phiStart + u * phiLength ) * sinf( thetaStart + v * thetaLength );
			vertex.y = radius * cosf( thetaStart + v * thetaLength );
			vertex.z = radius * sinf( phiStart + u * phiLength ) * sinf( thetaStart + v * thetaLength );

			// normal
			normal = glm::normalize( vertex );

			// Store vertex data
			self->uvs.emplace_back( u, 1 - v );
			self->vertices.emplace_back( vertex );
			self->normals.emplace_back( normal );

			// Store index
			verticesRow.emplace_back( index++ );
		}

		grid.emplace_back( verticesRow );
	}

	// indices

	for ( iy = 0; iy < heightSegments; iy++ ) {
		for ( ix = 0; ix < widthSegments; ix++ ) {

			auto a = grid[ iy ][ ix + 1 ];
			auto b = grid[ iy ][ ix ];
			auto c = grid[ iy + 1 ][ ix ];
			auto d = grid[ iy + 1 ][ ix + 1 ];

			if ( iy != 0 || thetaStart > 0 ) {
				// bottom triangle
				self->indices.emplace_back( a );
				self->indices.emplace_back( d );
				self->indices.emplace_back( b );
			}
			if ( iy != heightSegments - 1 || thetaEnd < M_PI ) {
				// top triangle
				self->indices.emplace_back( d );
				self->indices.emplace_back( c );
				self->indices.emplace_back( b );
			}
		}
	}
}

// ----------------------------------------------------------------------

static void le_mesh_generator_get_vertices( le_mesh_generator_o *self, float **vertices, size_t &count ) {
	count     = self->vertices.size();
	*vertices = static_cast<float *>( &self->vertices[ 0 ].x );
}

// ----------------------------------------------------------------------

static void le_mesh_generator_get_indices( le_mesh_generator_o *self, uint16_t **indices, size_t &count ) {
	count    = self->indices.size();
	*indices = self->indices.data();
}

// ----------------------------------------------------------------------

static void le_mesh_generator_get_normals( le_mesh_generator_o *self, float **normals, size_t &count ) {
	count    = self->normals.size();
	*normals = static_cast<float *>( &self->normals[ 0 ].x );
}

// ----------------------------------------------------------------------

static void le_mesh_generator_get_uvs( le_mesh_generator_o *self, float **uvs, size_t &count ) {
	count = self->normals.size();
	*uvs  = static_cast<float *>( &self->uvs[ 0 ].x );
}

// ----------------------------------------------------------------------

static void le_mesh_generator_get_data( le_mesh_generator_o *self, float **vertices, float **normals, float **uvs, uint16_t **indices, size_t &numVertices, size_t &numIndices ) {
	numVertices = self->vertices.size();
	*vertices   = static_cast<float *>( &self->vertices[ 0 ].x );
	*normals    = static_cast<float *>( &self->normals[ 0 ].x );
	*uvs        = static_cast<float *>( &self->uvs[ 0 ].x );
	numIndices  = self->indices.size();
	*indices    = self->indices.data();
}

// ----------------------------------------------------------------------

static void le_mesh_generator_destroy( le_mesh_generator_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_mesh_generator_api( void *api ) {
	auto &le_mesh_generator_i = static_cast<le_mesh_generator_api *>( api )->le_mesh_generator_i;

	le_mesh_generator_i.create = le_mesh_generator_create;

	le_mesh_generator_i.get_vertices = le_mesh_generator_get_vertices;
	le_mesh_generator_i.get_indices  = le_mesh_generator_get_indices;
	le_mesh_generator_i.get_uvs      = le_mesh_generator_get_uvs;
	le_mesh_generator_i.get_normals  = le_mesh_generator_get_normals;
	le_mesh_generator_i.get_data     = le_mesh_generator_get_data;

	le_mesh_generator_i.generate_sphere = le_mesh_generator_generate_sphere;
	le_mesh_generator_i.destroy         = le_mesh_generator_destroy;
}
