#ifndef GUARD_le_bspline_H
#define GUARD_le_bspline_H

/*
 * A module to handle B splines (basis function splines)).
 *
 */

#include "le_core/le_core.h"

#ifdef __cplusplus

#	ifndef ISL_ALLOW_GLM_TYPES
#		define ISL_ALLOW_GLM_TYPES 1
#	endif

// Life is terrible without 3d type primitives, so let's include some glm forward declarations

#	if ( ISL_ALLOW_GLM_TYPES == 1 )
#		include <glm/fwd.hpp>
#	endif

#endif

struct le_bspline_o;

// clang-format off
struct le_bspline_api {

#if (ISL_ALLOW_GLM_TYPES == 1)
	typedef glm::vec2 Vertex;
#else
	struct Vertex{
		float x;
		float y;
	};
#endif

	struct le_bspline_interface_t {

		le_bspline_o *       ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_bspline_o* self );

		void                 (* set_degree                ) ( le_bspline_o* self, uint32_t degree );
		void                 (* set_closed                ) ( le_bspline_o* self, bool closed);
		void                 (* set_points                ) ( le_bspline_o* self, Vertex const * points, size_t const num_points );
		void                 (* set_knots                 ) ( le_bspline_o* self, float const * knots, size_t const num_knots );
		void                 (* set_weights               ) ( le_bspline_o* self, float const* weights, size_t const num_weights );
		bool                 (* trace                     ) ( le_bspline_o* self, size_t resolution );
		void                 (* get_vertices_for_polyline ) ( le_bspline_o* self, Vertex const ** vertices, size_t * num_vertices );
	};

	le_bspline_interface_t       le_bspline_i;
};
// clang-format on

LE_MODULE( le_bspline );
LE_MODULE_LOAD_DEFAULT( le_bspline );

#ifdef __cplusplus

namespace le_bspline {
static const auto &api          = le_bspline_api_i;
static const auto &le_bspline_i = api -> le_bspline_i;

} // namespace le_bspline

class LeBspline : NoCopy, NoMove {

	le_bspline_o *self;

  public:
	LeBspline()
	    : self( le_bspline::le_bspline_i.create() ) {
	}

	LeBspline &setDegree( uint32_t const &degree ) {
		le_bspline::le_bspline_i.set_degree( self, degree );
		return *this;
	}

	LeBspline &setClosed( bool closed ) {
		le_bspline::le_bspline_i.set_closed( self, closed );
		return *this;
	}

	LeBspline &setPoints( le_bspline_api::Vertex const *points, size_t numPoints ) {
		le_bspline::le_bspline_i.set_points( self, points, numPoints );
		return *this;
	}

	LeBspline &setKnots( float const *knots, size_t numKnots ) {
		le_bspline::le_bspline_i.set_knots( self, knots, numKnots );
		return *this;
	}

	LeBspline &setWeights( float const *weights, size_t numWeights ) {
		le_bspline::le_bspline_i.set_weights( self, weights, numWeights );
		return *this;
	}

	bool trace( size_t resolution ) {
		return le_bspline::le_bspline_i.trace( self, resolution );
	}

	LeBspline &getVerticesForPolyline( le_bspline_api::Vertex const **pVertices, size_t *numVertices ) {
		le_bspline::le_bspline_i.get_vertices_for_polyline( self, pVertices, numVertices );
		return *this;
	}

	~LeBspline() {
		le_bspline::le_bspline_i.destroy( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
