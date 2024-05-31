#include "le_mesh_generator.h"
#include "glm/geometric.hpp"
#include "le_core.h"
#include "le_mesh.h"
#include "le_log.h"
#include "glm/vec4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec2.hpp"

#define _USE_MATH_DEFINES
#include <math.h>
#include <cstring> // for memcpy
#include <vector>

static auto logger = le::Log( "le_mesh_generator" );

template <typename T>
static inline void copy_index( T* p_index, T val ) {
    *p_index = val;
}

static void le_mesh_generator_generate_plane( le_mesh_o* mesh,
                                              float      width,
                                              float      height,
                                              uint32_t   numWidthSegments,
                                              uint32_t numHeightSegments, uint32_t* p_num_bytes_per_index ) {

	le_mesh::le_mesh_i.clear( mesh );

	uint32_t ix = 0;
	uint32_t iz = 0;

	float deltaX = 1.f / float( numWidthSegments );
	float deltaZ = 1.f / float( numHeightSegments );

	bool was_reallocated = false;
	le_mesh::le_mesh_i.set_vertex_count( mesh, ( numHeightSegments + 1 ) * ( numWidthSegments + 1 ), &was_reallocated );

	auto position = ( glm::vec3* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::ePosition, sizeof( glm::vec3 ) );
	auto normal   = ( glm::vec3* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::eNormal, sizeof( glm::vec3 ) );
	auto uv       = ( glm::vec2* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::eUv, sizeof( glm::vec2 ) );

	// Build up vertices

	for ( iz = 0; iz <= numHeightSegments; ++iz ) {
		for ( ix = 0; ix <= numWidthSegments; ++ix ) {
			*position++ = { width * ( ix * deltaX - 0.5f ), 0, height * ( iz * deltaZ - 0.5f ) };
			*normal++   = { 0, 1, 0 };
			*uv++       = { ix * deltaX, iz * deltaZ };
		}
	}

	size_t num_indices = ( iz - 1 ) * ( ix - 1 ) * 6;

	uint32_t num_bytes_per_index = ( p_num_bytes_per_index ) ? *p_num_bytes_per_index : 0;
	void*    index_data          = le_mesh::le_mesh_i.allocate_index_data( mesh, num_indices, &num_bytes_per_index );

	if ( p_num_bytes_per_index ) {
		*p_num_bytes_per_index = num_bytes_per_index;
	}

	if ( num_bytes_per_index == 2 ) {
		uint16_t* i = ( uint16_t* )( index_data );
		for ( uint32_t z = 0; z + 1 < iz; z++ ) {
			for ( uint32_t x = 0; x + 1 < ix; x++ ) {

				*i++ = ( x + 0 + ( z + 0 ) * ix );
				*i++ = ( x + 0 + ( z + 1 ) * ix );
				*i++ = ( x + 1 + ( z + 1 ) * ix );

				*i++ = ( x + 0 + ( z + 0 ) * ix );
				*i++ = ( x + 1 + ( z + 1 ) * ix );
				*i++ = ( x + 1 + ( z + 0 ) * ix );
			}
		}

	} else if ( num_bytes_per_index == 4 ) {
		uint32_t* i = ( uint32_t* )( index_data );
		for ( uint32_t z = 0; z + 1 < iz; z++ ) {
			for ( uint32_t x = 0; x + 1 < ix; x++ ) {

				*i++ = ( x + 0 + ( z + 0 ) * ix );
				*i++ = ( x + 0 + ( z + 1 ) * ix );
				*i++ = ( x + 1 + ( z + 1 ) * ix );

				*i++ = ( x + 0 + ( z + 0 ) * ix );
				*i++ = ( x + 1 + ( z + 1 ) * ix );
				*i++ = ( x + 1 + ( z + 0 ) * ix );
			}
		}
	} else {
		logger.error( "Could not build mesh with index data type that requires %d bytes", num_bytes_per_index );
	}
};

