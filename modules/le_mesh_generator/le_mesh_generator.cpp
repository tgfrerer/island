#include "le_mesh_generator.h"
#include "le_core/le_core.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include <le_mesh/le_mesh.h>
#include <le_mesh/le_mesh_types.h>

static void le_mesh_generator_generate_plane( le_mesh_o *mesh,
                                              float      width,
                                              float      height,
                                              uint32_t   numWidthSegments,
                                              uint32_t   numHeightSegments ) {

	le_mesh::le_mesh_i.clear( mesh );

	uint32_t ix = 0;
	uint32_t iz = 0;

	float deltaX = 1.f / float( numWidthSegments );
	float deltaZ = 1.f / float( numHeightSegments );

	// Build up vertices

	for ( iz = 0; iz <= numHeightSegments; ++iz ) {
		for ( ix = 0; ix <= numWidthSegments; ++ix ) {
			mesh->vertices.emplace_back( width * ( ix * deltaX - 0.5f ), 0, height * ( iz * deltaZ - 0.5f ) );
			mesh->normals.emplace_back( 0, 1, 0 );
			mesh->uvs.emplace_back( ix * deltaX, iz * deltaZ );
		}
	}

	// build up indices for mesh

	for ( uint32_t z = 0; z + 1 < iz; z++ ) {
		for ( uint32_t x = 0; x + 1 < ix; x++ ) {
			mesh->indices.push_back( x + 0 + ( z + 0 ) * ix );
			mesh->indices.push_back( x + 0 + ( z + 1 ) * ix );
			mesh->indices.push_back( x + 1 + ( z + 1 ) * ix );

			mesh->indices.push_back( x + 0 + ( z + 0 ) * ix );
			mesh->indices.push_back( x + 1 + ( z + 1 ) * ix );
			mesh->indices.push_back( x + 1 + ( z + 0 ) * ix );
		}
	}
};

// ----------------------------------------------------------------------
// Adapted from: https://github.com/mrdoob/three.js/blob/dev/src/geometries/SphereGeometry.js
static void le_mesh_generator_generate_sphere( le_mesh_o *mesh,
                                               float      radius,
                                               uint32_t   widthSegments,
                                               uint32_t   heightSegments,
                                               float      phiStart,
                                               float      phiLength,
                                               float      thetaStart,
                                               float      thetaLength ) {

	float thetaEnd = thetaStart + thetaLength;

	size_t numIndices  = 3 * 2 * heightSegments * widthSegments - widthSegments * 3 * 2;
	size_t numVertices = ( widthSegments + 1 ) * ( heightSegments + 1 );

	le_mesh::le_mesh_i.clear( mesh );

	mesh->indices.reserve( numIndices );
	mesh->vertices.reserve( numVertices );
	mesh->normals.reserve( numVertices );
	mesh->tangents.reserve( numVertices );
	mesh->uvs.reserve( numVertices );

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
			glm::vec3 tangent;

			float u = ix / float( widthSegments );

			// vertex
			vertex.x = -radius * cosf( phiStart + u * phiLength ) * sinf( thetaStart + v * thetaLength );
			vertex.y = radius * cosf( thetaStart + v * thetaLength );
			vertex.z = radius * sinf( phiStart + u * phiLength ) * sinf( thetaStart + v * thetaLength );

			// normal
			normal = glm::normalize( vertex );

			// tangents

			// We could calculate tangents based on the fact that each tangent field (x/y/z) is the derivative of the
			// corresponding vertex field (x/y/z) - but for a sphere we can take advantage of the special case where we say the
			// tangent is normalized the cross product of the y-axis with V-Origin. Since our sphere is centred on the origin,
			// this boils down to: normalize ({0,1,0} cross Vertex)
			//
			// See:
			// <https://computergraphics.stackexchange.com/questions/5498/compute-sphere-tangent-for-normal-mapping>

			tangent = glm::normalize( glm::cross( { 0, 1, 0 }, vertex ) );

			// Store vertex data
			mesh->uvs.emplace_back( u, 1 - v );
			mesh->vertices.emplace_back( vertex );
			mesh->normals.emplace_back( normal );
			mesh->tangents.emplace_back( tangent );

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
				mesh->indices.emplace_back( a );
				mesh->indices.emplace_back( d );
				mesh->indices.emplace_back( b );
			}
			if ( iy != heightSegments - 1 || thetaEnd < M_PI ) {
				// top triangle
				mesh->indices.emplace_back( d );
				mesh->indices.emplace_back( c );
				mesh->indices.emplace_back( b );
			}
		}
	}
}

