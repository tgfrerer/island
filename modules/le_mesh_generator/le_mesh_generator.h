#ifndef GUARD_le_mesh_generator_H
#define GUARD_le_mesh_generator_H

#include "le_core.h"

struct le_mesh_o;

// clang-format off
struct le_mesh_generator_api {

	struct le_mesh_generator_interface_t {

		void ( *generate_sphere )(
		    le_mesh_o * mesh,
		    float                 radius,         //
		    uint32_t              widthSegments,  //
		    uint32_t              heightSegments, //
		    float                 phiStart,       // 0..2pi (default: 0)
		    float                 phiLength,      // 0..2pi (default: 2pi)
		    float                 thetaStart,     // 0..pi  (default: 0)
		    float                 thetaLength,    // 0..pi  (default: pi)
		    uint32_t*             num_bytes_per_index
		);
		void ( *generate_plane )(le_mesh_o* mesh, float width, float height, uint32_t widthSegments, uint32_t heightSegments, uint32_t* num_bytes_per_index);
		void (* generate_box )(le_mesh_o* mesh, float width, float height, float depth);

	};

	le_mesh_generator_interface_t le_mesh_generator_i;
};
// clang-format on
LE_MODULE( le_mesh_generator );
LE_MODULE_LOAD_DEFAULT( le_mesh_generator );

#ifdef __cplusplus

namespace le_mesh_generator {
static const auto& api                 = le_mesh_generator_api_i;
static const auto& le_mesh_generator_i = api->le_mesh_generator_i;
} // namespace le_mesh_generator

namespace le {

class MeshGenerator : NoCopy, NoMove {

	static constexpr float PI = 3.14159265358979323846f;

  public:
    static void generateSphere( le_mesh_o* mesh, float radius = 1.f, uint32_t widthSegments = 3, uint32_t heightSegments = 2, float phiStart = 0.f, float phiLength = 2 * PI, float thetaStart = 0.f, float thetaLength = PI, uint32_t* num_bytes_per_index = nullptr ) {
        le_mesh_generator::le_mesh_generator_i.generate_sphere( mesh, radius, widthSegments, heightSegments, phiStart, phiLength, thetaStart, thetaLength, num_bytes_per_index );
	}

	static void generatePlane( le_mesh_o* mesh, float width, float height, uint32_t widthSegments = 2, uint32_t heightSegments = 2, uint32_t* num_bytes_per_index = nullptr ) {
		le_mesh_generator::le_mesh_generator_i.generate_plane( mesh, width, height, widthSegments, heightSegments, num_bytes_per_index = nullptr );
	}
};

} // namespace le

using LeMeshGenerator = le::MeshGenerator;

#endif // __cplusplus

#endif