// ----------------------------------------------------------------------
// Adapted from: https://github.com/mrdoob/three.js/blob/dev/src/geometries/SphereGeometry.js
static void le_mesh_generator_generate_sphere( le_mesh_o* mesh,
                                               float      radius,
                                               uint32_t   widthSegments,
                                               uint32_t   heightSegments,
                                               float      phiStart,
                                               float      phiLength,
                                               float      thetaStart,
                                               float      thetaLength,
                                               uint32_t*  p_num_bytes_per_index ) {

	float thetaEnd = thetaStart + thetaLength;

	size_t num_vertices = ( widthSegments + 1 ) * ( heightSegments + 1 );

	bool was_reallocated = false;
	le_mesh::le_mesh_i.set_vertex_count( mesh, num_vertices, &was_reallocated );

	size_t   ix;
	size_t   iy;
	uint32_t index = 0;

	std::vector<std::vector<uint32_t>> grid; // holds indices for rows of vertices

	auto mesh_position = ( glm::vec3* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::ePosition, sizeof( glm::vec3 ) );
	auto mesh_normal   = ( glm::vec3* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::eNormal, sizeof( glm::vec3 ) );
	auto mesh_tangent  = ( glm::vec3* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::eTangent, sizeof( glm::vec3 ) );
	auto mesh_uv       = ( glm::vec2* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::eUv, sizeof( glm::vec2 ) );

	// Generate vertices, normals and uvs
	for ( iy = 0; iy <= heightSegments; iy++ ) {

		std::vector<uint32_t> verticesRow;

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
			// We could calculate tangents based on the fact that each tangent field (x/y/z) is the derivative of the
			// corresponding vertex field (x/y/z) - but for a sphere we can take advantage of the special case where we say the
			// tangent is normalized the cross product of the y-axis with V-Origin. Since our sphere is centred on the origin,
			// this boils down to: normalize ({0,1,0} cross Vertex)
			//
			// See:
			// <https://computergraphics.stackexchange.com/questions/5498/compute-sphere-tangent-for-normal-mapping>

			tangent = glm::normalize( glm::cross( { 0, 1, 0 }, vertex ) );

			// Store vertex data
			*mesh_uv++       = { u, 1 - v };
			*mesh_position++ = vertex;
			*mesh_normal++   = normal;
			*mesh_tangent++  = tangent;

			verticesRow.push_back( index++ );
		}

		grid.emplace_back( verticesRow );
	}

	// Indices - we do a "dry run" to find out how many indices we will need...

	size_t num_indices = 0;
	for ( iy = 0; iy < heightSegments; iy++ ) {
		for ( ix = 0; ix < widthSegments; ix++ ) {

			if ( iy != 0 || thetaStart > 0 ) {
				num_indices += 3;
			}
			if ( iy != heightSegments - 1 || thetaEnd < M_PI ) {
				num_indices += 3;
			}
		}
	}

	uint32_t num_bytes_per_index = ( p_num_bytes_per_index ) ? *p_num_bytes_per_index : 0;
	void*    index_data          = le_mesh::le_mesh_i.allocate_index_data( mesh, num_indices, &num_bytes_per_index );

	if ( p_num_bytes_per_index ) {
		*p_num_bytes_per_index = num_bytes_per_index;
	}

	if ( num_bytes_per_index == 2 ) {
		auto i = reinterpret_cast<uint16_t*>( index_data );
		for ( iy = 0; iy < heightSegments; iy++ ) {
			for ( ix = 0; ix < widthSegments; ix++ ) {

				auto a = grid[ iy ][ ix + 1 ];
				auto b = grid[ iy ][ ix ];
				auto c = grid[ iy + 1 ][ ix ];
				auto d = grid[ iy + 1 ][ ix + 1 ];

				if ( iy != 0 || thetaStart > 0 ) {
					// bottom triangle
					*i++ = a;
					*i++ = d;
					*i++ = b;
				}
				if ( iy != heightSegments - 1 || thetaEnd < M_PI ) {
					// top triangle
					*i++ = d;
					*i++ = c;
					*i++ = b;
				}
			}
		}
	} else if ( num_bytes_per_index == 4 ) {
		auto i = reinterpret_cast<uint32_t*>( index_data );
		for ( iy = 0; iy < heightSegments; iy++ ) {
			for ( ix = 0; ix < widthSegments; ix++ ) {

				auto a = grid[ iy ][ ix + 1 ];
				auto b = grid[ iy ][ ix ];
				auto c = grid[ iy + 1 ][ ix ];
				auto d = grid[ iy + 1 ][ ix + 1 ];

				if ( iy != 0 || thetaStart > 0 ) {
					// bottom triangle
					*i++ = a;
					*i++ = d;
					*i++ = b;
				}
				if ( iy != heightSegments - 1 || thetaEnd < M_PI ) {
					// top triangle
					*i++ = d;
					*i++ = c;
					*i++ = b;
				}
			}
		}
	} else {
		logger.error( "Could not build mesh with index data type that requires %d bytes", num_bytes_per_index );
	}
}

