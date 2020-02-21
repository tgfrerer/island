#include "le_bspline.h"
#include "le_core/le_core.h"

/* B-Spline methods
 *
 * Implementation based on <https://github.com/thibauts/b-spline/>
 */

#include <vector>
#include "glm/glm.hpp"
#include <iostream>
#include <iomanip>

using Vertex = glm::vec2;

struct le_bspline_o {
	uint32_t degree = 1; // must be at least 1

	uint16_t dirty  = 1;
	uint16_t closed = 0;

	std::vector<float>  knots;
	std::vector<Vertex> points;
	std::vector<float>  weight;

	std::vector<Vertex> polyline;
};

// ----------------------------------------------------------------------

static le_bspline_o *le_bspline_create() {
	auto self = new le_bspline_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_bspline_destroy( le_bspline_o *self ) {
	delete self;
}
// ----------------------------------------------------------------------

static void le_bspline_set_degree( le_bspline_o *self, uint32_t degree ) {
	self->degree = degree;
}

// ----------------------------------------------------------------------
static void le_bspline_set_closed( le_bspline_o *self, bool closed ) {
	self->closed = closed;
}
// ----------------------------------------------------------------------
static void le_bspline_set_points( le_bspline_o *self, Vertex const *points, size_t const num_points ) {
	self->points = {points, points + num_points};
}

// ----------------------------------------------------------------------
static void le_bspline_set_knots( le_bspline_o *self, float const *knots, size_t const num_knots ) {
	self->knots = {knots, knots + num_knots};
}

// ----------------------------------------------------------------------
static void le_bspline_set_weights( le_bspline_o *self, float const *weights, size_t const num_weights ) {
	self->weight = {weights, weights + num_weights};
}

// ----------------------------------------------------------------------
static bool le_bspline_trace( le_bspline_o *self, size_t resolution ) {

	assert( resolution > 1 ); // resolution must be at least 2, otherwise we cannot cover at least start and endpoint.

	size_t n = self->points.size();

	if ( self->degree < 1 ) {
		assert( false ); // degree must be >= 1
		return false;
	}

	if ( self->degree > ( n - 1 ) ) {
		assert( false ); // degree must be less or equal to point count -1
		return false;
	}

	// Initialise weights to 1 if no weights given.
	// must have one weight per point
	if ( self->weight.empty() ) {
		self->weight = std::vector<float>( n, 1.f );
	} else {
		// Ensure that a weight is given for each point
		if ( self->weight.size() != self->points.size() ) {
			assert( false );
			return false;
		}
	}

	// Initisalise knots if not given, check number
	// of knots if knots given.
	{
		// If closed, add a second helping of (degree+1) to number of knots
		size_t numKnots = n + ( self->degree + 1 ) * ( self->closed ? 2 : 1 );

		if ( self->knots.empty() ) {
			self->knots.reserve( numKnots );
			for ( size_t i = 0; i != numKnots; i++ ) {
				self->knots.push_back( i );
			}
		} else {
			// must ensure that number of knots is correct
			if ( self->knots.size() != numKnots ) {
				assert( false );
				return false;
			}
		}
	}
	// ---------| invariant: number of knots is number of points + degree + 1

	size_t domain[ 2 ] = {self->degree, self->knots.size() - ( self->degree + 1 )};
	float  low         = self->knots[ domain[ 0 ] ];
	float  high        = self->knots[ domain[ 1 ] - 1 ];

	self->polyline.clear();

	size_t s = domain[ 0 ]; // current segment
	for ( size_t r = 0; r < resolution; r++ ) {

		// homogenous coordinates for points
		std::vector<glm::vec3> v;

		// Create homogenous coordinates for each point
		{

			const size_t numPoints     = self->points.size();
			size_t       numIterations = numPoints;
			if ( self->closed ) {
				numIterations += self->degree + 1;
			}

			v.reserve( numIterations );
			for ( size_t i = 0; i != numIterations; ++i ) {
				auto &p = self->points[ i % numPoints ];
				auto &w = self->weight[ i % numPoints ];
				v.emplace_back( p.x * w, p.y * w, w );
			}
		}

		float t = float( r ) / float( resolution - 1 );
		t       = t * ( high - low ) + low; // map t to domain

		for ( ; s < domain[ 1 ]; ) {
			if ( t >= self->knots[ s ] && t <= self->knots[ s + 1 ] ) {
				break;
			}
			s++;
		}

		// l (level) goes from 1 to the curve degree + 1
		for ( size_t l = 1; l <= self->degree + 1; ++l ) {
			// build level l of the pyramid
			for ( size_t i = s; i > ( s - self->degree - 1 + l ); i-- ) {
				float alpha = ( t - self->knots[ i ] ) /
				              ( self->knots[ i + self->degree + 1 - l ] - self->knots[ i ] );
				v[ i ] = ( 1.f - alpha ) * v[ i - 1 ] + alpha * v[ i ];
			}
		}

		// Convert back (unproject homogenous coordinates) and store in output vector.

		self->polyline.emplace_back( v[ s ].x / v[ s ].z,
		                             v[ s ].y / v[ s ].z );
	}

	return true;
}

// ----------------------------------------------------------------------

static void le_bspline_get_vertices_for_polyline( le_bspline_o *self, Vertex const **vertices, size_t *num_vertices ) {
	*vertices     = self->polyline.data();
	*num_vertices = self->polyline.size();
};

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_bspline_api( void *api ) {
	auto &le_bspline_i = static_cast<le_bspline_api *>( api )->le_bspline_i;

	le_bspline_i.create                    = le_bspline_create;
	le_bspline_i.destroy                   = le_bspline_destroy;
	le_bspline_i.set_degree                = le_bspline_set_degree;
	le_bspline_i.set_closed                = le_bspline_set_closed;
	le_bspline_i.set_points                = le_bspline_set_points;
	le_bspline_i.set_knots                 = le_bspline_set_knots;
	le_bspline_i.set_weights               = le_bspline_set_weights;
	le_bspline_i.trace                     = le_bspline_trace;
	le_bspline_i.get_vertices_for_polyline = le_bspline_get_vertices_for_polyline;
}
