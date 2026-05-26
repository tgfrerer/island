#ifndef GUARD_le_mesh_H
#define GUARD_le_mesh_H

#include "le_core.h"

struct le_mesh_o;
struct le_renderer_o;
struct le_rendergraph_o;
struct le_vertex_input_attribute_description; // defined in le_renderer_types.h
struct le_vertex_input_binding_description;   // defined in le_renderer_types.h
struct le_command_buffer_encoder_o;
struct le_renderpass_o;
/*

  A modern mesh API:

  + we want the mesh to be able to draw itself
  + we want a mesh to be able to optimize itself

  + we want to have a pure-cpu mesh as well as a mesh that exists on the gpu.
  + how should we draw a mesh?

*/

// clang-format off
struct le_mesh_api {

    // typedef uint16_t default_index_type;
    // typedef float default_vertex_type[3];
    // typedef float default_uv_type[2];
    // typedef float default_colour_type[4];
    // typedef float default_normal_type[3];
    // typedef float default_tangent_type[3];

    // static constexpr size_t ALL_VERTICES = ~size_t(0);


	enum attribute_name_t :uint8_t  {
			eUndefined = 0 << 0,
			//
			ePadding = eUndefined,
			//
			ePosition  = 1 << 0,
			eNormal    = 1 << 1,
			eColour    = 1 << 2,
			eUv        = 1 << 3,
			eTangent   = 1 << 4,
		};

	// clang-format on
	struct attribute_info_t {
		attribute_name_t name                          = {}; //
		uint32_t         bytes_per_vertex              = 0;  // bytes per vertex for this attribute (this may include padding if interleaved)
		                                                     // uint32_t         buffer_idx                    = 0;  // which buffer should be used for this?
	};
	// clang-format off

	struct le_mesh_interface_t {

		le_mesh_o *    ( * create                   ) ();
		void           ( * destroy                  ) ( le_mesh_o* self );

		/// Submits mesh(es) to rendergraph; introduces the mesh buffers to rendergraph; 
		void 		   ( * submit_meshes_to_rendergraph ) (le_mesh_o** meshes, size_t meshes_count, le_rendergraph_o* rg, le_renderer_o* renderer);

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
		/// @note the total number of vertices is set by `set_vertex_count`, which will invalidate all attribute data pointers that were queried before `set_vertex_count`.
		/// @warning writing into allocated data is super finnicky - you must make sure that you don't write over the boundaries of the data that you allocated.
		///
		void *(*allocate_index_data)( le_mesh_o * self, size_t num_indices, uint32_t* num_bytes_per_index); // num_bytes_per_index can be 0, will be set to 2 or 4 depending on number of vertices, must be 4 if number of vertices is (2^16)

		// Allocates one buffer for vertex data - vertex data may be interleaved, in which case attribute_infos must 
		// hold infos for more than one attribute in the correct order for interleaving.
		void *(*allocate_vertex_data)( le_mesh_o * self, attribute_info_t const * attribute_infos, size_t attribute_infos_count);


		void (*read_vertex_data_into_buffer)( le_mesh_o const* self, void* target, size_t target_capacity_num_bytes, le_mesh_api::attribute_info_t* dst_attribute_info, size_t dst_attribute_info_count, size_t first_vertex );

		/// Read attribute data into `target`
		///
		/// @param `target`                    : pointer to where to write data to
		/// @param `target_capacity_num_bytes` : number of bytes held at `target` - this limits the maximum number of bytes that will be read into target.
		/// @param `attribute_name`            : name of the attribute from which to read data from
		/// @param `num_bytes_per_vertex`      : out: (optional) number of bytes per-vertex for this attribute, if set, this will return the actual number of bytes that this attribute requires per-vertex
		/// @param `num_vertices`              : in/out: (optional) number of vertices to read, if not set, will assume that you want to read any available vertices. if set, will return number of vertices that were read into `target`.
		/// @param `first_vertex`              : first vertex to read; this works as an offset, default is 0
		/// @param `initial_stride_offset`     : initial write offset into target (in bytes) -- (initial_stride_offset + attribute_sz) <= stride, default is 0
		void (*read_attribute_data_into)( le_mesh_o const * self, void* target, size_t target_capacity_num_bytes, attribute_name_t attribute_name,  uint32_t* out_num_bytes_per_vertex, size_t *num_vertices, size_t first_vertex, uint32_t stride, uint32_t initial_stride_offset );

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
		/// @note   retuned attribute_infos are sorted asc by attribute_name.
		void (*read_attribute_infos_into)(le_mesh_o*self, attribute_info_t* target, size_t *num_attributes_in_target);

		// PLY import

		bool (*load_from_ply_file)( le_mesh_o *self, char const *file_path, bool should_interleave );

		// get the input attribute descriptions for any given bindings given by name and byte count
		uint32_t (*get_vertex_input_descriptions)( le_mesh_o* self, le_mesh_api::attribute_info_t const* attribute_infos, size_t attribute_infos_count, le_vertex_input_attribute_description* attribute_descriptions, size_t * attribute_descriptions_count , le_vertex_input_binding_description*   binding_descriptions, size_t *binding_descriptions_count);