// ----------------------------------------------------------------------
// generates box, and stores it into mesh. Note:
static void le_mesh_generator_generate_box( le_mesh_o* mesh, float width, float height, float depth ) {

	le_mesh::le_mesh_i.clear( mesh );

	bool was_reallocated = false;
	le_mesh::le_mesh_i.set_vertex_count( mesh, 4 * 6, &was_reallocated );

	// we must generate vertices first.
	// we should import this via ply and scale it to our needs.
	auto mesh_position = ( glm::vec3* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::ePosition, sizeof( glm::vec3 ) );
	auto mesh_normal   = ( glm::vec3* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::eNormal, sizeof( glm::vec3 ) );
	auto mesh_uv       = ( glm::vec2* )le_mesh::le_mesh_i.allocate_attribute_data( mesh, le_mesh_api::attribute_name_t::eUv, sizeof( glm::vec2 ) );

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

	for ( auto const& v : unit_cube ) {
		*mesh_position++ = ( v.vertex * scale_factor );
		*mesh_normal++   = v.normal;
		*mesh_uv++       = v.tex_coord;
	}

	// Set indices for box

	uint32_t num_bytes_per_index = 0;
	void*    index_data          = le_mesh::le_mesh_i.allocate_index_data( mesh, 3 * 2 * 6, &num_bytes_per_index );

	if ( num_bytes_per_index == 2 ) {
		uint16_t indices[ 3 * 2 * 6 ] = {
		    0, 1, 2,    //
		    3, 4, 5,    //
		    6, 7, 8,    //
		    9, 10, 11,  //
		    12, 13, 14, //
		    15, 16, 17, //
		    0, 18, 1,   //
		    3, 19, 4,   //
		    6, 20, 7,   //
		    9, 21, 10,  //
		    12, 22, 13, //
		    15, 23, 16, //
		};
		memcpy( index_data, indices, sizeof( indices ) );
	} else if ( num_bytes_per_index == 4 ) {
		uint32_t indices[ 3 * 2 * 6 ] = {
		    0, 1, 2,    //
		    3, 4, 5,    //
		    6, 7, 8,    //
		    9, 10, 11,  //
		    12, 13, 14, //
		    15, 16, 17, //
		    0, 18, 1,   //
		    3, 19, 4,   //
		    6, 20, 7,   //
		    9, 21, 10,  //
		    12, 22, 13, //
		    15, 23, 16, //
		};
		memcpy( index_data, indices, sizeof( indices ) );
	} else {
		logger.error( "Could not build mesh with index data type that requires %d bytes", num_bytes_per_index );
	}
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh_generator, api ) {
	auto& le_mesh_generator_i = static_cast<le_mesh_generator_api*>( api )->le_mesh_generator_i;

	le_mesh_generator_i.generate_sphere = le_mesh_generator_generate_sphere;
	le_mesh_generator_i.generate_plane  = le_mesh_generator_generate_plane;
	le_mesh_generator_i.generate_box    = le_mesh_generator_generate_box;
}
