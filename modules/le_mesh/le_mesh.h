#ifndef GUARD_le_mesh_H
#define GUARD_le_mesh_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_mesh_o;

void register_le_mesh_api( void *api );

// clang-format off
struct le_mesh_api {
	static constexpr auto id      = "le_mesh";
	static constexpr auto pRegFun = register_le_mesh_api;

	struct le_mesh_interface_t {

		le_mesh_o *    ( * create                   ) ( );
		void           ( * destroy                  ) ( le_mesh_o* self );

		void (*clear)(le_mesh_o* self);

		void (*get_vertices )( le_mesh_o *self, size_t& count, float **   vertices);
		void (*get_normals  )( le_mesh_o *self, size_t& count, float **   normals );
		void (*get_colours  )( le_mesh_o *self, size_t& count, float **   colours );
		void (*get_uvs      )( le_mesh_o *self, size_t& count, float **   uvs     );
		void (*get_tangents )( le_mesh_o *self, size_t& count, float **   tangents);
		void (*get_indices  )( le_mesh_o *self, size_t& count, uint16_t** indices );

		void (*get_data     )( le_mesh_o *self, size_t& numVertices, size_t& numIndices, float** vertices, float**normals, float**uvs, float ** colours, uint16_t **indices);

		bool (*load_from_ply_file)( le_mesh_o *self, char const *file_path );

	};

	le_mesh_interface_t       le_mesh_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_mesh {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_mesh_api>( true );
#	else
const auto api = Registry::addApiStatic<le_mesh_api>();
#	endif

static const auto &le_mesh_i = api -> le_mesh_i;

} // namespace le_mesh

class LeMesh : NoCopy, NoMove {
#	ifndef this_i
#		define this_i le_mesh::le_mesh_i

	le_mesh_o *self;

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

	void getVertices( size_t &count, float **pVertices = nullptr ) {
		this_i.get_vertices( self, count, pVertices );
	}

	void getTangents( size_t &count, float **pTangents = nullptr ) {
		this_i.get_tangents( self, count, pTangents );
	}

	void getColours( size_t &count, float **pColours = nullptr ) {
		this_i.get_colours( self, count, pColours );
	}

	void getNormals( size_t &count, float **pNormals = nullptr ) {
		this_i.get_vertices( self, count, pNormals );
	}

	void getUvs( size_t &count, float **pUvs = nullptr ) {
		this_i.get_uvs( self, count, pUvs );
	}

	void getIndices( size_t &count, uint16_t **pIndices = nullptr ) {
		this_i.get_indices( self, count, pIndices );
	}

	void getData( size_t &numVertices, size_t &numIndices, float **pVertices = nullptr, float **pNormals = nullptr, float **pUvs = nullptr, float **pColours = nullptr, uint16_t **pIndices = nullptr ) {
		this_i.get_data( self, numVertices, numIndices, pVertices, pNormals, pUvs, pColours, pIndices );
	}

	bool loadFromPlyFile( char const *file_path ) {
		return this_i.load_from_ply_file( self, file_path );
	}

	operator auto() {
		return self;
	}
#		undef this_i
#	endif
};

#endif // __cplusplus

#endif