		// bind vertex buffers and index buffers to the mesh if buffer handles exist
		// otherwise upload mesh data
		bool (*bind_to_encoder)(le_mesh_o* self, le_command_buffer_encoder_o* encoder, le_mesh_api::attribute_info_t const* attribute_infos, size_t attribute_infos_count);


		// Declare any buffers that have been created for this mesh to the renderpass 
		// so that these resoures may be used with an encoder.
		// attribute_infos is optional, if nullptr, all buffers available buffers will be declared as being used
		// if an index buffer exists, it will automatically be declared as being used by this renderpass
		void (*setup_renderpass)(le_mesh_o* self, le_renderpass_o* rp, le_mesh_api::attribute_info_t const* attribute_infos, size_t attribute_infos_count);
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

	bool setVertexCount( size_t num_vertices ) {
		bool did_reallocate = false;
		this_i.set_vertex_count( self, num_vertices, &did_reallocate );
		return did_reallocate;
	}

	size_t getIndexCount( uint32_t* num_bytes_per_index = nullptr ) {
		return this_i.get_index_count( self, num_bytes_per_index );
	}

	size_t getVertexCount() {
		return this_i.get_vertex_count( self );
	}

	[[nodiscard]]
	void* allocateIndexData( size_t num_indices, uint32_t* num_bytes_per_index ) {
		return this_i.allocate_index_data( self, num_indices, num_bytes_per_index );
	}

	[[nodiscard]]
	void* allocateVertexData( le_mesh_api::attribute_info_t const* attribute_infos, uint32_t attribute_infos_count ) {
		return this_i.allocate_vertex_data( self, attribute_infos, attribute_infos_count );
	}

	void readAttributeInfosInto( le_mesh_api::attribute_info_t* target, size_t* num_attributes_in_target ) {
		this_i.read_attribute_infos_into( self, target, num_attributes_in_target );
	}

	void readVertexDataIntoBuffer( void* target, size_t target_capacity_num_bytes, le_mesh_api::attribute_info_t* dst_attribute_info, size_t dst_attribute_info_count, size_t first_vertex = 0 ) {
		this_i.read_vertex_data_into_buffer( self, target, target_capacity_num_bytes, dst_attribute_info, dst_attribute_info_count, first_vertex );
	}

	[[deprecated]]
	void readAttributeDataInto( void* target, size_t target_capacity_num_bytes, le_mesh_api::attribute_name_t attribute_name, uint32_t* num_bytes_per_vertex = nullptr, size_t* num_vertices = nullptr, size_t first_vertex = 0, uint32_t stride = 0, uint32_t initial_stride_offset = 0 ) const {
		this_i.read_attribute_data_into( self, target, target_capacity_num_bytes, attribute_name, num_bytes_per_vertex, num_vertices, first_vertex, stride, initial_stride_offset );
	}

	void readIndexDataInto( void* target, size_t target_capacity_num_bytes, uint32_t* num_bytes_per_index = nullptr, size_t* num_indices = nullptr, size_t first_index = 0 ) const {
		this_i.read_index_data_into( self, target, target_capacity_num_bytes, num_bytes_per_index, num_indices, first_index );
	}

	bool loadFromPlyFile( char const* file_path, bool should_interleave = false ) {
		return this_i.load_from_ply_file( self, file_path, should_interleave );
	}

	// get any binding infos and attribute infos for the attributes contained in the ordered list attribute_infos
	// list order
	bool getVertexInputDescriptions( le_mesh_api::attribute_info_t const* attribute_infos, size_t attribute_infos_count, le_vertex_input_attribute_description* attribute_descriptions, size_t* attribute_descriptions_count, le_vertex_input_binding_description* binding_descriptions, size_t* binding_descriptions_count ) {
		return this_i.get_vertex_input_descriptions( self, attribute_infos, attribute_infos_count, attribute_descriptions, attribute_descriptions_count, binding_descriptions, binding_descriptions_count );
	}

	bool bind( le_command_buffer_encoder_o* encoder, le_mesh_api::attribute_info_t const* attribute_infos, size_t attribute_infos_count ) {
		return this_i.bind_to_encoder( self, encoder, attribute_infos, attribute_infos_count );
	}

	void setupRenderPass( le_renderpass_o* rp, le_mesh_api::attribute_info_t const* attribute_infos = nullptr, size_t attribute_infos_count = 0 ) {
		this_i.setup_renderpass( self, rp, attribute_infos, attribute_infos_count );
	}

	operator auto() {
		return self;
	}

#		undef this_i
#	endif

	static void submitMeshesToRendergraph( le_mesh_o** meshes, size_t meshes_count, le_rendergraph_o* rg, le_renderer_o* renderer ) {
		le_mesh_api_i->le_mesh_i.submit_meshes_to_rendergraph( meshes, meshes_count, rg, renderer );
	};
};

namespace le {
using Mesh = LeMesh;
}

#endif // __cplusplus

#endif
