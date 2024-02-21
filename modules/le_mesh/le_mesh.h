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


		// If attributes were already set, this means that these attributes will have their pointers invalidated - did_reallocate will tell you.
		void   (*set_vertex_count)( le_mesh_o * self , size_t num_vertices, bool * did_reallocate);
		size_t (*get_vertex_count)( le_mesh_o * self );

		size_t (*get_index_count)(le_mesh_o* self, uint32_t * num_bytes_per_index);

		/// Allocate attribute data
		/// @return                            : data pointer to where mesh memory lives. you should write into this immediately, and not keep this pointer around.
		/// @param `attribute_name`            : enum value which attribute value we want to allocate memory for.
		/// @param `num_bytes_per_vertex`      : number of bytes required for per-vertex for this attribute.
		///
		/// @note if this attribute has already been allocated, this function will just return a pointer to the attribute data.
		/// @note the total number of vertices is set by `set_vertex_count`, which will invalidate all attribute data pointers that were queried before `set_vertex_count`.
		/// @warning writing into allocated data is super finnicky - you must make sure that you don't write over the boundaries of the data that you allocated.
		///
		void *(*allocate_attribute_data)( le_mesh_o * self, attribute_name_t attribute_name, uint32_t num_bytes_per_vertex);
		void *(*allocate_index_data)( le_mesh_o * self, size_t num_indices, uint32_t* num_bytes_per_index); // num_bytes_per_index can be 0, will be set to 2 or 4 depending on number of vertices, must be 4 if number of vertices is (2^16)

		/// Read attribute data into `target`
		///
		/// @param `target`                    : pointer to where to write data to
		/// @param `target_capacity_num_bytes` : number of bytes held at `target` - this limits the maximum number of bytes that will be read into target.
		/// @param `attribute_name`            : name of the attribute from which to read data from
		/// @param `num_bytes_per_vertex`      : (optional) number of bytes per-vertex for this attribute, if set, this will return the actual number of bytes that this attribute requires per-vertex
		/// @param `num_vertices`              : (optional) number of vertices to read, if not set, will assume that you want to read any available vertices. if set, will return number of vertices that were read into `target`.
		/// @param `first_vertex`              : first vertex to read; this works as an offset, default is 0
		void (*read_attribute_data_into)( le_mesh_o const * self, void* target, size_t target_capacity_num_bytes, attribute_name_t attribute_name,  uint32_t* num_bytes_per_vertex, size_t *num_vertices, size_t first_vertex, uint32_t stride );

		/// Read index data into `target`
		///
		/// @param `target`                    : pointer to where to write data to
		/// @param `target_capacity_num_bytes` : number of bytes held at `target` - this limits the maximum number of bytes that will be read into target.
		/// @param `num_bytes_per_index`       : (optional) number of bytes per-index, if set, this will return the actual number of bytes required per-index
		/// @param `num_vertices`              : (optional) number of vertices to read, if not set, will assume that you want to read all available indices. if set, will return number of vertices that were read into `target`.
		/// @param `first_vertex`              : first vertex to read; this works as an offset, default is 0
		void (*read_index_data_into)( le_mesh_o const * self, void*target,size_t target_capacity_num_bytes, uint32_t *num_bytes_per_index,  size_t *num_indices, size_t first_index);

		/// Read attribute info into a given array of `attribute_info_t`.
		///
		/// @param `target`                    : (optional) pointer (or c-array) where to write data to.
		/// @param `num_attributes_in_target`  : (required) memory available in target, given as a multiple of `sizeof(attribute_info_t)`, returns total number of attributes available in mesh.
		void (*read_attribute_info_into)(le_mesh_o*self, attribute_info_t* target, size_t *num_attributes_in_target);

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

	size_t getIndexCount( uint32_t* num_bytes_per_index = nullptr ) {
		return this_i.get_index_count( self, num_bytes_per_index );
	}

	size_t getVertexCount() {
		return this_i.get_vertex_count( self );
	}

	void readAttributeInfoInto( le_mesh_api::attribute_info_t* target, size_t* num_attributes_in_target ) {
		this_i.read_attribute_info_into( self, target, num_attributes_in_target );
	}

	void readAttributeDataInto( void* target, size_t target_capacity_num_bytes, le_mesh_api::attribute_name_t attribute_name, uint32_t* num_bytes_per_vertex = nullptr, size_t* num_vertices = nullptr, size_t first_vertex = 0, uint32_t stride = 0 ) const {
		this_i.read_attribute_data_into( self, target, target_capacity_num_bytes, attribute_name, num_bytes_per_vertex, num_vertices, first_vertex, stride );
	}

	void readIndexDataInto( void* target, size_t target_capacity_num_bytes, uint32_t* num_bytes_per_index = nullptr, size_t* num_indices = nullptr, size_t first_index = 0 ) const {
		this_i.read_index_data_into( self, target, target_capacity_num_bytes, num_bytes_per_index, num_indices, first_index );
	}

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
