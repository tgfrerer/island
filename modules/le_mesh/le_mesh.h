#ifndef GUARD_le_mesh_H
#define GUARD_le_mesh_H

#include "le_core.h"

struct le_mesh_o;

/*

  A modern mesh API:

  + we want the mesh to be able to draw itself
  + we want a mesh to be able to optimize itself

  + we want to have a pure-cpu mesh as well as a mesh that exists on the gpu.
  + how should we draw a mesh?




*/

// clang-format off
struct le_mesh_api {

    typedef uint16_t default_index_type;
    typedef float default_vertex_type[3];
    typedef float default_uv_type[2];
    typedef float default_colour_type[4];
    typedef float default_normal_type[3];
    typedef float default_tangent_type[3];

    static constexpr size_t ALL_VERTICES = ~size_t(0);


	enum attribute_name_t : uint32_t  {
			eUndefined = 0,
			ePosition ,
			eNormal ,
			eColour,
			eUv,
			eTangent,
		};

	struct attribute_info_t {
		    attribute_name_t name; //
			uint32_t bytes_per_vertex; // bytes per vertex for attribute
	};

	struct le_mesh_interface_t {

		le_mesh_o *    ( * create                   ) ( );
		void           ( * destroy                  ) ( le_mesh_o* self );

		void (*clear)(le_mesh_o* self);


		// I would like to change the api so that the mesh will copy into a given pointer
		// (bounded by size or num_bytes)

		// void (*get_vertices )( le_mesh_o *self, size_t* count, default_vertex_type const **   vertices); 	// 3 floats per vertex
		// void (*get_normals  )( le_mesh_o *self, size_t* count, default_normal_type const **   normals ); 	// 3 floats per vertex
		// void (*get_colours  )( le_mesh_o *self, size_t* count, default_colour_type const **   colours ); 	// 4 floats per vertex
		// void (*get_uvs      )( le_mesh_o *self, size_t* count, default_uv_type const **   uvs     ); 	// 2 floats per vertex
		// void (*get_tangents )( le_mesh_o *self, size_t* count, default_tangent_type const **   tangents); 	// 3 floats per vertex
		// void (*get_indices  )( le_mesh_o *self, size_t* count, default_index_type const ** indices ); // 1 uint16_t per index

		// void (*get_data     )( le_mesh_o *self, size_t* numVertices, size_t* numIndices, float const** vertices, float const **normals, float const **uvs, float const  ** colours, uint16_t const **indices);

		// New API


		// If attributes were already set, this means that these attributes will have their pointers invalidated
		void   (*set_vertex_count)( le_mesh_o * self , size_t num_vertices, bool * did_reallocate);
		size_t (*get_vertex_count)( le_mesh_o * self );

		size_t (*get_index_count)(le_mesh_o* self, uint32_t * num_bytes_per_index);

		// allocate attribute data
		void *(*allocate_attribute_data)( le_mesh_o * self, attribute_name_t attribute_name, uint32_t num_bytes_per_vertex);
		void *(*allocate_index_data)( le_mesh_o * self, size_t num_indices, uint32_t* num_bytes_per_index); // num_bytes_per_index can be 0, will be set to 2 or 4 depending on number of vertices, must be 4 if number of vertices is (2^16)

		void (*read_attribute_data_into)( le_mesh_o * self, void* target, size_t target_capacity_num_bytes, attribute_name_t attribute_name, size_t first_vertex, size_t *num_vertices, uint32_t* num_bytes_per_vertex, uint32_t stride);

		void (*read_attribute_info_into)(le_mesh_o*self, attribute_info_t* target, size_t *num_attributes_in_target);
		// void (*write_into_vertices )( le_mesh_o * self, size_t bytes_offset, float * const vertices, size_t * num_bytes);

		// PLY import

		bool (*load_from_ply_file)( le_mesh_o *self, char const *file_path );

	};

	le_mesh_interface_t       le_mesh_i;
};
// clang-format on
LE_MODULE( le_mesh );
LE_MODULE_LOAD_DEFAULT( le_mesh );

#ifdef __cplusplus

namespace le_mesh {
const auto         api       = le_mesh_api_i;
static const auto& le_mesh_i = api->le_mesh_i;
} // namespace le_mesh

class LeMesh : NoCopy, NoMove {
#	ifndef this_i
#		define this_i le_mesh::le_mesh_i

	le_mesh_o* self;

  public:
    LeMesh()
	    : self( this_i.create() ) {
	}

	~LeMesh() {
		this_i.destroy( self );
	}

	void clear() {
		this_i.clear( self );
	}

	// void getVertices( size_t* count, le_mesh_api::default_vertex_type const** pVertices = nullptr ) {
	// this_i.get_vertices( self, count, pVertices );
	// }

	// void getTangents( size_t* count, le_mesh_api::default_tangent_type const** pTangents = nullptr ) {
	// this_i.get_tangents( self, count, pTangents );
	// }

	// void getColours( size_t* count, le_mesh_api::default_colour_type const** pColours = nullptr ) {
	// this_i.get_colours( self, count, pColours );
	// }

	// void getNormals( size_t* count, le_mesh_api::default_normal_type const** pNormals = nullptr ) {
	// this_i.get_vertices( self, count, pNormals );
	// }

	// void getUvs( size_t* count, le_mesh_api::default_uv_type const** pUvs = nullptr ) {
	// this_i.get_uvs( self, count, pUvs );
	// }

	// void getIndices( size_t* count, le_mesh_api::default_index_type const** pIndices = nullptr ) {
	// this_i.get_indices( self, count, pIndices );
	// }

	// void getData( size_t* numVertices, size_t* numIndices, float const** pVertices = nullptr, float const** pNormals = nullptr, float const** pUvs = nullptr, float const** pColours = nullptr, uint16_t const** pIndices = nullptr ) {
	// this_i.get_data( self, numVertices, numIndices, pVertices, pNormals, pUvs, pColours, pIndices );
	// }

	bool loadFromPlyFile( char const* file_path ) {
		return this_i.load_from_ply_file( self, file_path );
	}

	operator auto() {
		return self;
	}
#		undef this_i
#	endif
};

namespace le {
using Mesh = LeMesh;
}

#endif // __cplusplus

#endif
