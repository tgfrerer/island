#ifndef GUARD_le_verlet_H
#define GUARD_le_verlet_H

#include "le_core.h"

#ifdef __cplusplus

// Unless explicitly disabled, enable glm types by default.
#	ifndef ISL_ALLOW_GLM_TYPES
#		define ISL_ALLOW_GLM_TYPES
#	endif

#	ifdef ISL_ALLOW_GLM_TYPES
#		include <glm/fwd.hpp>
#	endif

#endif

struct le_verlet_particle_system_o;

// clang-format off
struct le_verlet_api {

#ifdef ISL_ALLOW_GLM_TYPES
	typedef glm::vec2 Vertex;
#else
	struct Vertex{
		float x;
		float y;
	};
#endif

	// clang-format on
	struct FollowConstraint {
		uint32_t a;        // Point a     , index into particle system
		uint32_t b;        // Point b     , index into particle system
		uint32_t anchor;   // Anchor point, index into particle system
		uint32_t bCCW;     // Bool
		float    distance; // Distance between point a, b
		FollowConstraint( uint32_t const &a, uint32_t const &b, uint32_t const &anchor, uint32_t const &ccw )
		    : a( a )
		    , b( b )
		    , anchor( anchor )
		    , bCCW( ccw )
		    , distance( 0 ) {
		}
	};

	struct SpringConstraint {
		uint32_t a;        // index into particle system
		uint32_t b;        // index into particle system
		float    distance; // resting distance
		SpringConstraint( uint32_t const &a, uint32_t const &b )
		    : a( a )
		    , b( b )
		    , distance( 0 ) {
		}
	};

	struct Constraint {
		enum Type : uint32_t {
			eUndefined = 0,
			eSpring,
			eFollow,
		};
		Type type;
		union {
			FollowConstraint follow;
			SpringConstraint spring;
		};
		Constraint( FollowConstraint const &follow )
		    : type( eFollow )
		    , follow( follow ) {
		}
		Constraint( SpringConstraint const &spring )
		    : type( eSpring )
		    , spring( spring ) {
		}
	};
	// clang-format off

	struct le_verl_particle_system_interface_t{
		le_verlet_particle_system_o* ( * create             ) ( );
		void                       ( * destroy            ) ( le_verlet_particle_system_o* self );
		void                       ( * add_particles      ) ( le_verlet_particle_system_o* self, Vertex*p_vertex, size_t num_vertices);
		void                       ( * get_particles      ) ( le_verlet_particle_system_o* self, Vertex** p_vertex, size_t * num_vertices);
		size_t                     ( * get_particle_count ) ( le_verlet_particle_system_o* self );
		void                       ( * add_constraint     ) ( le_verlet_particle_system_o* self, Constraint const & constraint);
		void                       ( * update             ) ( le_verlet_particle_system_o* self, size_t num_steps );
		void                       ( * set_particle       ) ( le_verlet_particle_system_o* self, size_t idx, Vertex const & vertex );
	};

	le_verl_particle_system_interface_t  le_verlet_i;
};
// clang-format on

LE_MODULE( le_verlet );
LE_MODULE_LOAD_DEFAULT( le_verlet );

#ifdef __cplusplus
namespace le_verlet {
static const auto &api         = le_verlet_api_i;
static const auto &le_verlet_i = api -> le_verlet_i;
} // namespace le_verlet
#endif // __cplusplus

#endif
