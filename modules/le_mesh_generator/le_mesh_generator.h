#ifndef GUARD_le_mesh_generator_H
#define GUARD_le_mesh_generator_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_mesh_generator_o;

void register_le_mesh_generator_api( void *api );

// clang-format off
struct le_mesh_generator_api {
	static constexpr auto id      = "le_mesh_generator";
	static constexpr auto pRegFun = register_le_mesh_generator_api;

	struct le_mesh_generator_interface_t {

		 le_mesh_generator_o * (*create) ();

		void ( *generate_sphere )(
		    le_mesh_generator_o * self,
		    float                 radius,         //
		    uint32_t              widthSegments,  //
		    uint32_t              heightSegments, //
		    float                 phiStart,       // 0..2pi (default: 0)
		    float                 phiLength,      // 0..2pi (default: 2pi)
		    float                 thetaStart,     // 0..pi  (default: 0)
		    float                 thetaLength     // 0..pi  (default: pi)
		);

		void ( *generate_plane )(le_mesh_generator_o* self, float width, float height, uint32_t widthSegments, uint32_t heightSegments);

		void ( *destroy )( le_mesh_generator_o *self );

		void (*get_vertices )( le_mesh_generator_o *self, size_t& count, float **   vertices);
		void (*get_normals  )( le_mesh_generator_o *self, size_t& count, float **   normals );
		void (*get_uvs      )( le_mesh_generator_o *self, size_t& count, float **   uvs     );
		void (*get_tangents )( le_mesh_generator_o *self, size_t& count, float **   tangents);

		void (*get_indices  )( le_mesh_generator_o *self, size_t& count, uint16_t** indices );

		void (*get_data     )( le_mesh_generator_o *self, size_t& numVertices, size_t& numIndices, float** vertices, float**normals, float**uvs, uint16_t **indices);
	};

	le_mesh_generator_interface_t le_mesh_generator_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_mesh_generator {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_mesh_generator_api>( true );
#	else
const auto api = Registry::addApiStatic<le_mesh_generator_api>();
#	endif

static const auto &le_mesh_generator_i = api -> le_mesh_generator_i;

} // namespace le_mesh_generator

class LeMeshGenerator : NoCopy, NoMove {

	le_mesh_generator_o *self;

	static constexpr float PI = 3.14159265358979323846f;

  public:
	LeMeshGenerator()
	    : self( le_mesh_generator::le_mesh_generator_i.create() ) {
	}

	void generateSphere( float    radius         = 1.f,
	                     uint32_t widthSegments  = 3,
	                     uint32_t heightSegments = 2,
	                     float    phiStart       = 0.f,
	                     float    phiLength      = 2 * PI,
	                     float    thetaStart     = 0.f,
	                     float    thetaLength    = PI ) {

		le_mesh_generator::le_mesh_generator_i.generate_sphere( self, radius, widthSegments, heightSegments, phiStart, phiLength, thetaStart, thetaLength );
	}

	~LeMeshGenerator() {
		le_mesh_generator::le_mesh_generator_i.destroy( self );
	}

	void getVertices( size_t &count, float **pVertices = nullptr ) {
		le_mesh_generator::le_mesh_generator_i.get_vertices( self, count, pVertices );
	}

	void getTangents( size_t &count, float **pTangents = nullptr ) {
		le_mesh_generator::le_mesh_generator_i.get_tangents( self, count, pTangents );
	}

	void getNormals( size_t &count, float **pNormals = nullptr ) {
		le_mesh_generator::le_mesh_generator_i.get_vertices( self, count, pNormals );
	}

	void getUvs( size_t &count, float **pUvs = nullptr ) {
		le_mesh_generator::le_mesh_generator_i.get_uvs( self, count, pUvs );
	}

	void getIndices( size_t &count, uint16_t **pIndices = nullptr ) {
		le_mesh_generator::le_mesh_generator_i.get_indices( self, count, pIndices );
	}

	void getData( size_t &numVertices, size_t &numIndices, float **pVertices = nullptr, float **pNormals = nullptr, float **pUvs = nullptr, uint16_t **pIndices = nullptr ) {
		le_mesh_generator::le_mesh_generator_i.get_data( self, numVertices, numIndices, pVertices, pNormals, pUvs, pIndices );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