// ----------------------------------------------------------------------
// generates box, and stores it into mesh. Note:
static void le_mesh_generator_generate_box( le_mesh_o *mesh, float width, float height, float depth ) {

	le_mesh::le_mesh_i.clear( mesh );

	// we must generate vertices first.
	// we should import this via ply and scale it to our needs.

	mesh->vertices.reserve( 4 * 6 );    //
	mesh->normals.reserve( 4 * 6 );     //
	mesh->uvs.reserve( 4 * 6 );         //
	mesh->indices.reserve( 3 * 2 * 6 ); // 2 triangles for each of 6 sides.

	struct cube_data {
		glm::vec3 vertex;
		glm::vec3 normal;
		glm::vec2 tex_coord;
	};

	// clang-format off
	cube_data unit_cube[] ={
	    {{-1.000000f, 1.000000f, -1.000000f}, {0.000000f, 1.000000f, -0.000000f}, {0.875000f, 0.500000f},},
	    {{1.000000f, 1.000000f, 1.000000f}, {0.000000f, 1.000000f, -0.000000f}, {0.625000f, 0.750000f},},
	    {{1.000000f, 1.000000f, -1.000000f},{ 0.000000f, 1.000000f, -0.000000f},{ 0.625000f, 0.500000f},},
	    {{1.000000f, 1.000000f, 1.000000f},{ 0.000000f, -0.000000f, 1.000000f},{ 0.625000f, 0.750000f},},
	    {{-1.000000f, -1.000000f, 1.000000f},{ 0.000000f, -0.000000f, 1.000000f},{ 0.375000f, 1.000000f},},
	    {{1.000000f, -1.000000f, 1.000000f},{ 0.000000f, -0.000000f, 1.000000f},{ 0.375000f, 0.750000f},},
	    {{-1.000000f, 1.000000f, 1.000000f},{ -1.000000f, 0.000000f, 0.000000f},{ 0.625000f, 0.000000f},},
	    {{-1.000000f, -1.000000f, -1.000000f},{ -1.000000f, 0.000000f, 0.000000f},{ 0.375000f, 0.250000f},},
	    {{-1.000000f, -1.000000f, 1.000000f},{ -1.000000f, 0.000000f, 0.000000f},{ 0.375000f, 0.000000f},},
	    {{1.000000f, -1.000000f, -1.000000f},{ 0.000000f, -1.000000f, 0.000000f},{ 0.375000f, 0.500000f},},
	    {{-1.000000f, -1.000000f, 1.000000f},{ 0.000000f, -1.000000f, 0.000000f},{ 0.125000f, 0.750000f},},
	    {{-1.000000f, -1.000000f, -1.000000f},{ 0.000000f, -1.000000f, 0.000000f},{ 0.125000f, 0.500000f},},
	    {{1.000000f, 1.000000f, -1.000000f},{ 1.000000f, -0.000000f, 0.000000f},{ 0.625000f, 0.500000f},},
	    {{1.000000f, -1.000000f, 1.000000f},{ 1.000000f, -0.000000f, 0.000000f},{ 0.375000f, 0.750000f},},
	    {{1.000000f, -1.000000f, -1.000000f},{ 1.000000f, -0.000000f, 0.000000f},{ 0.375000f, 0.500000f},},
	    {{-1.000000f, 1.000000f, -1.000000f},{ 0.000000f, 0.000000f, -1.000000f},{ 0.625000f, 0.250000f},},
	    {{1.000000f, -1.000000f, -1.000000f},{ 0.000000f, 0.000000f, -1.000000f},{ 0.375000f, 0.500000f},},
	    {{-1.000000f, -1.000000f, -1.000000f},{ 0.000000f, 0.000000f, -1.000000f},{ 0.375000f, 0.250000f},},
	    {{-1.000000f, 1.000000f, 1.000000f},{ 0.000000f, 1.000000f, 0.000000f},{ 0.875000f, 0.750000f},},
	    {{-1.000000f, 1.000000f, 1.000000f},{ 0.000000f, 0.000000f, 1.000000f},{ 0.625000f, 1.000000f},},
	    {{-1.000000f, 1.000000f, -1.000000f},{ -1.000000f, 0.000000f, 0.000000f},{ 0.625000f, 0.250000f},},
	    {{1.000000f, -1.000000f, 1.000000f},{ 0.000000f, -1.000000f, 0.000000f},{ 0.375000f, 0.750000f},},
	    {{1.000000f, 1.000000f, 1.000000f},{ 1.000000f, -0.000000f, 0.000000f},{ 0.625000f, 0.750000f},},
	    {{1.000000f, 1.000000f, -1.000000f},{ 0.000000f, 0.000000f, -1.000000f},{ 0.625000f, 0.500000f},}};
	// clang-format on

	// Since our standard cube has extents from -1..1, we must half input scale factor
	glm::vec3 scale_factor{ width * 0.5f, height * 0.5f, depth * 0.5f };

	for ( auto const &v : unit_cube ) {
		mesh->vertices.emplace_back( v.vertex * scale_factor );
		mesh->normals.emplace_back( v.normal );
		mesh->uvs.emplace_back( v.tex_coord );
	}

	// Set indices for box

	// clang-format off
	mesh->indices = {
		0, 1, 2,
		3, 4, 5,
		6, 7, 8,
		9, 10, 11,
		12, 13, 14,
		15, 16, 17,
		0, 18, 1,
		3, 19, 4,
		6, 20, 7,
		9, 21, 10,
		12, 22, 13,
		15, 23, 16,
	};
	// clang-format on
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh_generator, api ) {
	auto &le_mesh_generator_i = static_cast<le_mesh_generator_api *>( api )->le_mesh_generator_i;

	le_mesh_generator_i.generate_sphere = le_mesh_generator_generate_sphere;
	le_mesh_generator_i.generate_plane  = le_mesh_generator_generate_plane;
	le_mesh_generator_i.generate_box    = le_mesh_generator_generate_box;
}
