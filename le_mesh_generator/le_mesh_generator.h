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
		    float                 phiStart,       // 0..2pi (default 0)
		    float                 phiLength,      // 0..2pi (default 2pi)
		    float                 thetaStart,     // 0..pi  (default 0)
		    float                 thetaLength     // 0..pi  (default 2pi)
		);

		void ( *destroy )( le_mesh_generator_o *self );

		void (*get_vertices )( le_mesh_generator_o *self, float ** vertices, size_t& count);
		void (*get_normals )( le_mesh_generator_o *self, float ** normals, size_t& count);
		void (*get_uvs )( le_mesh_generator_o *self, float ** uvs, size_t& count);
		void (*get_indices  )( le_mesh_generator_o* self, uint16_t**indices, size_t&count);
		void (*get_data)(le_mesh_generator_o*self, float** vertices, float**normals, float**uvs, uint16_t **indices, size_t& numVertices, size_t& numIndices);
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

	void getVertices( float **pVertices, size_t &count ) {
		le_mesh_generator::le_mesh_generator_i.get_vertices( self, pVertices, count );
	}

	void getNormals( float **pNormals, size_t &count ) {
		le_mesh_generator::le_mesh_generator_i.get_vertices( self, pNormals, count );
	}

	void getUvs( float **pUvs, size_t &count ) {
		le_mesh_generator::le_mesh_generator_i.get_uvs( self, pUvs, count );
	}

	void getIndices( uint16_t **pIndices, size_t &count ) {
		le_mesh_generator::le_mesh_generator_i.get_indices( self, pIndices, count );
	}

	void getData( float **pVertices, float **pNormals, float **pUvs, uint16_t **pIndices, size_t &numVertices, size_t &numIndices ) {
		le_mesh_generator::le_mesh_generator_i.get_data( self, pVertices, pNormals, pUvs, pIndices, numVertices, numIndices );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
