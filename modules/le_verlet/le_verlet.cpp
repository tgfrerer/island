#include "le_verlet.h"
#include "le_core/le_core.h"

#include <vector>
#include <glm.hpp>

typedef le_verlet_api::Constraint Constraint;
static constexpr float            cSTIFFNESS = 0.01445f;

struct le_verlet_particle_system_o {
	std::vector<glm::vec2>  pos;
	std::vector<glm::vec2>  prev_pos;
	std::vector<Constraint> constraints;
};

// ----------------------------------------------------------------------

static le_verlet_particle_system_o *le_verlet_create() {
	auto self = new le_verlet_particle_system_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_verlet_destroy( le_verlet_particle_system_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_verlet_apply_constraints( le_verlet_particle_system_o *self, size_t numSteps ) {
	float stepCoeff = 1.f / float( numSteps );
	auto &pos       = self->pos;

	for ( auto const &c : self->constraints ) {
		for ( size_t i = 0; i != numSteps; ++i ) {
			// Each constraint is evaluated numSteps times, and thus numerically integrated over n discrete steps
			switch ( c.type ) {
			case ( Constraint::eFollow ): {
				glm::vec2 AnchorToB        = glm::normalize( pos[ c.follow.b ] - pos[ c.follow.anchor ] );
				float     length2AnchorToB = dot( AnchorToB, AnchorToB );
				// We test against 0 so that there cannot be a division by zero
				if ( length2AnchorToB > std::numeric_limits<float>::epsilon() ) {
					AnchorToB /= sqrtf( length2AnchorToB );
					glm::vec2 unitNormal{c.follow.bCCW ? -AnchorToB.y : +AnchorToB.y,
						                 c.follow.bCCW ? +AnchorToB.x : -AnchorToB.x};
					pos[ c.follow.a ] = pos[ c.follow.b ] + c.follow.distance * unitNormal;
				} else {
					// Length anchorToB is 0 -> leave unchanged, as we can't calculate unit normal
				}

			} break;
			case ( Constraint::eSpring ): {
				glm::vec2 force       = pos[ c.spring.a ] - pos[ c.spring.b ];
				float     fMagnitude2 = glm::dot( force, force );
				if ( fMagnitude2 > std::numeric_limits<float>::epsilon() ) {
					//force *= ((mDistance * mDistance - fMagnitude2) / fMagnitude2) * mStiffness * stepCoeff;
					force *= ( ( c.spring.distance * c.spring.distance - fMagnitude2 ) / fMagnitude2 ) * cSTIFFNESS * stepCoeff;
					pos[ c.spring.a ] += force;
					pos[ c.spring.b ] -= force;
				}
			} break;
			default:
				assert( false );
				break;
			}
		}
	}
}

// ----------------------------------------------------------------------

static void le_verlet_add_particles( le_verlet_particle_system_o *self, glm::vec2 *p_vertex, size_t num_vertices ) {
	self->pos.insert( self->pos.end(), p_vertex, p_vertex + num_vertices );
	self->prev_pos.insert( self->prev_pos.end(), p_vertex, p_vertex + num_vertices );
}

// ----------------------------------------------------------------------
// Setup constraint based on positions for indexed particles,
// then add it to the particle system.
static void le_verlet_add_constraint( le_verlet_particle_system_o *self, Constraint const &constraint ) {
	// setup constraint
	auto  c   = constraint;
	auto &pos = self->pos;

	switch ( c.type ) {
	case ( Constraint::eFollow ): {
		c.follow.distance = glm::length( pos[ c.follow.a ] - pos[ c.follow.b ] );
	} break;
	case ( Constraint::eSpring ): {
		c.spring.distance = glm::length( pos[ c.spring.b ] - pos[ c.spring.a ] );
	} break;
	default:
		assert( false );
	    break;
	}

	self->constraints.emplace_back( std::move( c ) );
}

// ----------------------------------------------------------------------

static void le_verlet_update( le_verlet_particle_system_o *self, size_t num_steps ) {

	// first update velocity, friction for all particles.

	assert( self->pos.size() == self->prev_pos.size() );

	size_t const num_elements = self->pos.size();

	for ( size_t i = 0; i != num_elements; ++i ) {
		auto &p        = self->pos[ i ];
		auto &pp       = self->prev_pos[ i ];
		auto  velocity = ( p - pp );

		// Store current pos as previous pos
		pp = p;

		// Apply friction
		velocity *= 0.995;

		// Apply inertia
		p += velocity;
	}

	// Then update constraints, iterate each by step

	le_verlet_apply_constraints( self, num_steps );
}

// ----------------------------------------------------------------------

static void le_verlet_get_particles( le_verlet_particle_system_o *self, le_verlet_api::Vertex **vertices, size_t *num_vertices ) {
	*vertices = self->pos.data();
	if ( num_vertices ) {
		*num_vertices = self->pos.size();
	}
}

// ----------------------------------------------------------------------

static void le_verlet_set_particle( le_verlet_particle_system_o *self, size_t idx, le_verlet_api::Vertex const &vertex ) {

	assert( self->pos.size() == self->prev_pos.size() );

	if ( idx < self->pos.size() ) {
		self->prev_pos[ idx ] = self->pos[ idx ] = vertex;
	}
}

// ----------------------------------------------------------------------

static size_t le_verlet_get_particle_count( le_verlet_particle_system_o *self ) {
	return self->pos.size();
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_verlet_api( void *api ) {
	auto &le_verlet_i = static_cast<le_verlet_api *>( api )->le_verlet_i;

	le_verlet_i.create             = le_verlet_create;
	le_verlet_i.destroy            = le_verlet_destroy;
	le_verlet_i.update             = le_verlet_update;
	le_verlet_i.add_particles      = le_verlet_add_particles;
	le_verlet_i.add_constraint     = le_verlet_add_constraint;
	le_verlet_i.get_particles      = le_verlet_get_particles;
	le_verlet_i.get_particle_count = le_verlet_get_particle_count;
	le_verlet_i.set_particle       = le_verlet_set_particle;
}
