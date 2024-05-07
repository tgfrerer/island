#include "le_path.h"

#include "le_log.h"

#include <vector>
#include <algorithm>

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "glm/glm.hpp"
#include "glm/gtx/vector_query.hpp"
#include "glm/gtx/vector_angle.hpp"
#include "glm/gtx/rotate_vector.hpp"

using stroke_attribute_t = le_path_api::stroke_attribute_t;

static auto logger = le::Log( "le_path" );

struct PathCommand {

	enum Type : uint32_t {
		eUnknown = 0,
		eMoveTo,
		eLineTo,
		eCurveTo,
		eQuadBezierTo = eCurveTo,
		eCubicBezierTo,
		eArcTo,
		eClosePath,
	} type;

	glm::vec2 p; // end point

	union Data {
		struct AsCubicBezier {
			glm::vec2 c1; // control point 1
			glm::vec2 c2; // control point 2
		} as_cubic_bezier;
		struct AsQuadBezier {
			glm::vec2 c1; // control point 1
		} as_quad_bezier;
		struct AsArc {
			glm::vec2 radii; // control point 1
			float     phi;   // control point 2
			bool      large_arc;
			bool      sweep;
		} as_arc;
	} data;

	PathCommand( glm::vec2 const& p, Data::AsCubicBezier const& as_cubic_bezier )
	    : type( eCubicBezierTo )
	    , p( p ) {
		data.as_cubic_bezier = as_cubic_bezier;
	}

	PathCommand( glm::vec2 const& p, Data::AsQuadBezier const& as_quad_bezier )
	    : type( eQuadBezierTo )
	    , p( p ) {
		data.as_quad_bezier = as_quad_bezier;
	}

	PathCommand( glm::vec2 const& p, Data::AsArc const& as_arc )
	    : type( eArcTo )
	    , p( p ) {
		data.as_arc = as_arc;
	}

	PathCommand( Type type, glm::vec2 const& p )
	    : type( type )
	    , p( p ) {
	}
};

struct Contour {
	std::vector<PathCommand> commands; // svg-style commands+parameters creating the path
};

struct Polyline {
	std::vector<glm::vec2> vertices;
	std::vector<glm::vec2> tangents;
	std::vector<float>     distances;
	float                  total_distance = 0;
};

struct le_path_o {
	std::vector<Contour>  contours;  // an array of sub-paths, a contour must start with a moveto instruction
	std::vector<Polyline> polylines; // an array of polylines, each corresponding to a sub-path.
};

struct CubicBezier {
	glm::vec2 p0;
	glm::vec2 c1;
	glm::vec2 c2;
	glm::vec2 p1;
};

struct Line {
	glm::vec2 p0;
	glm::vec2 p1;
};

struct CurveSegment {
	enum Type : uint32_t {
		eCubicBezier = 0,
		eLine        = 1,
	} const type;
	union {
		CubicBezier asCubicBezier;
		Line        asLine;
	};
	CurveSegment( CubicBezier const& cb )
	    : type( eCubicBezier ) {
		asCubicBezier = cb;
	}
	CurveSegment( Line const& line )
	    : type( eLine ) {
		asLine = line;
	}
};

struct InflectionData {
	float t_cusp;
	float t_1;
	float t_2;
};

// Thomas Algorithm, also known as tridiagonal matrix solver algorithm,
// implemented based on video lecture by Prof. Dr. Edmund Weitz, see:
// <https://www.youtube.com/watch?v=0oUo1d6PpGU>
//
// This implementation follows the naming convention used in the Wikipedia
// entry, <https://en.m.wikipedia.org/wiki/Tridiagonal_matrix_algorithm>
// with the important difference that our arrays are zero-indexed, so as to
// follow the c/cpp convention.
//
// Note: Parameters a, b, c, d are arrays of length `count`. a[0], and c[n]
// are not used, `result` must be an array of length `count`.
//
template <typename T>
inline static void thomas( T const* a, T const* b, T const* c, T const* d, size_t const count, T* result ) {

	// We copy, so that we don't overwrite given paramter data.
	//
	std::vector<T> c_prime( count );
	std::vector<T> d_prime( count );

	size_t i           = 0;
	T      denominator = b[ i ];
	c_prime[ i ]       = c[ i ] / denominator;
	d_prime[ i ]       = d[ i ] / denominator;

	for ( i = 1; i != count; ++i ) {
		denominator  = b[ i ] - c_prime[ i - 1 ] * a[ i ];
		c_prime[ i ] = c[ i ] / denominator;
		d_prime[ i ] = ( d[ i ] - d_prime[ i - 1 ] * a[ i ] ) / denominator;
	}

	int n       = count - 1;
	result[ n ] = d_prime[ n ];

	for ( n = count - 2; n >= 0; n-- ) {
		result[ n ] = d_prime[ n ] - c_prime[ n ] * result[ n + 1 ];
	}
}

// Note that we expect value a[0] to contain the value from the
// top right corner of the "almost tridiagonal" matrix,
// and that we expect c[count-1] to contain the value from the
// bottom left corner of the "almost tridiagonal" matrix.
template <typename T>
inline static void sherman_morrisson_woodbury( T const* a, T const* b, T const* c, T const* d, size_t const count, T* result ) {

	std::vector<T> u( count, 0 );
	std::vector<T> v( count, 0 );

	u[ 0 ]         = 1;
	u[ count - 1 ] = 1;

	auto const& s = a[ 0 ];         // note: a[0] not used by thomas algorithm
	auto const& t = c[ count - 1 ]; // note: c[count-1] not used by thomas algorithm

	v[ count - 1 ] = s;
	v[ 0 ]         = t;

	std::vector<T> b_dash{ b, b + count };

	b_dash[ 0 ] -= t;
	b_dash[ count - 1 ] -= s;

	std::vector<T> Td( count );
	std::vector<T> Tu( count );

	thomas( a, b_dash.data(), c, d, count, Td.data() );
	thomas( a, b_dash.data(), c, u.data(), count, Tu.data() );

	const T factor = ( t * Td[ 0 ] +
	                   s * Td[ count - 1 ] ) /
	                 ( 1 + t * Tu[ 0 ] +
	                   s * Tu[ count - 1 ] );

	for ( size_t i = 0; i != count; i++ ) {
		result[ i ] = Td[ i ] - factor * Tu[ i ];
	}
}

// ----------------------------------------------------------------------

inline static float clamp( float val, float range_min, float range_max ) {
	return val < range_min ? range_min : val > range_max ? range_max
	                                                     : val;
}

// ----------------------------------------------------------------------

inline static float map( float val_, float range_min_, float range_max_, float min_, float max_ ) {
	return clamp( min_ + ( max_ - min_ ) * ( ( clamp( val_, range_min_, range_max_ ) - range_min_ ) / ( range_max_ - range_min_ ) ), min_, max_ );
}

// ----------------------------------------------------------------------

static inline glm::vec2 quad_bezier_derivative( float t, glm::vec2 const& p0, glm::vec2 const& c1, glm::vec2 const& p1 ) {
	float one_minus_t = ( 1 - t );
	return 2 * one_minus_t * ( c1 - p0 ) + 2 * t * ( p1 - c1 );
}

// ----------------------------------------------------------------------

// Calculate derivative of cubic bezier - that is, tangent on bezier curve at parameter t
static inline glm::vec2 cubic_bezier_derivative( float t, glm::vec2 const& p0, glm::vec2 const& c1, glm::vec2 const& c2, glm::vec2 const& p1 ) {
	float t_sq           = t * t;
	float one_minus_t    = 1 - t;
	float one_minus_t_sq = one_minus_t * one_minus_t;
	return 3 * one_minus_t_sq * ( c1 - p0 ) + 6 * one_minus_t * t * ( c2 - c1 ) + 3 * t_sq * ( p1 - c2 );
}

// ----------------------------------------------------------------------

inline static bool is_contained_0_1( float f ) {
	return ( f >= 0.f && f <= 1.f );
}

// ----------------------------------------------------------------------

static le_path_o* le_path_create() {
	auto self = new le_path_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_path_destroy( le_path_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_path_clear( le_path_o* self ) {
	self->contours.clear();
	self->polylines.clear();
}

// ----------------------------------------------------------------------

static void trace_move_to( Polyline& polyline, glm::vec2 const& p ) {
	polyline.distances.emplace_back( 0 );
	polyline.vertices.emplace_back( p );
	// NOTE: we dont insert a tangent here, as we need at least two
	// points to calculate tangents. In an open path, there will be n-1
	// tangent vectors than vertices, closed paths have same number of
	// tangent vectors as vertices.
}

// ----------------------------------------------------------------------

static void trace_line_to( Polyline& polyline, glm::vec2 const& p ) {

	// We must check if the current point is identical with previous point -
	// in which case we will not add this point.

	auto const& p0               = polyline.vertices.back();
	glm::vec2   relativeMovement = p - p0;

	// Instead of using glm::distance directly, we calculate squared distance
	// so that we can filter out any potential invalid distance calculations -
	// distance cannot be calculated with two points which are identical,
	// because this would mean a division by zero. We must therefore filter out
	// any zero distances.

	float dist2 = glm::dot( relativeMovement, relativeMovement );

	static constexpr float epsilon2 = std::numeric_limits<float>::epsilon() * std::numeric_limits<float>::epsilon();

	if ( dist2 <= epsilon2 ) {
		// Distance to previous point is too small
		// No need to add this point twice.
		return;
	}

	polyline.total_distance += sqrtf( dist2 );
	polyline.distances.emplace_back( polyline.total_distance );
	polyline.vertices.emplace_back( p );
	polyline.tangents.emplace_back( relativeMovement );
}

// ----------------------------------------------------------------------

static void trace_close_path( Polyline& polyline ) {
	// eClosePath is the same as a direct line to the very first vertex.
	trace_line_to( polyline, polyline.vertices.front() );
}

// ----------------------------------------------------------------------

// Trace a quadratic bezier curve from previous point p0 to target point p2 (p2_x,p2_y),
// controlled by control point p1 (p1_x, p1_y), in steps iterations.
static void trace_quad_bezier_to( Polyline&        polyline,
                                  glm::vec2 const& p1,        // end point
                                  glm::vec2 const& c1,        // control point
                                  size_t           resolution // number of segments
) {

	if ( resolution == 0 ) {
		// nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may draw a
		// direct line to target point and return.
		trace_line_to( polyline, p1 );
		return;
	}

	// --------| invariant: resolution > 1

	polyline.vertices.reserve( polyline.vertices.size() + resolution );
	polyline.distances.reserve( polyline.vertices.size() );

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	glm::vec2 const& p0     = polyline.vertices.back(); // copy start point
	glm::vec2        p_prev = p0;

	float delta_t = 1.f / float( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( size_t i = 1; i <= resolution; i++ ) {
		float t              = i * delta_t;
		float t_sq           = t * t;
		float one_minus_t    = ( 1.f - t );
		float one_minus_t_sq = one_minus_t * one_minus_t;

		glm::vec2 b = one_minus_t_sq * p0 + 2 * one_minus_t * t * c1 + t_sq * p1;

		polyline.total_distance += glm::distance( b, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );
		p_prev = b;
		polyline.vertices.emplace_back( b );

		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
		polyline.tangents.emplace_back( quad_bezier_derivative( t, p0, c1, p1 ) );
	}
}

// ----------------------------------------------------------------------
// Trace a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void trace_cubic_bezier_to( Polyline&        polyline,
                                   glm::vec2 const& p1,        // end point
                                   glm::vec2 const& c1,        // control point 1
                                   glm::vec2 const& c2,        // control point 2
                                   size_t           resolution // number of segments
) {
	if ( resolution == 0 ) {
		// Nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly trace to the target point and return.
		trace_line_to( polyline, p1 );
		return;
	}

	// --------| invariant: resolution > 1

	polyline.vertices.reserve( polyline.vertices.size() + resolution );

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	glm::vec2 const p0     = polyline.vertices.back(); // copy start point
	glm::vec2       p_prev = p0;

	float delta_t = 1.f / float( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( size_t i = 1; i <= resolution; i++ ) {
		float t               = i * delta_t;
		float t_sq            = t * t;
		float t_cub           = t_sq * t;
		float one_minus_t     = ( 1.f - t );
		float one_minus_t_sq  = one_minus_t * one_minus_t;
		float one_minus_t_cub = one_minus_t_sq * one_minus_t;

		glm::vec2 b = one_minus_t_cub * p0 + 3 * one_minus_t_sq * t * c1 + 3 * one_minus_t * t_sq * c2 + t_cub * p1;

		polyline.total_distance += glm::distance( b, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );
		p_prev = b;

		polyline.vertices.emplace_back( b );

		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
		polyline.tangents.emplace_back( cubic_bezier_derivative( t, p0, c1, c2, p1 ) );
	}
}

// ----------------------------------------------------------------------
// translates arc into straight polylines - while respecting tolerance.
static void trace_arc_to( Polyline&        polyline,
                          glm::vec2 const& p1, // end point
                          glm::vec2 const& radii,
                          float            phi,
                          bool             large_arc,
                          bool             sweep,
                          size_t           iterations ) {

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	// If any or both of radii.x or radii.y is 0, then we must treat the
	// arc as a straight line:
	//
	if ( fabsf( radii.x * radii.y ) <= std::numeric_limits<float>::epsilon() ) {
		trace_line_to( polyline, p1 );
		return;
	}

	// ---------| Invariant: radii.x and radii.y are not 0.

	glm::vec2 const p0 = polyline.vertices.back(); // copy start point

	// First, we perform an endpoint to centre form conversion, following the
	// implementation notes of the w3/svg standards group.
	//
	// See: <https://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter>
	//
	glm::vec2 x_axis{ cosf( phi ), sinf( phi ) };
	glm::vec2 y_axis{ -x_axis.y, x_axis.x };
	glm::mat2 basis{ x_axis, y_axis };
	glm::mat2 inv_basis = glm::transpose( basis );

	glm::vec2 x_ = basis * ( ( p0 - p1 ) / 2.f ); // "x dash"

	float x_sq = x_.x * x_.x;
	float y_sq = x_.y * x_.y;

	glm::vec2 r    = glm::vec2{ fabsf( radii.x ), fabsf( radii.y ) }; // TODO: make sure radius is large enough.
	float     rxsq = r.x * r.x;
	float     rysq = r.y * r.y;

	// Ensure radius is large enough
	//
	float lambda = x_sq / rxsq + y_sq / rysq;
	if ( lambda > 1 ) {
		float sqrt_lambda = sqrtf( lambda );
		r *= sqrt_lambda;
		rxsq = r.x * r.x;
		rysq = r.y * r.y;
	}
	// ----------| Invariant: radius is large enough

	float sqrt_sign = ( large_arc == sweep ) ? -1.f : 1.f;
	float sqrt_term = ( rxsq * rysq -
	                    rxsq * y_sq -
	                    rysq * x_sq ) /
	                  ( rxsq * y_sq +
	                    rysq * x_sq );

	glm::vec2 c_{};
	if ( ( rxsq * y_sq + rysq * x_sq ) > std::numeric_limits<float>::epsilon() ) {
		// Woah! that fabsf is not in the w3c implementation notes...
		// We need it for the special case where the sqrt_term
		// would get negative.
		c_ = sqrtf( fabsf( sqrt_term ) ) * sqrt_sign *
		     glm::vec2( ( r.x * x_.y ) / r.y, ( -r.y * x_.x ) / r.x );
	} else {
		c_ = glm::vec2{ 0 };
	}

	glm::vec2 c = inv_basis * c_ + ( ( p0 + p1 ) / 2.f );

	glm::vec2 u = glm::normalize( ( x_ - c_ ) / r );
	glm::vec2 v = glm::normalize( ( -x_ - c_ ) / r );

	// Note that it's important to take the oriented, and not just the absolute angle here.
	//
	float theta_1     = glm::orientedAngle( glm::vec2{ 1, 0 }, u );
	float theta_delta = fmodf( glm::orientedAngle( u, v ), glm::two_pi<float>() );

	// No Sweep: Angles must be decreasing
	if ( sweep == false && theta_delta > 0 ) {
		theta_delta = theta_delta - glm::two_pi<float>();
	}

	// Sweep: Angles must be increasing
	if ( sweep == true && theta_delta < 0 ) {
		theta_delta = theta_delta + glm::two_pi<float>();
	}

	if ( fabsf( theta_delta ) <= std::numeric_limits<float>::epsilon() ) {
		return;
	}

	// --------- | Invariant: delta_theta is not zero.

	float theta     = theta_1;
	float theta_end = theta_1 + theta_delta;

	glm::vec2 prev_pt = polyline.vertices.back();
	glm::vec2 n       = glm::vec2{ cosf( theta ), sinf( theta ) };

	float angle_offset = theta_delta / float( iterations );

	for ( size_t i = 0; i <= iterations; i++ ) {

		theta += angle_offset;

		n = { cosf( theta ), sinf( theta ) };

		glm::vec2 arc_pt = r * n;
		arc_pt           = inv_basis * arc_pt + c;

		polyline.vertices.push_back( arc_pt );
		polyline.total_distance += glm::distance( arc_pt, prev_pt );
		polyline.distances.push_back( polyline.total_distance );
		polyline.tangents.push_back( inv_basis * ( r * glm::vec2{ -sinf( theta ), cosf( theta ) } ) );
		prev_pt = arc_pt;

		if ( !sweep && theta <= theta_end ) {
			break;
		}
		if ( sweep && theta >= theta_end ) {
			break;
		}
	}
}

// ----------------------------------------------------------------------
// Traces the path with all its subpaths into a list of polylines.
// Each subpath will be translated into one polyline.
// A polyline is a list of vertices which may be thought of being
// connected by lines.
//
static void le_path_trace_path( le_path_o* self, size_t resolution ) {

	self->polylines.clear();
	self->polylines.reserve( self->contours.size() );

	for ( auto const& s : self->contours ) {

		Polyline polyline;

		for ( auto const& command : s.commands ) {

			switch ( command.type ) {
			case PathCommand::eMoveTo:
				trace_move_to( polyline, command.p );
				break;
			case PathCommand::eLineTo:
				trace_line_to( polyline, command.p );
				break;
			case PathCommand::eQuadBezierTo: {
				auto& bez = command.data.as_quad_bezier;
				trace_quad_bezier_to( polyline,
				                      command.p,
				                      bez.c1,
				                      resolution );
			} break;
			case PathCommand::eCubicBezierTo: {
				auto& bez = command.data.as_cubic_bezier;
				trace_cubic_bezier_to( polyline,
				                       command.p,
				                       bez.c1,
				                       bez.c2,
				                       resolution );
			} break;
			case PathCommand::eArcTo: {
				auto& arc = command.data.as_arc;
				trace_arc_to( polyline,
				              command.p,
				              arc.radii,
				              arc.phi,
				              arc.large_arc,
				              arc.sweep,
				              resolution );
			} break;
			case PathCommand::eClosePath:
				trace_close_path( polyline );
				break;
			case PathCommand::eUnknown:
				assert( false );
				break;
			}
		}

		assert( polyline.vertices.size() == polyline.distances.size() );

		self->polylines.emplace_back( polyline );
	}
}

// Subdivides given cubic bezier curve `b` at position `t`
// into two cubic bezier curves, `s_0`, and `s_1`
static void bezier_subdivide( CubicBezier const& b, float t, CubicBezier* s_0, CubicBezier* s_1 ) {

	auto const b0    = b.p0;
	auto const b2_   = b.c2 + t * ( b.p1 - b.c2 );
	auto const b0_   = b.p0 + t * ( b.c1 - b.p0 );
	auto const b1_   = b.c1 + t * ( b.c2 - b.c1 );
	auto const b0__  = b0_ + t * ( b1_ - b0_ );
	auto const b1__  = b1_ + t * ( b2_ - b1_ );
	auto const b0___ = b0__ + t * ( b1__ - b0__ );
	auto const b3    = b.p1;

	if ( s_0 ) {
		s_0->p0 = b0;
		s_0->c1 = b0_;
		s_0->c2 = b0__;
		s_0->p1 = b0___;
	}
	if ( s_1 ) {
		s_1->p0 = b0___;
		s_1->c1 = b1__;
		s_1->c2 = b2_;
		s_1->p1 = b3;
	}
}

// Calculate inflection points for cubic bezier curve, expressed in
// parameter t value at inflection point. Cubic bezier curves may
// Have 0 or 2 inflection points. Inflection points may be negative,
// in which case they don't appear on the curve.
//
// The mathematics for this method have been verified using mathematica.
static bool cubic_bezier_calculate_inflection_points( CubicBezier const& b, InflectionData* infl ) {

	// clang-format off
	glm::vec2 const a_ =       -b.p0 + 3.f * b.c1 - 3.f * b.c2 + b.p1;
	glm::vec2 const b_ =  3.f * b.p0 - 6.f * b.c1 + 3.f * b.c2;
	glm::vec2 const c_ = -3.f * b.p0 + 3.f * b.c1;
	// clang-format on

	float const divisor = 12 * ( -a_.y * b_.x + a_.x * b_.y );

	if ( fabsf( divisor ) <= std::numeric_limits<float>::epsilon() ) {
		// must not be zero, otherwise there are no solutions.
		return false;
	}

	float t_cusp = 6 * a_.y * c_.x - 6 * a_.x * c_.y;

	infl->t_cusp = t_cusp / divisor;

	float sq_term = t_cusp * t_cusp -
	                4 *
	                    ( 6 * a_.y * b_.x - 6 * a_.x * b_.y ) *
	                    ( 2 * b_.y * c_.x - 2 * b_.x * c_.y );

	if ( sq_term < 0 ) {
		// must be > 0 otherswise, no solutions.
		infl->t_1 = 0;
		infl->t_2 = 0;
		return false;
	}

	infl->t_1 = ( t_cusp - sqrtf( sq_term ) ) / divisor;
	infl->t_2 = ( t_cusp + sqrtf( sq_term ) ) / divisor;

	return true;
}

// ----------------------------------------------------------------------
// Split a cubic bezier curve into a list of monotonous segments, so that
// none of the segments contains a cusp or inflection point within its 0..1
// parameter range.
//
// Each subsegment will itself be a cubic bezier curve.
//
// Tolerance tells us how close to follow original curve
// when interpolating the curve as a list of straight line segments.
static void split_cubic_bezier_into_monotonous_sub_segments( CubicBezier& b, std::vector<CurveSegment>& curves, float tolerance ) {
	// --- calculate inflection points:

	InflectionData infl;
	bool           has_inflection_points = cubic_bezier_calculate_inflection_points( b, &infl );

	CubicBezier b_0; // placeholder
	CubicBezier b_1; // placeholder

	if ( !has_inflection_points ) {
		if ( is_contained_0_1( infl.t_cusp ) ) {
			bezier_subdivide( b, infl.t_cusp, &b_0, &b_1 );
			curves.push_back( b_0 );
			curves.push_back( b_1 );
		} else {

			curves.push_back( b ); // curve is already monotonous - no need to do anything further.
		}
		return;
	}

	// ----------| Invariant: this curve contains inflection points.

	float boundaries[ 4 ]{};

	float& t1_m = boundaries[ 0 ]; // t1 minus delta
	float& t1_p = boundaries[ 1 ]; // t1 plus  delta
	float& t2_m = boundaries[ 2 ]; // t2 minus delta
	float& t2_p = boundaries[ 3 ]; // t2 plus  delta

	auto calc_inflection_point_offsets = []( CubicBezier const& b, float tolerance, float infl, float* infl_m, float* infl_p ) {
		CubicBezier b_sub{};
		bezier_subdivide( b, infl, nullptr, &b_sub );

		glm::vec2 r;

		if ( b_sub.c1 == b_sub.p0 ) {
			// We must handle special case in which c1 == p0: this means coordinate basis must be built as if we went to c2
			r = glm::normalize( b_sub.c2 - b_sub.p0 );
		} else {
			r = glm::normalize( b_sub.c1 - b_sub.p0 );
		}

		glm::vec2 s = { r.y, -r.x };

		// Define a coordinate basis built on the first two points, b0, and b1
		glm::mat2 const basis = { r, s };

		float s3 = 3 * fabsf( ( basis * ( b_sub.p1 - b_sub.p0 ) ).y );

		if ( s3 <= std::numeric_limits<float>::epsilon() ) {
			*infl_m = infl;
			*infl_p = infl;

			return;
		}

		float t_f = powf( tolerance / s3, 1.f / 3.f ); // cubic root

		*infl_m = infl - t_f * ( 1 - infl );
		*infl_p = infl + t_f * ( 1 - infl );
	};

	calc_inflection_point_offsets( b, tolerance, infl.t_1, &t1_m, &t1_p );
	calc_inflection_point_offsets( b, tolerance, infl.t_2, &t2_m, &t2_p );

	// It's possible that our bezier curve self-intersects,
	// in which case inflection points are out of order -
	// then we must sort them.

	bool curve_has_knot = t2_m <= t1_p || ( t1_p >= t2_m );

	if ( curve_has_knot && ( infl.t_1 >= infl.t_2 ) ) {
		std::sort( boundaries, boundaries + 4 );
		std::swap( infl.t_1, infl.t_2 );
	}

	{

		// ----------| invariant: curve does not have a cusp.

		auto which_region = []( float* boundaries, size_t num_boundaries, float marker ) -> size_t {
			size_t i = 0;
			for ( ; i != num_boundaries; i++ ) {
				if ( boundaries[ i ] > marker ) {
					return i;
				}
			}
			return i;
		};

		// Calculate into which of the 5 segments of an infinite cubic bezier
		// the given start and end points (based on t = 0..1 ) fall:
		//
		// ---0--- t1_m ---1--- t1_p ---2--- t2_m ---3--- t2_p ---4---
		//
		size_t c_start = which_region( boundaries, 4, 0.f );
		size_t c_end   = which_region( boundaries, 4, 1.f );

		if ( c_start == c_end ) {

			// Curve contained within a single segment.

			// Note segments 1, and 3 are flat, as such they
			// are better represented as straight lines.

			if ( c_start == 1 || c_start == 3 ) {
				CurveSegment line{ Line() };
				line.asLine.p0 = b.p0;
				line.asLine.p1 = b.p1;
				curves.push_back( line );
			} else {
				curves.push_back( b );
			}

		} else {

			// Curve spans multiple segments

			if ( c_start == 0 ) {
				// curve starts within first segment, but does not end here.
				// this means the first segment of the curve will be 0..t1m
				bezier_subdivide( b, t1_m, &b_0, nullptr );
				curves.push_back( b_0 );
			}

			if ( c_start == 1 ) {
				// curve starts between t1_m and t1_p which means that this
				// segment, the segment before t1_p can be approximated by a
				// straight line.

				bezier_subdivide( b, t1_p, &b_0, nullptr );

				CurveSegment line{ Line() };
				line.asLine.p0 = b_0.p0;
				line.asLine.p1 = b_0.p1;

				curves.push_back( line );
			}

			if ( c_end == 1 ) {
				// curve ends within the 1st segment, but does not start here.
				// this means that from t1m to end the curve can be approximated
				// by a straight line.

				bezier_subdivide( b, t1_m, nullptr, &b_0 );

				CurveSegment line{ Line() };
				line.asLine.p0 = b_0.p0;
				line.asLine.p1 = b_0.p1;

				curves.push_back( line );
			}

			if ( c_end == 2 ) {
				// curve ends within the 2nd segment, but does not start here.
				// this means that the next segment of the curve will be limited
				// by t1_p .. 1
				bezier_subdivide( b, t1_p, nullptr, &b_0 );
				curves.push_back( b_0 );
			}

			if ( c_start < 2 && c_end > 2 ) {
				// curve goes over segment 2, we need to get segment t1_p .. t2..m
				bezier_subdivide( b, t1_p, nullptr, &b_1 );  // part t1_p .. 1
				float t3 = map( t2_m, t1_p, 1.f, 0.f, 1.f ); // t2_m expressed in t1_p .. 1 space
				bezier_subdivide( b_1, t3, &b_0, nullptr );  // part t1_p .. t2_m
				curves.push_back( b_0 );
			}

			if ( c_start == 2 ) {
				bezier_subdivide( b, t2_m, &b_0, nullptr );
				curves.push_back( b_0 );
			}

			if ( c_start == 3 ) {
				bezier_subdivide( b, t2_p, &b_0, nullptr );

				CurveSegment line{ Line() };
				line.asLine.p0 = b.p0;
				line.asLine.p1 = b_0.p1;

				curves.push_back( line );
			}

			if ( c_end == 3 ) {
				bezier_subdivide( b, t2_m, nullptr, &b_0 );
				CurveSegment line{ Line() };
				line.asLine.p0 = b_0.p0;
				line.asLine.p1 = b_0.p1;

				curves.push_back( line );
			}

			if ( c_end == 4 ) {
				bezier_subdivide( b, t2_p, nullptr, &b_0 );
				curves.push_back( b_0 );
			}
		}
	}
}

// ----------------------------------------------------------------------

static void flatten_cubic_bezier_segment_to( Polyline&          polyline,
                                             CubicBezier const& b_,
                                             float              tolerance ) {

	CubicBezier b = b_; // fixme: not necessary.

	float t = 0;

	glm::vec2 p_prev = b.p0;

	// Note that we limit the number of iterations by setting a maximum of 1000 - this
	// should only ever be reached when tolerance is super small.
	for ( int i = 0; i != 1000; i++ ) {

		// create a coordinate basis based on the first point, and the first control point
		glm::vec2 r = glm::normalize( b.c1 - b.p0 );
		glm::vec2 s = { r.y, -r.x };

		// Define a coordinate basis built on the first two points, b0, and b1
		glm::mat2 const basis = { r, s };

		glm::vec2 P2 = basis * ( b.c2 - b.p0 );

		float s2 = ( P2 ).y;

		float t_dash = sqrtf( tolerance / ( 3 * fabsf( s2 ) ) );
		t            = std::min<float>( 1.f, t_dash );

		// Apply subdivision at (t). This means that the start point of the sub-segment
		// will be the point we can add to the polyline while respecting flatness.
		bezier_subdivide( b, t, nullptr, &b );

		glm::vec2 pt = b.p0;

		polyline.vertices.emplace_back( pt );
		polyline.total_distance += glm::distance( pt, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );

		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
		polyline.tangents.emplace_back( cubic_bezier_derivative( t, b.p0, b.c1, b.c2, b.p1 ) );

		if ( t >= 1.0f )
			break;

		p_prev = pt;
	}
}

// ----------------------------------------------------------------------
// Flatten a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void flatten_cubic_bezier_to( Polyline&        polyline,
                                     glm::vec2 const& p1,       // end point
                                     glm::vec2 const& c1,       // control point 1
                                     glm::vec2 const& c2,       // control point 2
                                     float            tolerance // max distance for arc segment
) {

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	glm::vec2 const p0 = polyline.vertices.back(); // copy start point

	CubicBezier b{
	    p0,
	    c1,
	    c2,
	    p1,
	};

	std::vector<CurveSegment> segments;

	split_cubic_bezier_into_monotonous_sub_segments( b, segments, tolerance );

	// ---
	for ( auto& s : segments ) {
		switch ( s.type ) {
		case ( CurveSegment::Type::eCubicBezier ):
			flatten_cubic_bezier_segment_to( polyline, s.asCubicBezier, tolerance );
			break;
		case ( CurveSegment::Type::eLine ):
			trace_line_to( polyline, s.asLine.p1 );
			break;
		}
	}
}

// ----------------------------------------------------------------------
// translates arc into straight polylines - while respecting tolerance.
static glm::vec2 get_arc_tangent_at_normalised_t( glm::vec2 const& p0, // end point
                                                  glm::vec2 const& p1, // end point
                                                  glm::vec2 const& radii,
                                                  float            phi,
                                                  bool             large_arc,
                                                  bool             sweep,
                                                  float            t // [0..1] normalised over [theta..theta+delta_theta]
) {

	// If any or both of radii.x or radii.y is 0, then we must treat the
	// arc as a straight line:
	//
	if ( fabsf( radii.x * radii.y ) <= std::numeric_limits<float>::epsilon() ) {
		return glm::normalize( p1 - p0 );
	}

	// ---------| Invariant: radii.x and radii.y are not 0.

	// First, we perform an endpoint to centre form conversion, following the
	// implementation notes of the w3/svg standards group.
	//
	// See: <https://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter>
	//
	glm::vec2 x_axis{ cosf( phi ), sinf( phi ) };
	glm::vec2 y_axis{ -x_axis.y, x_axis.x };
	glm::mat2 basis{ x_axis, y_axis };
	glm::mat2 inv_basis = glm::transpose( basis );

	glm::vec2 x_ = basis * ( ( p0 - p1 ) / 2.f ); // "x dash"

	float x_sq = x_.x * x_.x;
	float y_sq = x_.y * x_.y;

	glm::vec2 r    = glm::vec2{ fabsf( radii.x ), fabsf( radii.y ) }; // TODO: make sure radius is large enough.
	float     rxsq = r.x * r.x;
	float     rysq = r.y * r.y;

	// Ensure radius is large enough
	//
	float lambda = x_sq / rxsq + y_sq / rysq;
	if ( lambda > 1 ) {
		float sqrt_lambda = sqrtf( lambda );
		r *= sqrt_lambda;
		rxsq = r.x * r.x;
		rysq = r.y * r.y;
	}
	// ----------| Invariant: radius is large enough

	float sqrt_sign = ( large_arc == sweep ) ? -1.f : 1.f;
	float sqrt_term = ( rxsq * rysq -
	                    rxsq * y_sq -
	                    rysq * x_sq ) /
	                  ( rxsq * y_sq +
	                    rysq * x_sq );

	glm::vec2 c_{};
	if ( ( rxsq * y_sq + rysq * x_sq ) > std::numeric_limits<float>::epsilon() ) {
		// Woah! that fabsf is not in the w3c implementation notes...
		// We need it for the special case where the sqrt_term
		// would get negative.
		c_ = sqrtf( fabsf( sqrt_term ) ) * sqrt_sign *
		     glm::vec2( ( r.x * x_.y ) / r.y, ( -r.y * x_.x ) / r.x );
	} else {
		c_ = glm::vec2{ 0 };
	}

	glm::vec2 u = glm::normalize( ( x_ - c_ ) / r );
	glm::vec2 v = glm::normalize( ( -x_ - c_ ) / r );

	// Note that it's important to take the oriented, and not just the absolute angle here.
	//
	float theta_1     = glm::orientedAngle( glm::vec2{ 1, 0 }, u );
	float theta_delta = fmodf( glm::orientedAngle( u, v ), glm::two_pi<float>() );

	// No Sweep: Angles must be decreasing
	if ( sweep == false && theta_delta > 0 ) {
		theta_delta = theta_delta - glm::two_pi<float>();
	}

	// Sweep: Angles must be increasing
	if ( sweep == true && theta_delta < 0 ) {
		theta_delta = theta_delta + glm::two_pi<float>();
	}

	// --------- | Invariant: delta_theta is not zero.

	float     theta   = theta_1 + theta_delta * t;
	glm::vec2 tangent = inv_basis * ( r * glm::vec2{ -sinf( theta ), cosf( theta ) } );

	if ( sweep ) {
		return tangent;
	} else {
		return -tangent;
	}
}

// ----------------------------------------------------------------------
// translates arc into straight polylines - while respecting tolerance.
//
// FIXME: There is potentially still a bug in this - look how the following
// SVG string evaluates:
// "M 300 450 L 350 425 A 25 25 -30 0 1 400 400 L 450 375 A 25 50 -30 0 1 500 350 L 550 325 A 25 75 -30 0 1 600 300 L 650 275 A 25 100 -30 0 1 700 250 L 750 225"
//
// The string is taken from the svg spec:
// https://svgwg.org/svg2-draft/paths.html#PathDataEllipticalArcCommands
// The ellipses should face in the same direction:
static void flatten_arc_to( Polyline&        polyline,
                            glm::vec2 const& p1, // end point
                            glm::vec2 const& radii,
                            float            phi,
                            bool             large_arc,
                            bool             sweep,
                            float            tolerance ) {

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	// If any or both of radii.x or radii.y is 0, then we must treat the
	// arc as a straight line:
	//
	if ( fabsf( radii.x * radii.y ) <= std::numeric_limits<float>::epsilon() ) {
		trace_line_to( polyline, p1 );
		return;
	}

	// ---------| Invariant: radii.x and radii.y are not 0.

	glm::vec2 const p0 = polyline.vertices.back(); // copy start point

	// First, we perform an endpoint to centre form conversion, following the
	// implementation notes of the w3/svg standards group.
	//
	// See: <https://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter>
	//
	glm::vec2 x_axis{ cosf( phi ), sinf( phi ) };
	glm::vec2 y_axis{ -x_axis.y, x_axis.x };
	glm::mat2 basis{ x_axis, y_axis };
	glm::mat2 inv_basis = glm::transpose( basis );

	glm::vec2 x_ = basis * ( ( p0 - p1 ) / 2.f ); // "x dash"

	float x_sq = x_.x * x_.x;
	float y_sq = x_.y * x_.y;

	glm::vec2 r    = glm::vec2{ fabsf( radii.x ), fabsf( radii.y ) }; // TODO: make sure radius is large enough.
	float     rxsq = r.x * r.x;
	float     rysq = r.y * r.y;

	// Ensure radius is large enough
	//
	float lambda = x_sq / rxsq + y_sq / rysq;
	if ( lambda > 1 ) {
		float sqrt_lambda = sqrtf( lambda );
		r *= sqrt_lambda;
		rxsq = r.x * r.x;
		rysq = r.y * r.y;
	}
	// ----------| Invariant: radius is large enough

	float sqrt_sign = ( large_arc == sweep ) ? -1.f : 1.f;
	float sqrt_term = ( rxsq * rysq -
	                    rxsq * y_sq -
	                    rysq * x_sq ) /
	                  ( rxsq * y_sq +
	                    rysq * x_sq );

	glm::vec2 c_{};
	if ( ( rxsq * y_sq + rysq * x_sq ) > std::numeric_limits<float>::epsilon() ) {
		// Woah! that fabsf is not in the w3c implementation notes...
		// We need it for the special case where the sqrt_term
		// would get negative.
		c_ = sqrtf( fabsf( sqrt_term ) ) * sqrt_sign *
		     glm::vec2( ( r.x * x_.y ) / r.y, ( -r.y * x_.x ) / r.x );
	} else {
		c_ = glm::vec2{ 0 };
	}

	glm::vec2 c = inv_basis * c_ + ( ( p0 + p1 ) / 2.f );

	glm::vec2 u = glm::normalize( ( x_ - c_ ) / r );
	glm::vec2 v = glm::normalize( ( -x_ - c_ ) / r );

	// Note that it's important to take the oriented, and not just the absolute angle here.
	//
	float theta_1     = glm::orientedAngle( glm::vec2{ 1, 0 }, u );
	float theta_delta = fmodf( glm::orientedAngle( u, v ), glm::two_pi<float>() );

	// No Sweep: Angles must be decreasing
	if ( sweep == false && theta_delta > 0 ) {
		theta_delta = theta_delta - glm::two_pi<float>();
	}

	// Sweep: Angles must be increasing
	if ( sweep == true && theta_delta < 0 ) {
		theta_delta = theta_delta + glm::two_pi<float>();
	}

	if ( fabsf( theta_delta ) <= std::numeric_limits<float>::epsilon() ) {
		return;
	}

	// --------- | Invariant: delta_theta is not zero.

	float theta     = theta_1;
	float theta_end = theta_1 + theta_delta;

	glm::vec2 prev_pt = polyline.vertices.back();
	glm::vec2 n       = glm::vec2{ cosf( theta ), sinf( theta ) };

	// We are much more likely to break ealier - but we add a counter as an upper bound
	// to this loop to minimise getting trapped in an endless loop in case of some NaN
	// mishap.
	//
	for ( size_t i = 0; i <= 1000; i++ ) {

		float r_length = glm::dot( glm::vec2{ fabsf( n.x ), fabsf( n.y ) }, glm::abs( inv_basis * radii ) );

		float angle_offset = acosf( 1 - ( tolerance / r_length ) );

		if ( !sweep ) {
			theta = std::max( theta - angle_offset, theta_end );
		} else {
			theta = std::min( theta + angle_offset, theta_end );
		}

		n = { cosf( theta ), sinf( theta ) };

		glm::vec2 arc_pt = r * n;
		arc_pt           = inv_basis * arc_pt + c;

		polyline.vertices.push_back( arc_pt );
		polyline.total_distance += glm::distance( arc_pt, prev_pt );
		polyline.distances.push_back( polyline.total_distance );
		polyline.tangents.push_back( inv_basis * ( r * glm::vec2{ -sinf( theta ), cosf( theta ) } ) );
		prev_pt = arc_pt;

		if ( !sweep && theta <= theta_end ) {
			break;
		}
		if ( sweep && theta >= theta_end ) {
			break;
		}
	}
}

// ----------------------------------------------------------------------

static void le_path_flatten_path( le_path_o* self, float tolerance ) {

	self->polylines.clear();
	self->polylines.reserve( self->contours.size() );

	for ( auto const& s : self->contours ) {

		Polyline polyline;

		glm::vec2 prev_point = {};

		for ( auto const& command : s.commands ) {

			switch ( command.type ) {
			case PathCommand::eMoveTo:
				trace_move_to( polyline, command.p );
				prev_point = command.p;
				break;
			case PathCommand::eLineTo:
				trace_line_to( polyline, command.p );
				prev_point = command.p;
				break;
			case PathCommand::eQuadBezierTo: {
				auto& bez = command.data.as_quad_bezier;
				flatten_cubic_bezier_to( polyline,
				                         command.p,
				                         prev_point + 2 / 3.f * ( bez.c1 - prev_point ),
				                         command.p + 2 / 3.f * ( bez.c1 - command.p ),
				                         tolerance );
				prev_point = command.p;
			} break;
			case PathCommand::eCubicBezierTo: {
				auto& bez = command.data.as_cubic_bezier;
				flatten_cubic_bezier_to( polyline,
				                         command.p,
				                         bez.c1,
				                         bez.c2,
				                         tolerance );
				prev_point = command.p;
			} break;
			case PathCommand::eArcTo: {
				auto& arc = command.data.as_arc;
				flatten_arc_to( polyline, command.p, arc.radii, arc.phi, arc.large_arc, arc.sweep, tolerance );
				prev_point = command.p;
			} break;
			case PathCommand::eClosePath:
				trace_close_path( polyline );
				break;
			case PathCommand::eUnknown:
				assert( false );
				break;
			}
		}

		assert( polyline.vertices.size() == polyline.distances.size() );

		self->polylines.emplace_back( polyline );
	}
}

// ----------------------------------------------------------------------

static void generate_offset_outline_line_to( std::vector<glm::vec2>& outline, glm::vec2 const& p0, glm::vec2 const& p1, float offset ) {

	if ( p1 == p0 ) {
		return;
	}

	glm::vec2 r = glm::normalize( p1 - p0 );
	glm::vec2 s = { r.y, -r.x };

	outline.push_back( p0 + offset * s );

	auto p = p1 + offset * s;

	outline.push_back( p );
}

// ----------------------------------------------------------------------

static void generate_offset_outline_cubic_bezier_segment_to( std::vector<glm::vec2>& outline,
                                                             CubicBezier const&      b_,
                                                             float                   tolerance,
                                                             float                   offset ) {

	CubicBezier b = b_;

	float determinant = dot( b.p1, { -b.p0.y, b.p0.x } );
	if ( fabsf( determinant ) <= std::numeric_limits<float>::epsilon() ) {
		// start point equals end point - we must not consider this curve segment.
		return;
	}

	float t = 0;

	// Prepare for a coordinate basis based on the first point, and the first control point
	glm::vec2 r;

	if ( b.p0 != b.c1 ) {
		r = glm::normalize( b.c1 - b.p0 );
	} else {
		r = glm::normalize( b.c2 - b.p0 );
	}

	glm::vec2 s = { r.y, -r.x };

	glm::vec2 pt = b.p0 + offset * s;

	outline.emplace_back( pt );

	for ( int i = 0; i < 1000; i++ ) {

		// Define a coordinate basis built on the first two points, b0, and b1
		glm::mat2 const basis = { r, s };

		glm::vec2 P1 = basis * ( ( b.p0 != b.c1 ) ? ( b.c1 - b.p0 ) : ( b.c2 - b.p0 ) );
		glm::vec2 P2 = basis * ( ( b.p0 != b.c1 ) ? ( b.c2 - b.p0 ) : ( b.p1 - b.p0 ) );

		float s2 = P2.y;
		float r1 = P1.x;

		// IMPORTANT:
		// Since we're taking the absolute value of s2 we can't be sure whether s2 was
		// positive or negative. We want to pick the smallest absolute t_dash value,
		// which is why we calculate twice, once for positive, and once for negative s2
		// and then pick the smallest absolute value.

		float x          = ( 1 - offset * s2 / ( 3 * r1 * r1 ) );
		float x_neg      = ( 1 - offset * -s2 / ( 3 * r1 * r1 ) );
		float t_dash     = sqrtf( tolerance / fabsf( 3 * s2 * x ) );
		float t_dash_neg = sqrtf( tolerance / fabsf( 3 * s2 * x_neg ) );

		t_dash = std::min<float>( fabsf( t_dash ), fabsf( t_dash_neg ) );

		// Limit t_dash to 0..1 range
		t = std::min<float>( 1, t_dash );

		// Apply subdivision at (t). This means that the start point of the sub-segment
		// will be the point we can add to the polyline while respecting flatness.
		bezier_subdivide( b, t, nullptr, &b );

		// Update the coordinate basis based on the first point, and the first control point
		// if this is possible.

		if ( t < 1.f ) {

			r = glm::normalize( b.c1 - b.p0 );
			s = { r.y, -r.x };

			pt = b.p0 + offset * s;

			if ( x > 0 ) {
				outline.emplace_back( pt );
			}

		} else {
			break;
		}
	}

	// Add a last point at exact position of tangent - we have to
	// calculate the last point differently as with the subdividing
	// method above we cannot calculate the tangent when t == 1.f.

	// We can, however, calculate the precise tangent of the original
	// bezier curve by taking the derivative of the curve at t == 1.f.

	// -- Add last point:

	// Last point sits at Bezier control parameter `t` == 1.f,
	// at distance `offset` orthogonal to Bezier tangent at this
	// point.
	//
	// Tangent is derivative of Bezier curve at `t` == 1.f.
	glm::vec2 tangent = cubic_bezier_derivative( 1.f, b_.p0, b_.c1, b_.c2, b_.p1 );

	pt = b_.p1 - offset * glm::normalize( glm::vec2{ -tangent.y, tangent.x } );
	outline.emplace_back( pt );
}

// ----------------------------------------------------------------------

static void generate_offset_outline_line_to( std::vector<glm::vec2>& vertices_l,
                                             std::vector<glm::vec2>& vertices_r,
                                             glm::vec2 const&        p0,
                                             glm::vec2 const&        p1,
                                             float                   line_weight ) {

	if ( glm::isNull( p1 - p0, 0.001f ) ) {
		// If target point is too close to current point, we bail out.
		return;
	}

	glm::vec2 t = glm::normalize( p1 - p0 ); // tangent == current line direction
	glm::vec2 n = glm::vec2{ -t.y, t.x };    // normal onto current line

	vertices_l.push_back( p0 + n * line_weight * -0.5f );
	vertices_r.push_back( p0 + n * line_weight * 0.5f );

	vertices_l.push_back( p1 + n * line_weight * -0.5f );
	vertices_r.push_back( p1 + n * line_weight * 0.5f );
};

// ----------------------------------------------------------------------

static void generate_offset_outline_cubic_bezier_to( std::vector<glm::vec2>& outline_l,
                                                     std::vector<glm::vec2>& outline_r,
                                                     glm::vec2 const&        p0, // start point
                                                     glm::vec2 const&        c1, // control point 1
                                                     glm::vec2 const&        c2, // control point 2
                                                     glm::vec2 const&        p1, // end point
                                                     float                   tolerance,
                                                     float                   line_weight ) {

	CubicBezier b{
	    p0,
	    c1,
	    c2,
	    p1,
	};

	std::vector<CurveSegment> curve_segments;

	split_cubic_bezier_into_monotonous_sub_segments( b, curve_segments, tolerance );

	// ---
	for ( auto& s : curve_segments ) {

		switch ( s.type ) {
		case ( CurveSegment::Type::eCubicBezier ):
			generate_offset_outline_cubic_bezier_segment_to( outline_l, s.asCubicBezier, tolerance, -line_weight * 0.5f );
			generate_offset_outline_cubic_bezier_segment_to( outline_r, s.asCubicBezier, tolerance, line_weight * 0.5f );
			break;
		case ( CurveSegment::Type::eLine ):
			generate_offset_outline_line_to( outline_l, s.asLine.p0, s.asLine.p1, -line_weight * 0.5f );
			generate_offset_outline_line_to( outline_r, s.asLine.p0, s.asLine.p1, line_weight * 0.5f );
			break;
		}
	}
}

// ----------------------------------------------------------------------
// translates arc into straight polylines - while respecting tolerance.
static void generate_offset_outline_arc_to( std::vector<glm::vec2>& outline_l,
                                            std::vector<glm::vec2>& outline_r,
                                            glm::vec2 const&        p0, // start point
                                            glm::vec2 const&        p1, // end point
                                            glm::vec2 const&        radii_,
                                            float                   phi,
                                            bool                    large_arc,
                                            bool                    sweep,
                                            float                   tolerance,
                                            float                   line_weight ) {

	// If any or both of radii.x or radii.y is 0, then we must treat the
	// arc as a straight line:
	//
	if ( fabsf( radii_.x * radii_.y ) <= std::numeric_limits<float>::epsilon() ) {
		generate_offset_outline_line_to( outline_l, p0, p1, -line_weight * 0.5f );
		generate_offset_outline_line_to( outline_r, p0, p1, +line_weight * 0.5f );
		return;
	}

	// ---------| Invariant: radii.x and radii.y are not 0.

	// First, we perform an endpoint to centre form conversion, following the
	// implementation notes of the w3/svg standards group.
	//
	// See: <https://www.w3.org/TR/SVG/implnote.html#ArcConversionEndpointToCenter>
	//
	glm::vec2 x_axis{ cosf( phi ), sinf( phi ) };
	glm::vec2 y_axis{ -x_axis.y, x_axis.x };
	glm::mat2 basis{ x_axis, y_axis };
	glm::mat2 inv_basis = glm::transpose( basis );

	glm::vec2 x_ = basis * ( ( p0 - p1 ) / 2.f ); // "x dash"

	float x_sq = x_.x * x_.x;
	float y_sq = x_.y * x_.y;

	glm::vec2 r    = glm::vec2{ fabsf( radii_.x ), fabsf( radii_.y ) }; // TODO: make sure radius is large enough.
	float     rxsq = r.x * r.x;
	float     rysq = r.y * r.y;

	// Ensure radius is large enough
	//
	float lambda = x_sq / rxsq + y_sq / rysq;
	if ( lambda > 1 ) {
		float sqrt_lambda = sqrtf( lambda );
		r *= sqrt_lambda;
		rxsq = r.x * r.x;
		rysq = r.y * r.y;
	}
	// ----------| Invariant: radius is large enough

	float sqrt_sign = ( large_arc == sweep ) ? -1.f : 1.f;
	float sqrt_term = ( rxsq * rysq -
	                    rxsq * y_sq -
	                    rysq * x_sq ) /
	                  ( rxsq * y_sq +
	                    rysq * x_sq );

	glm::vec2 c_{};
	if ( ( rxsq * y_sq + rysq * x_sq ) > std::numeric_limits<float>::epsilon() ) {
		// Woah! that fabsf is not in the w3c implementation notes...
		// We need it for the special case where the sqrt_term
		// would get negative.
		c_ = sqrtf( fabsf( sqrt_term ) ) * sqrt_sign *
		     glm::vec2( ( r.x * x_.y ) / r.y, ( -r.y * x_.x ) / r.x );
	} else {
		c_ = glm::vec2{ 0 };
	}

	glm::vec2 c = inv_basis * c_ + ( ( p0 + p1 ) / 2.f );

	glm::vec2 u = glm::normalize( ( x_ - c_ ) / r );
	glm::vec2 v = glm::normalize( ( -x_ - c_ ) / r );

	// Note that it's important to take the oriented, and not just the absolute angle here.
	//
	float theta_1     = glm::orientedAngle( glm::vec2{ 1, 0 }, u );
	float theta_delta = fmodf( glm::orientedAngle( u, v ), glm::two_pi<float>() );

	// No Sweep: Angles must be decreasing
	if ( sweep == false && theta_delta > 0 ) {
		theta_delta = theta_delta - glm::two_pi<float>();
	}

	// Sweep: Angles must be increasing
	if ( sweep == true && theta_delta < 0 ) {
		theta_delta = theta_delta + glm::two_pi<float>();
	}

	if ( fabsf( theta_delta ) <= std::numeric_limits<float>::epsilon() ) {
		return;
	}

	// --------- | Invariant: delta_theta is not zero.

	float theta     = theta_1;
	float theta_end = theta_1 + theta_delta;

	glm::vec2 n = glm::vec2{ cosf( theta ), sinf( theta ) };

	float const offset  = line_weight * 0.5f;
	glm::vec2   p1_perp = glm::normalize( glm::vec2{ r.y, r.x } * n );

	glm::vec2 p_far  = c + inv_basis * ( n * r - p1_perp * offset );
	glm::vec2 p_near = c + inv_basis * ( n * r + p1_perp * offset );

	outline_l.push_back( p_near );
	outline_r.push_back( p_far );

	// We are much more likely to break ealier - but we add a counter as an upper bound
	// to this loop to minimise getting trapped in an endless loop in case of some NaN
	// mishap.
	//
	for ( size_t i = 0; i <= 1000; i++ ) {

		// Note: The calculation for `r_length`, and `angle_offset` is based on flatness
		// calculation formula for a circle, and some mathematical intuition.
		// It is, in short, not proven to be correct.
		//
		float r_length = glm::dot( glm::vec2{ fabsf( n.x ), fabsf( n.y ) }, glm::abs( r ) + glm::abs( p1_perp * offset ) );

		float angle_offset = acosf( 1 - ( tolerance / r_length ) );

		if ( !sweep ) {
			theta = std::max( theta - angle_offset, theta_end );
		} else {
			theta = std::min( theta + angle_offset, theta_end );
		}

		n = { cosf( theta ), sinf( theta ) };

		glm::vec2 arc_pt = r * n;
		arc_pt           = inv_basis * arc_pt + c;

		// p1_perp is a normalized vector which is perpendicular to the tangent
		// of the ellipse at point p1.
		//
		// The tangent is the first derivative of the ellipse in parametric notation:
		//
		// e(t) : {r.x * cos(t), r.y * sin(t)}
		// e(t'): {r.x * -sin(t), r.y * cos(t)} // tangent is first derivative
		//
		// now rotate this 90 deg ccw:
		//
		// {-r.y*cos(t), r.x*-sin(t)} // we can invert sign to remove negative if we want
		//
		// `offset` is how far we want to move outwards/inwards at the ellipse point p1,
		// in direction p1_perp. So that p1_perp has unit length, we must normalize it.
		//
		p1_perp = glm::normalize( glm::vec2{ r.y, r.x } * n );

		p_far  = c + inv_basis * ( n * r - p1_perp * offset );
		p_near = c + inv_basis * ( n * r + p1_perp * offset );

		outline_l.push_back( p_near );
		outline_r.push_back( p_far );

		if ( !sweep && theta <= theta_end ) {
			break;
		}
		if ( sweep && theta >= theta_end ) {
			break;
		}
	}
}

// ----------------------------------------------------------------------

// Generate vertices for path outline by flattening first left, then right
// offset outline. Offsetting cubic bezier curves is based on the T. F. Hain
// paper from 2005:
// "Fast, Precise Flattening of Cubic Bézier Segment Offset Curves"
// <https://doi.org/10.1016/j.cag.2005.08.002>
static bool le_path_generate_offset_outline_for_contour(
    le_path_o* self, size_t contour_index,
    float      line_weight,
    float      tolerance,
    glm::vec2* outline_l_, size_t* max_count_outline_l,
    glm::vec2* outline_r_, size_t* max_count_outline_r ) {

	// We allocate space internally to store the results of our outline generation.
	// We do this because if we were to directly write back to the caller, we would
	// have to bounds-check against `max_count_outline[l|r]` on every write.
	//
	// This way, we do the bounds-check only at the very end, and if the bounds check
	// fails, we can at least tell the caller how many elements to reserve next time.

	std::vector<glm::vec2> outline_l;
	std::vector<glm::vec2> outline_r;

	outline_l.reserve( *max_count_outline_l );
	outline_r.reserve( *max_count_outline_r );

	// Now process the commands for this contour

	glm::vec2 prev_point  = {};
	float     line_offset = line_weight * 0.5f;

	auto& s = self->contours[ contour_index ];
	for ( auto const& command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:
			prev_point = command.p;
			break;
		case PathCommand::eLineTo:
			generate_offset_outline_line_to( outline_l, prev_point, command.p, -line_offset );
			generate_offset_outline_line_to( outline_r, prev_point, command.p, line_offset );
			prev_point = command.p;
			break;
		case PathCommand::eArcTo: {
			auto const& arc = command.data.as_arc;
			generate_offset_outline_arc_to( outline_l, outline_r, prev_point, command.p, arc.radii, arc.phi, arc.large_arc, arc.sweep, tolerance, line_weight );
			prev_point = command.p;
		} break;
		case PathCommand::eQuadBezierTo: {
			auto const& bez = command.data.as_quad_bezier;
			generate_offset_outline_cubic_bezier_to( outline_l,
			                                         outline_r,
			                                         prev_point,
			                                         prev_point + 2 / 3.f * ( bez.c1 - prev_point ),
			                                         command.p + 2 / 3.f * ( bez.c1 - command.p ),
			                                         command.p,
			                                         tolerance,
			                                         line_weight );

			prev_point = command.p;
		} break;
		case PathCommand::eCubicBezierTo: {
			auto const& bez = command.data.as_cubic_bezier;
			generate_offset_outline_cubic_bezier_to( outline_l,
			                                         outline_r,
			                                         prev_point,
			                                         bez.c1,
			                                         bez.c2,
			                                         command.p,
			                                         tolerance,
			                                         line_weight );
			prev_point = command.p;
		} break;
		case PathCommand::eClosePath: {
			if ( outline_l.empty() || outline_r.empty() ) {
				break;
			}
			glm::vec2 start_p = 0.5f * ( outline_l.front() + outline_r.front() );
			generate_offset_outline_line_to( outline_l, prev_point, start_p, -line_offset );
			generate_offset_outline_line_to( outline_r, prev_point, start_p, +line_offset );
			break;
		}
		case PathCommand::eUnknown:
			assert( false );
			break;
		}
	}

	// Copy generated vertices back to caller

	bool success = true;

	if ( outline_l_ && outline_l.size() <= *max_count_outline_l ) {
		memcpy( outline_l_, outline_l.data(), sizeof( glm::vec2 ) * outline_l.size() );
	} else {
		success = false;
	}

	if ( outline_r_ && outline_r.size() <= *max_count_outline_r ) {
		memcpy( outline_r_, outline_r.data(), sizeof( glm::vec2 ) * outline_r.size() );
	} else {
		success = false;
	}

	// update outline counts with actual number of generated vertices.
	*max_count_outline_l = outline_l.size();
	*max_count_outline_r = outline_r.size();

	return success;
}

// ----------------------------------------------------------------------

static void tessellate_joint( std::vector<glm::vec2>&   triangles,
                              stroke_attribute_t const* sa,
                              glm::vec2                 t,
                              PathCommand const*        cmd,
                              PathCommand const*        cmd_next ) {

	float     offset = sa->width * 0.5f;
	glm::vec2 n      = glm::vec2{ -t.y, t.x }; // normal onto current line

	glm::vec2 p1 = cmd->p;
	glm::vec2 p2 = cmd_next->p;

	// If this point and next point are identical
	// do nothing.
	if ( glm::isNull( p1 - p2, 0.001f ) ) {
		return;
	}

	glm::vec2 t1{};

	if ( cmd_next->type == PathCommand::eQuadBezierTo ) {
		auto const& bez = cmd_next->data.as_quad_bezier;
		t1              = quad_bezier_derivative( 0.f, cmd->p, bez.c1, cmd_next->p );
	} else if ( cmd_next->type == PathCommand::eCubicBezierTo ) {
		auto const& bez = cmd_next->data.as_cubic_bezier;
		t1              = cubic_bezier_derivative( 0.f, cmd->p, bez.c1, bez.c2, cmd_next->p );
		if ( bez.c1 == cmd->p ) {
			// we must account for the special case in which c1 is identical with cmd->p
			// in which case we must point t1 to c2.
			t1 = bez.c2 - cmd->p;
		}
	} else if ( cmd_next->type == PathCommand::eArcTo ) {
		auto const& arc = cmd_next->data.as_arc;
		t1              = get_arc_tangent_at_normalised_t( cmd->p, cmd_next->p, arc.radii, arc.phi, arc.large_arc, arc.sweep, 0.f );

	} else {
		t1 = p2 - p1;
	}

	float t1_length = glm::length( t1 );

	if ( t1_length >= std::numeric_limits<float>::epsilon() ) {
		t1 /= t1_length;
	} else {
		// we cannot continue - t1 cannot be calculated.
		return;
	}

	glm::vec2 n1 = glm::vec2{ -t1.y, t1.x }; // normal onto next line

	// If angles are identical, we should not add a joint
	if ( glm::isNull( t1 - t, 0.001f ) ) {
		return;
	}

	float rotation_direction = 1;

	if ( !glm::isNull( t1 + t, 0.001f ) ) {
		// if angles are not pointing exaclty against each other, we can calculate
		// a rotation_direction.
		// otherwise we can't.
		rotation_direction = glm::cross( glm::vec3( t, 0 ), glm::vec3( t1, 0 ) ).z;
		rotation_direction = rotation_direction / fabsf( rotation_direction );
	}

	// ---------| invariant: We need to add a joint

	if ( sa->line_join_type == stroke_attribute_t::eLineJoinBevel ||
	     sa->line_join_type == stroke_attribute_t::eLineJoinMiter ) {

		// -- Bevel: connect two edge points, and end point

		glm::vec2 edge_0 = p1 - rotation_direction * n * offset;
		glm::vec2 edge_1 = p1 - rotation_direction * n1 * offset;

		triangles.push_back( edge_0 );
		triangles.push_back( p1 );
		triangles.push_back( edge_1 );

		// -- Miter: point where offset tangents meet

		if ( sa->line_join_type == stroke_attribute_t::eLineJoinMiter ) {

			// We must calculate point at which the offset contours meet.

			float t_miter = ( edge_1.x - edge_0.x ) / ( t.x + t1.x ); // note we flip t1.x so that lines point to each other

			glm::vec2 p_miter = edge_0 + t_miter * t;

			triangles.push_back( edge_0 );
			triangles.push_back( p_miter );
			triangles.push_back( edge_1 );
		}
	}

	if ( sa->line_join_type == stroke_attribute_t::eLineJoinRound ) {

		// Calculate the angle for triangle fan segments - the angle
		// is such that the outline of the triangle fan is at most
		// at distance `sa->tolerance` from the perfect circle of
		// radius `offset`

		float angle_resolution = acosf( 1.f - ( sa->tolerance / offset ) );

		float  angle              = glm::angle( n, n1 );
		size_t angle_num_segments = size_t( fabsf( ceilf( angle / angle_resolution ) ) );

		angle_resolution = angle / angle_num_segments;

		float prev_angle = atan2f( n.y, n.x ); // start angle is equal to angle for n
		angle            = prev_angle + angle_resolution * rotation_direction;

		// start angle is orthonormal on t: n
		// end angle is orthonormal on t1: n1
		// centre point is p1

		glm::vec2 n_left  = n;
		glm::vec2 n_right = { cosf( angle ), sinf( angle ) };

		for ( size_t i = 0; i != angle_num_segments - 1; ++i ) {

			triangles.push_back( p1 - offset * rotation_direction * n_left );
			triangles.push_back( p1 );
			triangles.push_back( p1 - offset * rotation_direction * n_right );

			angle += angle_resolution * rotation_direction;
			n_left  = n_right;
			n_right = { cosf( angle ), sinf( angle ) };
		}

		n_right = n1;
		triangles.push_back( p1 - offset * rotation_direction * n_left );
		triangles.push_back( p1 );
		triangles.push_back( p1 - offset * rotation_direction * n_right );
	}
}

// ----------------------------------------------------------------------

static void draw_cap_round( std::vector<glm::vec2>& triangles, glm::vec2 const& p1, glm::vec2 const& n, stroke_attribute_t const* sa ) {
	// Calculate the angle for triangle fan segments - the angle
	// is such that the outline of the triangle fan is at most
	// at distance `sa->tolerance` from the perfect circle of
	// radius `offset`
	float offset           = sa->width * 0.5f;
	float angle_resolution = acosf( 1.f - ( sa->tolerance / offset ) );

	float  angle              = glm::pi<float>();
	size_t angle_num_segments = size_t( fabsf( ceilf( angle / angle_resolution ) ) );

	angle_resolution = angle / angle_num_segments;

	float prev_angle = atan2f( n.y, n.x ); // start angle is equal to angle for n
	angle            = prev_angle + angle_resolution;

	// start angle is orthonormal on t: n
	// end angle is orthonormal on t1: n1
	// centre point is p1

	for ( size_t i = 0; i != angle_num_segments; ++i ) {

		triangles.push_back( p1 - offset * glm::vec2( cosf( prev_angle ), sinf( prev_angle ) ) );
		triangles.push_back( p1 );
		triangles.push_back( p1 - offset * glm::vec2( cosf( angle ), sinf( angle ) ) );

		prev_angle = angle;
		angle += angle_resolution;
	}
}

// ----------------------------------------------------------------------

static void draw_cap_square( std::vector<glm::vec2>& triangles, glm::vec2 const& p1, glm::vec2 const& n, stroke_attribute_t const* sa ) {
	float offset = sa->width * 0.5f;

	glm::vec2 tangent = { -n.y, n.x };

	triangles.push_back( p1 - tangent * offset - offset * n );
	triangles.push_back( p1 + offset * n );
	triangles.push_back( p1 - offset * n );

	triangles.push_back( p1 - tangent * offset - offset * n );
	triangles.push_back( p1 - tangent * offset + offset * n );
	triangles.push_back( p1 + offset * n );
}

// ----------------------------------------------------------------------
// Iterate over path, based on `cmd`, `wasClosed`.
// update cmd_prev, cmd, cmd_next
// Returns false if no next element.
// TODO: Skip duplicates
static bool path_command_iterator( std::vector<PathCommand> const& cmds,
                                   PathCommand const**             cmd_prev,
                                   PathCommand const**             cmd,
                                   PathCommand const**             cmd_next,
                                   bool*                           wasClosed ) {
	auto cmds_start = cmds.data();
	auto cmds_end   = cmds.data() + cmds.size();

	if ( *wasClosed ) {
		return false;
	}

	if ( *cmd == nullptr ) {
		*cmd_prev = cmds_start; // first element must be moveto
		*cmd      = cmds_start + 1;
	} else {
		*cmd_prev = *cmd;
		( *cmd )++;
	}

	if ( *cmd == cmds_end ) {
		return false;
	}

	if ( ( *cmd )->type == PathCommand::eClosePath ) {
		*cmd_prev  = ( *cmd ) - 1;
		*wasClosed = true;
		*cmd_next  = cmds_start + 1;
		return true;
	}

	*cmd_next = ( *cmd ) + 1;

	if ( ( *cmd_next )->type == PathCommand::eClosePath ) {
		*cmd_next = cmds_start;
	}

	if ( *cmd_next == cmds_end ) {
		*cmd_next = nullptr;
	}

	return true;
};

// ----------------------------------------------------------------------
// Calculate tangent at path end point
// Note: does not return anything of value if pathcommand is not endpoint.
static bool get_path_endpoint_tangents( std::vector<PathCommand> const& commands, glm::vec2& tangent_tail, glm::vec2& tangent_head ) {
	auto cmds_start = commands.data();
	auto cmds_end   = cmds_start + commands.size();

	// calculate tangent at tail of path

	auto c_tail = cmds_start;
	auto c_head = cmds_start + 1;

	switch ( c_head->type ) {
	case PathCommand::eLineTo: {
		tangent_tail = c_head->p - c_tail->p;
	} break;
	case ( PathCommand::eQuadBezierTo ): {
		tangent_tail = quad_bezier_derivative( 0.f, c_tail->p, c_head->data.as_quad_bezier.c1, c_head->p );
	} break;
	case ( PathCommand::eCubicBezierTo ): {
		tangent_tail = cubic_bezier_derivative( 0.f, c_tail->p, c_head->data.as_cubic_bezier.c1, c_head->data.as_cubic_bezier.c2, c_head->p );
	} break;
	case ( PathCommand::eArcTo ): {
		auto& arc    = c_head->data.as_arc;
		tangent_tail = get_arc_tangent_at_normalised_t( c_tail->p, c_head->p, arc.radii, arc.phi, arc.large_arc, arc.sweep, 0.f );
	} break;
	default:
		assert( false ); // unreachable
		return false;
	}

	// calculate tangent at head of path

	c_tail = cmds_end - 2;
	c_head = cmds_end - 1;

	switch ( c_head->type ) {
	case PathCommand::eLineTo: {
		tangent_head = c_head->p - c_tail->p;
	} break;
	case ( PathCommand::eQuadBezierTo ): {
		tangent_head = quad_bezier_derivative( 1.f, c_tail->p, c_head->data.as_quad_bezier.c1, c_head->p );
	} break;
	case ( PathCommand::eCubicBezierTo ): {
		tangent_head = cubic_bezier_derivative( 1.f, c_tail->p, c_head->data.as_cubic_bezier.c1, c_head->data.as_cubic_bezier.c2, c_head->p );
	} break;
	case ( PathCommand::eArcTo ): {
		auto& arc    = c_head->data.as_arc;
		tangent_head = get_arc_tangent_at_normalised_t( c_tail->p, c_head->p, arc.radii, arc.phi, arc.large_arc, arc.sweep, 1.f );
	} break;
	default:
		assert( false ); // unreachable
		return false;
	}

	tangent_tail = glm::normalize( tangent_tail );
	tangent_head = glm::normalize( tangent_head );

	return true;
};

// ----------------------------------------------------------------------
// Tessellate a lines strip between two outlines vertices_l, vertices_r
// into a list of triangles
void tessellate_outline_l_r( std::vector<glm::vec2>& triangles, std::vector<glm::vec2> const& vertices_l, std::vector<glm::vec2> const& vertices_r ) {

	if ( vertices_l.empty() || vertices_r.empty() ) {
		// assert( false ); // something went wrong when generating vertices.
		return;
	}

	// ---------| vertices_l and vertices_r are not empty.

	glm::vec2 const* v_l = vertices_l.data();
	glm::vec2 const* v_r = vertices_r.data();

	glm::vec2 const* l_prev = v_l;
	glm::vec2 const* r_prev = v_r;

	glm::vec2 const* l = l_prev + 1;
	glm::vec2 const* r = r_prev + 1;

	glm::vec2 const* const l_end = vertices_l.data() + vertices_l.size();
	glm::vec2 const* const r_end = vertices_r.data() + vertices_r.size();

	for ( ; ( l != l_end || r != r_end ); ) {

		if ( r != r_end ) {

			triangles.push_back( *l_prev );
			triangles.push_back( *r_prev );
			triangles.push_back( *r );

			r_prev = r;
			r++;
		}

		if ( l != l_end ) {

			triangles.push_back( *l_prev );
			triangles.push_back( *r_prev );
			triangles.push_back( *l );

			l_prev = l;
			l++;
		}
	}
}

// ----------------------------------------------------------------------

bool le_path_tessellate_thick_contour( le_path_o* self, size_t contour_index, le_path_api::stroke_attribute_t const* stroke_attributes, glm::vec2* vertices, size_t* num_vertices ) {
	std::vector<glm::vec2> triangles;

	triangles.reserve( *num_vertices );

	auto& contour = self->contours[ contour_index ];

	if ( contour.commands.empty() ) {
		*num_vertices = 0;
		return true;
	}

	// ---------| Invariant: There are commands to render

	PathCommand const* command      = nullptr;
	PathCommand const* command_prev = nullptr;
	PathCommand const* command_next = nullptr;
	bool               wasClosed    = false;

	std::vector<glm::vec2> vertices_l;
	std::vector<glm::vec2> vertices_r;

	glm::vec2 tangent{};

	while ( path_command_iterator( contour.commands, &command_prev, &command, &command_next, &wasClosed ) ) {

		switch ( command->type ) {

		case PathCommand::eLineTo: {
			generate_offset_outline_line_to( vertices_l, vertices_r, command_prev->p, command->p, stroke_attributes->width );
			tangent = command->p - command_prev->p;
			break;
		}
		case PathCommand::eQuadBezierTo: {
			auto const& bez = command->data.as_quad_bezier;

			glm::vec2 p0 = command_prev->p;
			glm::vec2 p1 = command->p;
			glm::vec2 c1 = p0 + 2.f / 3.f * ( bez.c1 - p0 );
			glm::vec2 c2 = p1 + 2 / 3.f * ( bez.c1 - p1 );

			generate_offset_outline_cubic_bezier_to( vertices_l, vertices_r, p0, c1, c2, p1, stroke_attributes->tolerance, stroke_attributes->width );

			if ( bez.c1 == command->p ) {
				// We must account for the special case in which c1 is identical with end point.
				// in this case we calculate the tangent to point from previous point to end point.
				tangent = command->p - command_prev->p;
			} else {
				tangent = quad_bezier_derivative( 1.f, command_prev->p, bez.c1, command->p );
			}

			break;
		}
		case PathCommand::eCubicBezierTo: {
			auto const& bez = command->data.as_cubic_bezier;

			generate_offset_outline_cubic_bezier_to( vertices_l, vertices_r, command_prev->p, bez.c1, bez.c2, command->p, stroke_attributes->tolerance, stroke_attributes->width );

			if ( bez.c2 == command->p ) {
				// We must account for the special case in which c2 is identical with end point.
				// in this case we calculate the tangent to point from c1 to end point.
				if ( bez.c1 == command->p ) {
					// We must account for the special case in which c1 is identical with end point.
					// in this case we calculate the tangent to point from previous point to end point.
					tangent = command->p - command_prev->p;
				} else {
					tangent = command->p - bez.c1;
				}
			} else {
				tangent = cubic_bezier_derivative( 1.f, command_prev->p, bez.c1, bez.c2, command->p );
			}

			break;
		}
		case PathCommand::eArcTo: {
			auto const& arc = command->data.as_arc;

			generate_offset_outline_arc_to( vertices_l, vertices_r, command_prev->p, command->p, arc.radii, arc.phi, arc.large_arc, arc.sweep, stroke_attributes->tolerance, stroke_attributes->width );

			tangent = get_arc_tangent_at_normalised_t( command_prev->p, command->p, arc.radii, arc.phi, arc.large_arc, arc.sweep, 1.f );

			break;
		}
		case PathCommand::eClosePath: {

			generate_offset_outline_line_to( vertices_l, vertices_r, command_prev->p, contour.commands.front().p, stroke_attributes->width );

			tangent = contour.commands.front().p - command_prev->p;

			break;
		}
		default:
			// This covers eMoveTo and eUnknown
			continue;
		}

		// Tessellate any outlines into triangles.

		if ( !vertices_l.empty() && !vertices_r.empty() ) {
			tessellate_outline_l_r( triangles, vertices_l, vertices_r );
		}

		vertices_l.clear();
		vertices_r.clear();

		// Draw joins
		//
		// Note that tangent at end may still bne undefined - that's the case if p0 == p1
		// we test against this case by splitting apart the normlisation of the tangent -
		// we first calculate the length (against which we may test) - in case of
		// undefined tangent, length will be very close to zero.
		//
		float tangent_length = glm::length( tangent );

		if ( command_next && ( tangent_length > std::numeric_limits<float>::epsilon() ) ) {

			// ----------| Invariant: tangent_length > 0
			tangent /= tangent_length;

			tessellate_joint( triangles, stroke_attributes, tangent,
			                  ( command->type == PathCommand::eClosePath ) ? &contour.commands.front()
			                                                               : command,
			                  command_next );
		}

	} // end path commands iterator

	// -- Draw caps if path was not closed

	if ( !wasClosed && !contour.commands.empty() &&
	     stroke_attributes->line_cap_type != stroke_attribute_t::LineCapType::eLineCapButt ) {

		if ( contour.commands.size() == 1 ) {
			// path has zero length
			// draw ending on first point
		} else {

			// we must find out tangent into the path

			PathCommand* tail = &contour.commands.front();
			PathCommand* head = &contour.commands.back();

			glm::vec2 tangent_head{};
			glm::vec2 tangent_tail{};

			get_path_endpoint_tangents( contour.commands, tangent_tail, tangent_head );

			if ( stroke_attributes->line_cap_type == stroke_attribute_t::LineCapType::eLineCapRound ) {
				draw_cap_round( triangles, head->p, { -tangent_head.y, tangent_head.x }, stroke_attributes );
				draw_cap_round( triangles, tail->p, { tangent_tail.y, -tangent_tail.x }, stroke_attributes );
			} else if ( stroke_attributes->line_cap_type == stroke_attribute_t::LineCapType::eLineCapSquare ) {
				draw_cap_square( triangles, head->p, { tangent_head.y, -tangent_head.x }, stroke_attributes );
				draw_cap_square( triangles, tail->p, { -tangent_tail.y, tangent_tail.x }, stroke_attributes );
			}
		}
	}

	//

	bool success = true;

	if ( vertices && triangles.size() <= *num_vertices ) {
		memcpy( vertices, triangles.data(), sizeof( glm::vec2 ) * triangles.size() );
	} else {
		success = false;
	}

	// update outline counts with actual number of generated vertices.
	*num_vertices = triangles.size();

	return success;
}

// ----------------------------------------------------------------------

static void le_path_iterate_vertices_for_contour( le_path_o* self, size_t const& contour_index, le_path_api::contour_vertex_cb callback, void* user_data ) {

	assert( self->contours.size() > contour_index );

	auto const& s = self->contours[ contour_index ];

	for ( auto const& command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:        // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eLineTo:        // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eQuadBezierTo:  // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eCubicBezierTo: // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eArcTo:         // fall-through, as we're allways just issueing the vertex, ignoring control points
			callback( user_data, command.p );
			break;
		case PathCommand::eClosePath:
			callback( user_data, s.commands[ 0 ].p ); // re-issue first vertex
			break;
		case PathCommand::eUnknown:
			assert( false );
			break;
		}
	}
}

// ----------------------------------------------------------------------

static void le_path_iterate_quad_beziers_for_contour( le_path_o* self, size_t const& contour_index, le_path_api::contour_quad_bezier_cb callback, void* user_data ) {

	assert( self->contours.size() > contour_index );

	auto const& s = self->contours[ contour_index ];

	glm::vec2 p0 = {};

	for ( auto const& command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:
			p0 = command.p;
			break;
		case PathCommand::eLineTo:
			p0 = command.p;
			break;
		case PathCommand::eQuadBezierTo:
			callback( user_data, p0, command.p, command.data.as_quad_bezier.c1 );
			p0 = command.p;
			break;
		case PathCommand::eArcTo:
			p0 = command.p;
			break;
		case PathCommand::eCubicBezierTo:
			p0 = command.p;
			break;
		case PathCommand::eClosePath:
			break;
		case PathCommand::eUnknown:
			assert( false );
			break;
		}
	}
}

// ----------------------------------------------------------------------
// Updates `result` to the vertex position on polyline
// at normalized position `t`
static void le_polyline_get_at( Polyline const& polyline, float t, glm::vec2* result ) {

	// -- Calculate unnormalised distance
	float d = t * float( polyline.total_distance );

	// find the first element in polyline which has a position larger than pos

	size_t       a = 0, b = 1;
	size_t const n = polyline.distances.size();

	assert( n >= 2 ); // we must have at least two elements for this to work.

	for ( ; b < n - 1; ++a, ++b ) {
		if ( polyline.distances[ b ] > d ) {
			// find the second distance which is larger than our test distance
			break;
		}
	}

	assert( b < n ); // b must not overshoot.

	float dist_start = polyline.distances[ a ];
	float dist_end   = polyline.distances[ b ];

	float scalar = map( d, dist_start, dist_end, 0.f, 1.f );

	glm::vec2 const& start_vertex = polyline.vertices[ a ];
	glm::vec2 const& end_vertex   = polyline.vertices[ b ];

	*result = start_vertex + scalar * ( end_vertex - start_vertex );
}

// ----------------------------------------------------------------------
// return calculated position on polyline
static void le_path_get_polyline_at_pos_interpolated( le_path_o* self, size_t const& polyline_index, float t, glm::vec2* result ) {
	assert( polyline_index < self->polylines.size() );
	le_polyline_get_at( self->polylines[ polyline_index ], t, result );
}

// ----------------------------------------------------------------------

static void le_polyline_resample( Polyline& polyline, float interval ) {
	Polyline poly_resampled;

	// -- How many times can we fit interval into length of polyline?

	// Find next integer multiple
	size_t n_segments = size_t( std::round( polyline.total_distance / interval ) );
	n_segments        = std::max( size_t( 1 ), n_segments );

	float delta = 1.f / float( n_segments );

	if ( n_segments == 1 ) {
		// we cannot resample polylines which have only one segment.
		return;
	}

	// reserve n vertices

	poly_resampled.vertices.reserve( n_segments + 1 );
	poly_resampled.distances.reserve( n_segments + 1 );
	poly_resampled.tangents.reserve( n_segments + 1 );

	// Find first point
	glm::vec2 vertex;
	le_polyline_get_at( polyline, 0.f, &vertex );
	trace_move_to( poly_resampled, vertex );

	// Note that we must add an extra vertex at the end so that we
	// capture the correct number of segments.
	for ( size_t i = 1; i <= n_segments; ++i ) {
		le_polyline_get_at( polyline, i * delta, &vertex );
		// We use trace_line_to, because this will get us more accurate distance
		// calculations - trace_line_to updates the distances as a side-effect,
		// effectively redrawing the polyline as if it was a series of `line_to`s.
		trace_line_to( poly_resampled, vertex );
	}

	std::swap( polyline, poly_resampled );
}

// ----------------------------------------------------------------------

static void le_path_resample( le_path_o* self, float interval ) {

	if ( self->contours.empty() ) {
		// nothing to do.
		return;
	}

	// --------| invariant: subpaths exist

	if ( self->polylines.empty() ) {
		le_path_trace_path( self, 100 ); // We must trace path - we will do it at a fairy high resolution.
	}

	// Resample each polyline, turn by turn

	for ( auto& p : self->polylines ) {
		le_polyline_resample( p, interval );
		// -- Enforce invariant that says for closed paths:
		// First and last vertex must be identical.
	}
}

// ----------------------------------------------------------------------

static void le_path_move_to( le_path_o* self, glm::vec2 const* p ) {
	// move_to means a new subpath, unless the last command was a
	self->contours.emplace_back(); // add empty subpath
	self->contours.back().commands.emplace_back( PathCommand::eMoveTo, *p );
}

// ----------------------------------------------------------------------

static void le_path_line_to( le_path_o* self, glm::vec2 const* p ) {
	if ( self->contours.empty() ) {
		constexpr static auto v0 = glm::vec2{};
		le_path_move_to( self, &v0 );
	}
	assert( !self->contours.empty() ); // subpath must exist
	self->contours.back().commands.emplace_back( PathCommand::eLineTo, *p );
}

// ----------------------------------------------------------------------

// Fetch the current pen point by grabbing the previous target point
// from the command stream.
static glm::vec2 const* le_path_get_previous_p( le_path_o* self ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	glm::vec2 const* p = nullptr;

	auto const& c = self->contours.back().commands.back(); // fetch last command

	switch ( c.type ) {
	case PathCommand::eMoveTo:        // fall-through
	case PathCommand::eLineTo:        // fall-through
	case PathCommand::eQuadBezierTo:  // fall-through
	case PathCommand::eCubicBezierTo: // fall-through
	case PathCommand::eArcTo:         // fall-through
		p = &c.p;
		break;
	default:
		// Error. Previous command must be one of above
		fprintf( stderr, "Warning: Relative path instruction requires absolute position to be known. In %s:%i\n", __FILE__, __LINE__ );
		break;
	}

	return p;
}

// ----------------------------------------------------------------------

static void le_path_line_horiz_to( le_path_o* self, float px ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		glm::vec2 p2 = *p;
		p2.x         = px;
		le_path_line_to( self, &p2 );
	}
}

// ----------------------------------------------------------------------

static void le_path_line_vert_to( le_path_o* self, float py ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		glm::vec2 p2 = *p;
		p2.y         = py;
		le_path_line_to( self, &p2 );
	}
}

// ----------------------------------------------------------------------

static void le_path_quad_bezier_to( le_path_o* self, glm::vec2 const* p, glm::vec2 const* c1 ) {
	assert( !self->contours.empty() ); // contour must exist
	self->contours.back().commands.emplace_back( *p, PathCommand::Data::AsQuadBezier{ *c1 } );
}

// ----------------------------------------------------------------------

static void le_path_cubic_bezier_to( le_path_o* self, glm::vec2 const* p, glm::vec2 const* c1, glm::vec2 const* c2 ) {
	assert( !self->contours.empty() ); // subpath must exist
	self->contours.back().commands.emplace_back( *p, PathCommand::Data::AsCubicBezier{ *c1, *c2 } );
}

// ----------------------------------------------------------------------

static void le_path_arc_to( le_path_o* self, glm::vec2 const* p, glm::vec2 const* radii, float phi, bool large_arc, bool sweep ) {
	assert( !self->contours.empty() ); // subpath must exist
	self->contours.back().commands.emplace_back( *p, PathCommand::Data::AsArc{ *radii, phi, large_arc, sweep } );
}

// ----------------------------------------------------------------------

static void le_path_close_path( le_path_o* self ) {
	self->contours.back().commands.emplace_back( PathCommand::eClosePath, glm::vec2{} );
}

// ----------------------------------------------------------------------

// "velocity function" for hobby algorithm.
// See videos linked under <http://weitz.de/hobby/> for details
static inline float rho( float a, float b ) {
	float sa  = sin( a );
	float sb  = sin( b );
	float ca  = cos( a );
	float cb  = cos( b );
	float s5  = sqrt( 5.f );
	float num = 4.f + sqrt( 8.f ) * ( sa - sb / 16.f ) * ( sb - sa / 16.f ) * ( ca - cb );
	float den = 2.f + ( s5 - 1.f ) * ca + ( 3.f - s5 ) * cb;
	return num / den;
}

// Apply hobby algorithm for a closed path onto path commands.
// This effectively changes all commands to type cubic bezier, and
// will set their control points to optimise for best curvature.
static void path_commands_apply_hobby_closed( std::vector<PathCommand>& commands ) {
	// note that last command will be the close command - all other commands are legit.

	// We expect a list of path commands with the following pattern:
	//
	// m, p(0), p(1), p(2), p(n), p(0), close
	//
	// Note that the last element is purely a flag, and the first element
	// only contains a moveto instruction.

	size_t count = commands.size() - 2; // we remove the close flag from the count, and the last, doubled vertex

	std::vector<float>     D( count );
	std::vector<glm::vec2> delta( count ); // vector between points

	for ( size_t i = 0; i != count; i++ ) {
		size_t j   = ( i + 1 ) % count; // next point wrapped around
		delta[ i ] = commands[ j ].p - commands[ i ].p;
		D[ i ]     = glm::length( delta[ i ] );
	}

	std::vector<float> gamma( count ); // angles for directions between points (relative to x-axis)

	for ( size_t i = 0; i != count; i++ ) {
		size_t    k          = ( i + count - 1 ) % count; // index for previous point, wrapped around
		glm::vec2 delta_norm = delta[ k ] / D[ k ];       // normalise delta, implicitly means x = sin(a), y = cos(a)
		delta_norm.y *= -1;                               // flip sign on y
		// rotate so that x-axis is previous direction
		glm::vec2 d_rot =
		    delta[ i ] *
		    glm::mat2{
		        { delta_norm.x, -delta_norm.y }, // first column : cos, -sin
		        { delta_norm.y, delta_norm.x },  // second column: sin, cos
		    };
		gamma[ i ] = atan2( d_rot.y, d_rot.x ); // capture angles
	}

	std::vector<float> alpha( count );
	std::vector<float> beta( count );

	{
		// Calculate alpha (and implicitly beta) via the sherman-morrisson-woodbury
		// formula.

		std::vector<float> a( count );
		std::vector<float> b( count );
		std::vector<float> c( count );
		std::vector<float> d( count );

		for ( size_t i = 0; i != count; i++ ) {
			size_t j = ( i + 1 ) % count;         // previous point, wrapped
			size_t k = ( i + count - 1 ) % count; // next point, wrapped
			a[ i ]   = 1.f / D[ k ];
			b[ i ]   = ( 2.f * D[ k ] + 2.f * D[ i ] ) / ( D[ k ] * D[ i ] );
			c[ i ]   = 1.f / D[ i ];
			d[ i ]   = -( 2.f * gamma[ i ] * D[ i ] + gamma[ j ] * D[ k ] ) / ( D[ k ] * D[ i ] );
		}

		sherman_morrisson_woodbury( a.data(), b.data(), c.data(), d.data(),
		                            alpha.size(), alpha.data() );

		// beta = -1 * ( gamma + alpha )
		for ( size_t i = 0; i != count; i++ ) {
			size_t j  = ( i + 1 ) % count; // next index, wrapped
			beta[ i ] = -gamma[ j ] - alpha[ j ];
		}
	}

	for ( size_t i = 0; i != count; i++ ) {
		float a = rho( alpha[ i ], beta[ i ] ) * D[ i ] / 3.f; // velocity function "rho"
		float b = rho( beta[ i ], alpha[ i ] ) * D[ i ] / 3.f; // velocity function in the other direction, "sigma"

		auto& c = commands[ 1 + i ];

		c.data.as_cubic_bezier.c1 = commands[ i ].p + a * glm::normalize( glm::rotate( delta[ i ], alpha[ i ] ) );
		c.data.as_cubic_bezier.c2 = commands[ i + 1 ].p - b * glm::normalize( glm::rotate( delta[ i ], -beta[ i ] ) );

		c.type = PathCommand::Type::eCubicBezierTo;
	}
}

// Apply hobby algorithm for a closed path onto path commands.
// This effectively changes all commands to type cubic bezier, and
// will set their control points to optimise for best curvature.
static void path_commands_apply_hobby_open( std::vector<PathCommand>& commands ) {
	// note that last command will be the close command - all other commands are legit.

	// We expect a list of path commands with the following pattern:
	//
	// m, p(0), p(1), p(2), p(n)
	//
	// Note that the last element is purely a flag, and the first element
	// only contains a moveto instruction.

	int count = commands.size() - 1; // we remove the close flag from the count, and the last, doubled vertex

	std::vector<float>     D( count );
	std::vector<glm::vec2> delta( count ); // vector between points

	for ( int i = 0; i < count; i++ ) {
		delta[ i ] = commands[ i + 1 ].p - commands[ i ].p;
		D[ i ]     = glm::length( delta[ i ] );
	}

	std::vector<float> gamma( count + 1 ); // angles for directions between points (relative to x-axis)

	for ( int i = 1; i < count; i++ ) {
		glm::vec2 delta_norm = delta[ i - 1 ] / D[ i - 1 ]; // normalise delta, this implicitly means x = sin(a), y = cos(a)
		delta_norm.y *= -1;                                 // flip sign on y
		// rotate so that x-axis is previous direction
		glm::vec2 d_rot =
		    delta[ i ] *
		    glm::mat2{
		        { delta_norm.x, -delta_norm.y }, // first column : cos, -sin
		        { delta_norm.y, delta_norm.x },  // second column: sin, cos
		    };
		gamma[ i ] = atan2( d_rot.y, d_rot.x ); // capture angles
	}

	std::vector<float> alpha( count + 1 );
	std::vector<float> beta( count );

	{
		// Calculate alpha (and implicitly beta)
		// via the Thomas algorithm.

		std::vector<float> a( count + 1 );
		std::vector<float> b( count + 1 );
		std::vector<float> c( count + 1 );
		std::vector<float> d( count + 1 );

		for ( int i = 1; i < count; i++ ) {
			a[ i ] = 1 / D[ i - 1 ];
			b[ i ] = ( 2 * D[ i - 1 ] + 2 * D[ i ] ) / ( D[ i - 1 ] * D[ i ] );
			c[ i ] = 1 / D[ i ];
			d[ i ] = -( 2 * gamma[ i ] * D[ i ] +
			            gamma[ i + 1 ] * D[ i - 1 ] ) /
			         ( D[ i - 1 ] * D[ i ] );
		}

		float const omega = 0.f;

		b[ 0 ]     = 2 + omega;
		c[ 0 ]     = 2 * omega + 1;
		d[ 0 ]     = -c[ 0 ] * gamma[ 1 ];
		a[ count ] = 2 * omega + 1;
		b[ count ] = 2 + omega;
		d[ count ] = 0;

		thomas( a.data(), b.data(), c.data(), d.data(),
		        alpha.size(), alpha.data() );

		// beta = -1 * ( gamma + alpha )
		for ( int i = 0; i < count - 1; i++ ) {
			beta[ i ] = -gamma[ i + 1 ] - alpha[ i + 1 ];
		}
		beta[ count - 1 ] = -alpha[ count ];
	}

	for ( int i = 0; i < count; i++ ) {
		float a = rho( alpha[ i ], beta[ i ] ) * D[ i ] / 3.f; // velocity function "rho"
		float b = rho( beta[ i ], alpha[ i ] ) * D[ i ] / 3.f; // velocity function in the other direction, "sigma"

		auto& c = commands[ 1 + i ];

		c.data.as_cubic_bezier.c1 = commands[ i ].p + a * glm::normalize( glm::rotate( delta[ i ], alpha[ i ] ) );
		c.data.as_cubic_bezier.c2 = commands[ i + 1 ].p - b * glm::normalize( glm::rotate( delta[ i ], -beta[ i ] ) );

		c.type = PathCommand::Type::eCubicBezierTo;
	}
}

// Applies the hobby algorithm on the last contour.
//
// any commands in the contour will be interpreted as plain points, and
// the contour will be transformed into cubic beziers, with freshly
// calculated control points.
// Depending on whether the contour has a `close` command as the last
// element, the open or the closed version of the hobby algorithm is
// applied.
static void le_path_apply_hobby_on_last_contour( le_path_o* self ) {

	if ( self->contours.empty() ) {
		return;
	}

	// ----------| invariant: there is a last contour

	auto& commands = self->contours.back().commands;

	if ( commands.back().type == PathCommand::Type::eClosePath ) {
		path_commands_apply_hobby_closed( commands );
	} else {
		path_commands_apply_hobby_open( commands );
	}
}

// ----------------------------------------------------------------------

static void le_path_ellipse( le_path_o* self, glm::vec2 const* centre, float r_x, float r_y ) {
	glm::vec2 radii = glm::vec2{ r_x, r_y };

	glm::vec2 a0 = *centre + glm::vec2{ r_x, 0 };
	le_path_move_to( self, &a0 );

	glm::vec2 a1 = *centre + glm::vec2{ 0, -r_y };
	le_path_arc_to( self, &a1, &radii, 0, false, false );

	glm::vec2 a2 = *centre + glm::vec2{ -r_x, 0 };
	le_path_arc_to( self, &a2, &radii, 0, false, false );

	glm::vec2 a3 = *centre + glm::vec2{ 0, r_y };
	le_path_arc_to( self, &a3, &radii, 0, false, false );

	le_path_arc_to( self, &a0, &radii, 0, false, false );

	le_path_close_path( self );
}

// ----------------------------------------------------------------------

static size_t le_path_get_num_polylines( le_path_o* self ) {
	return self->polylines.size();
}

static size_t le_path_get_num_contours( le_path_o* self ) {
	return self->contours.size();
}

// ----------------------------------------------------------------------

static bool le_path_get_vertices_for_polyline( le_path_o* self, size_t const& polyline_index, glm::vec2* vertices, size_t* numVertices ) {
	bool success = false;
	assert( polyline_index < self->polylines.size() );

	auto const& polyline = self->polylines[ polyline_index ];

	if ( polyline.vertices.size() <= *numVertices ) {
		memcpy( vertices, polyline.vertices.data(),
		        sizeof( decltype( polyline.vertices )::value_type ) * polyline.vertices.size() );
		success = true;
	}

	*numVertices = polyline.vertices.size();
	return success;
}

// ----------------------------------------------------------------------

static bool le_path_get_tangents_for_polyline( le_path_o* self, size_t const& polyline_index, glm::vec2* tangents, size_t* numTangents ) {
	bool success = false;
	assert( polyline_index < self->polylines.size() );

	auto const& polyline = self->polylines[ polyline_index ];
	if ( polyline.tangents.size() <= *numTangents ) {
		memcpy( tangents, polyline.tangents.data(),
		        sizeof( decltype( polyline.tangents )::value_type ) * polyline.tangents.size() );
		success = true;
	}

	*numTangents = polyline.tangents.size();
	return success;
}

// ----------------------------------------------------------------------

// Accumulates `*offset_local` into `*offset_total`.
// Always returns true.
static inline bool add_offsets( int offset_local, int* offset_total ) {
	( *offset_total ) += offset_local;
	return true;
}

// Accumulates coordinates
// always returns true
static inline bool set_coordinate_pair( glm::vec2* p, glm::vec2 const value ) {
	( *p ) = value;
	return true;
}

// Accumulates coordinates
// always returns true
static inline bool update_coordinate( glm::vec2* p, glm::vec2 const offset ) {
	( *p ) += offset;
	// logger.info( "p: %4.2f,%4.2f", p->x, p->y );
	return true;
}

// Accumulates float values
// always returns true
static bool accum_float( float* p, float const offset ) {
	( *p ) += offset;
	return true;
}

// Returns true if string c may be interpreted as
// a number,
// If true,
// + increases *offset by the count of characters forming the number.
// + sets *f to value of parsed number.
//
static bool is_float_number( char const* c, int* offset, float* f ) {
	if ( *c == 0 )
		return false;

	char* num_end;

	float tmp_float = strtof( c, &num_end ); // num_end will point to one after last number character
	*offset += ( num_end - c );              // add number of number characters to offset

	if ( num_end != c ) {
		*f = tmp_float;
		return true;
	} // if at least one number character was extracted, we were successful
	return false;
}

// Returns true if needle matches c.
// Increases *offset by 1 if true.
static bool is_character_match( char const needle, char const* c, int* offset ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	if ( *c == needle ) {
		++( *offset );
		// logger.info( "Match character '%c'", *c );
		return true;
	} else {
		return false;
	}
}

// Returns true if character is either 0, or 1.
// Sets *value to `true` if character is 1,
// Sets *value to `false` if character is 0.
// Increases *offset by 1 if returns true
static bool is_boolean_zero_or_one( char const* c, int* offset, bool* value ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	if ( is_character_match( '0', c, offset ) ) {
		*value = false;
		return true;
	}
	if ( is_character_match( '1', c, offset ) ) {
		*value = true;
		return true;
	}

	// ----------| invariant: nothing found.

	*value = false;
	return false;
}

// Returns true if what c points to may be interpreted as
// whitespace, and sets offset to the count of whitespace
// characters.
static bool is_whitespace( char const* c, int* offset ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	bool found_whitespace = false;

	// while c is one of possible whitespace characters
	while ( *c == 0x20 || *c == 0x9 || *c == 0xD || *c == 0xA ) {
		c++;
		++( *offset );
		found_whitespace = true;
	}

	return found_whitespace;
}

// returns true if there is a character coming,
// increases offset if the character is whitespace
static bool is_optional_whitespace( char const* c, int* offset ) {
	if ( *c == 0 ) {
		return false;
	}
	is_whitespace( c, offset );
	return true;
}

static bool is_comma_or_whitespace( char const* c, int* local_offset ) {
	return is_character_match( ',', c, local_offset ) ||
	       is_whitespace( c, local_offset );
}

constexpr uint32_t PATH_PARSER_STATE_FLAG_IS_REPEATED = 0x1 << 0;
constexpr uint32_t PATH_PARSER_STATE_FLAG_IS_ABSOLUTE = 0x1 << 1;

static bool is_repeat_or_command_char( char const needle, char const* c, int* local_offset, uint32_t* state_flags ) {
	bool is_absolute = false;
	bool is_repeated = *state_flags & PATH_PARSER_STATE_FLAG_IS_REPEATED;

	// Set flag for is_repeated no matter what so that subsequent queries get it
	*state_flags |= PATH_PARSER_STATE_FLAG_IS_REPEATED;

	if ( is_repeated ) {
		return true;
	} else {
		// Find if this is an absolute instruction
		is_absolute = is_character_match( toupper( needle ), c, local_offset );
		if ( is_absolute ) {
			*state_flags |= PATH_PARSER_STATE_FLAG_IS_ABSOLUTE;
			return true;
		}
	}

	return is_character_match( needle, c, local_offset );
}

// Returns true if c points to a coordinate pair.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*coord` will hold the vertex defined by the coordinate pair
static bool is_coordinate_pair( char const* c, int* offset, glm::vec2* v ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	int       local_offset = 0;
	glm::vec2 tmp_p        = {};

	return is_float_number( c, &local_offset, &tmp_p.x ) &&                // note how offset is re-used
	       is_comma_or_whitespace( c + local_offset, &local_offset ) &&    // in subsequent tests, so that
	       is_float_number( c + local_offset, &local_offset, &tmp_p.y ) && // each test begins at the previous offset
	       set_coordinate_pair( v, tmp_p ) &&                              // only apply coordinate update if parsing was successful
	       add_offsets( local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'm' instruction.
// note
// An 'm' instruction is a RELATIVE move_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the target point
static bool is_m_instruction( char const* c, int* offset, glm::vec2* p0, uint32_t* state_flags ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	int       local_offset = 0;
	glm::vec2 previous_p   = *p0;

	return ( is_repeat_or_command_char( 'm', c, &local_offset, state_flags ) ) &&
	       is_optional_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       add_offsets( local_offset, offset ) &&
	       ( ( *state_flags & PATH_PARSER_STATE_FLAG_IS_ABSOLUTE ) ||
	         update_coordinate( p0, previous_p ) );
}

// Return true if string `c` can be evaluated as an 'L' instruction.
// An 'l' instruction is an ABSOLUTE line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the target point
static bool is_l_instruction( char const* c, int* offset, glm::vec2* p0, uint32_t* state_flags ) {
	if ( *c == 0 )
		return false;

	int       local_offset = 0;
	glm::vec2 previous_p   = *p0;

	return ( is_repeat_or_command_char( 'l', c, &local_offset, state_flags ) ) &&
	       is_optional_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       add_offsets( local_offset, offset ) &&
	       ( ( *state_flags & PATH_PARSER_STATE_FLAG_IS_ABSOLUTE ) ||
	         update_coordinate( p0, previous_p ) );
}

// Return true if string `c` can be evaluated as an 'h' instruction.
// A 'h' instruction is a horizontal line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*px` will hold the value of the target point's x coordinate
static bool is_h_instruction( char const* c, int* offset, float* px, uint32_t* state_flags ) {
	if ( *c == 0 )
		return false;

	int   local_offset = 0;
	float previous_p   = *px;

	return ( is_repeat_or_command_char( 'h', c, &local_offset, state_flags ) ) &&
	       is_optional_whitespace( c + local_offset, &local_offset ) &&
	       is_float_number( c + local_offset, &local_offset, px ) &&
	       add_offsets( local_offset, offset ) &&
	       ( ( *state_flags & PATH_PARSER_STATE_FLAG_IS_ABSOLUTE ) ||
	         accum_float( px, previous_p ) );
}

// Return true if string `c` can be evaluated as an 'l' instruction.
// A 'v' instruction is a vertical line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*px` will hold the value of the target point's x coordinate
static bool is_v_instruction( char const* c, int* offset, float* py, uint32_t* state_flags ) {
	if ( *c == 0 )
		return false;

	int   local_offset = 0;
	float previous_p   = *py;

	return ( is_repeat_or_command_char( 'v', c, &local_offset, state_flags ) ) &&
	       is_optional_whitespace( c + local_offset, &local_offset ) &&
	       is_float_number( c + local_offset, &local_offset, py ) &&
	       add_offsets( local_offset, offset ) &&
	       ( ( *state_flags & PATH_PARSER_STATE_FLAG_IS_ABSOLUTE ) ||
	         accum_float( py, previous_p ) );
}

// Return true if string `c` can be evaluated as a 'c' instruction.
// A 'c' instruction is a cubic bezier instruction.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*c1` will hold the value of control point 0
// + `*c2` will hold the value of control point 1
// + `*p1` will hold the value of the target point
static bool is_c_instruction( char const* c, int* offset, glm::vec2* c1, glm::vec2* c2, glm::vec2* p1, uint32_t* state_flags ) {
	if ( *c == 0 )
		return false;

	int       local_offset = 0;
	glm::vec2 previous_p   = {};

	glm::vec2 tmp_c1 = {};
	glm::vec2 tmp_c2 = {};
	glm::vec2 tmp_p1 = {};

	return ( is_repeat_or_command_char( 'c', c, &local_offset, state_flags ) ) && // note this may update state_flags
	       is_optional_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, &tmp_c1 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, &tmp_c2 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, &tmp_p1 ) &&
	       ( ( *state_flags & PATH_PARSER_STATE_FLAG_IS_ABSOLUTE ) || // only set delta_p if not absolute
	         set_coordinate_pair( &previous_p, *p1 ) ) &&             // set delta_p to previous p1
	       set_coordinate_pair( p1, tmp_p1 + previous_p ) &&
	       set_coordinate_pair( c1, tmp_c1 + previous_p ) &&
	       set_coordinate_pair( c2, tmp_c2 + previous_p ) &&
	       add_offsets( local_offset, offset );
}

// Return true if string `c` can be evaluated as a 'q' instruction.
// A 'q' instruction is a quadratic bezier instruction.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*c1` will hold the value of the control point
// + `*p1` will hold the value of the target point
static bool is_q_instruction( char const* c, int* offset, glm::vec2* c1, glm::vec2* p1, uint32_t* state_flags ) {
	if ( *c == 0 )
		return false;

	int       local_offset = 0;
	glm::vec2 previous_p   = {};
	glm::vec2 tmp_c1       = {};
	glm::vec2 tmp_p1       = {};

	return ( is_repeat_or_command_char( 'q', c, &local_offset, state_flags ) ) &&
	       is_optional_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, &tmp_c1 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, &tmp_p1 ) &&
	       ( ( *state_flags & PATH_PARSER_STATE_FLAG_IS_ABSOLUTE ) || // only set delta_p if not absolute
	         set_coordinate_pair( &previous_p, *p1 ) ) &&             // set delta_p to previous p1
	       set_coordinate_pair( p1, tmp_p1 + previous_p ) &&
	       set_coordinate_pair( c1, tmp_c1 + previous_p ) &&
	       add_offsets( local_offset, offset );
}

// Return true if string `c` can be evaluated as a 'a' instruction.
// An 'a' instruction is an arc_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*radii` will hold the value of the radii
// + `*x_axis_rotation` will hold the value of the x axis rotation of the ellipse arc
// + `*large_arc_flag` will hold a flag indicating whether to trace the large arc(true), or the short arc(false)
// + `*sweep_flag` will hold a flag indicating whether to trace the arc in negative direction (true), or in positive direction (false)
// + `*p1` will hold the value of the target point
static bool is_a_instruction( char const* c, int* offset, glm::vec2* radii, float* x_axis_rotation, bool* large_arc_flag, bool* sweep_flag, glm::vec2* p1, uint32_t* state_flags ) {
	if ( *c == 0 )
		return false;

	int       local_offset = 0;
	glm::vec2 previous_p   = {};

	glm::vec2 tmp_p1 = {};

	return ( is_repeat_or_command_char( 'a', c, &local_offset, state_flags ) ) &&
	       is_optional_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, radii ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_float_number( c + local_offset, &local_offset, x_axis_rotation ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_boolean_zero_or_one( c + local_offset, &local_offset, large_arc_flag ) &&
	       is_comma_or_whitespace( c + local_offset, &local_offset ) &&
	       is_boolean_zero_or_one( c + local_offset, &local_offset, sweep_flag ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, &tmp_p1 ) &&
	       ( ( *state_flags & PATH_PARSER_STATE_FLAG_IS_ABSOLUTE ) || // only set delta_p if not absolute
	         set_coordinate_pair( &previous_p, *p1 ) ) &&             // set delta_p to previous p1
	       set_coordinate_pair( p1, tmp_p1 + previous_p ) &&
	       add_offsets( local_offset, offset );
}

// ----------------------------------------------------------------------
// Parse string `svg` for simplified SVG instructions and adds paths
// based on instructions found.
//
// Rules for similified SVG:
//
// - All coordinates must be absolute
// - Commands must be repeated
// - Allowed instruction tokens are:
//	 - 'M', with params {  p        } (moveto),
//	 - 'L', with params {  p        } (lineto),
//	 - 'C', with params { c0, c1, p } (cubic bezier to),
//	 - 'Q', with params { c0,  p,   } (quad bezier to),
//	 - 'Z', with params {           } (close path)
//   - 'A', with params { r, x-rot, large-arc-flag, sweep-flag, p } (arc to)
//
// You may set up Inkscape to output simplified SVG via:
// `Edit -> Preferences -> SVG Output ->
// (tick) Force Repeat Commands, Path string format (select: Absolute)`
//
// The full grammar for SVG paths is defined here:
// <https://svgwg.org/svg2-draft/paths.html#PathDataBNF>
//
static void le_path_add_from_simplified_svg( le_path_o* self, char const* svg ) {

	char const* c = svg;

	glm::vec2 p                 = ( !self->contours.empty() && !self->contours.back().commands.empty() ) ? *le_path_get_previous_p( self ) : glm::vec2{};
	glm::vec2 c1                = {};
	glm::vec2 c2                = {};
	glm::vec2 radii             = {};
	float     arc_axis_rotation = {};
	bool      arc_large{};
	bool      arc_sweep{};

	int offset = 0;

	for ( ; *c != 0; ) // We test for the \0 character, end of c-string
	{

		uint32_t state_flags;
		c += offset;
		offset = 0;

		state_flags = 0;
		while ( is_m_instruction( c + offset, &offset, &p, &state_flags ) ) {
			le_path_move_to( self, &p );
		}
		if ( offset ) {
			continue;
		}

		state_flags = 0;
		while ( is_l_instruction( c + offset, &offset, &p, &state_flags ) ) {
			le_path_line_to( self, &p );
		}
		if ( offset ) {
			continue;
		}

		state_flags = 0;
		while ( is_h_instruction( c + offset, &offset, &p.x, &state_flags ) ) {
			le_path_line_horiz_to( self, p.x );
		}
		if ( offset ) {
			continue;
		}

		state_flags = 0;
		if ( is_v_instruction( c + offset, &offset, &p.y, &state_flags ) ) {
			le_path_line_vert_to( self, p.y );
		}
		if ( offset ) {
			continue;
		}

		state_flags = 0;
		if ( is_c_instruction( c + offset, &offset, &c2, &c1, &p, &state_flags ) ) {
			le_path_cubic_bezier_to( self, &p, &c2, &c1 ); // Note that end vertex is p2 from SVG,
			                                               // as SVG has target vertex as last vertex
		}
		if ( offset ) {
			continue;
		}

		state_flags = 0;
		while ( is_q_instruction( c + offset, &offset, &c1, &p, &state_flags ) ) {
			le_path_quad_bezier_to( self, &p, &c1 ); // Note that target vertex is p1 from SVG,
			                                         // as SVG has target vertex as last vertex
		}
		if ( offset ) {
			continue;
		}

		state_flags = 0;
		while ( is_a_instruction( c + offset, &offset, &radii, &arc_axis_rotation, &arc_large, &arc_sweep, &p, &state_flags ) ) {
			le_path_arc_to( self, &p, &radii, arc_axis_rotation, arc_large, arc_sweep ); // Note that target vertex is p1 from SVG,
		}
		if ( offset ) {
			continue;
		}

		state_flags = 0;
		while ( is_character_match( 'Z', c + offset, &offset ) ||
		        is_character_match( 'z', c + offset, &offset ) ) {
			// close path event.
			le_path_close_path( self );
		}

		if ( offset ) {
			continue;
		}

		// ----------| Invariant: None of the cases above did match

		// If none of the above cases match, the current character is
		// invalid, or does not contribute. Most likely it is a white-
		// space character.

		++c; // Ignore current character.
	}
};

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_path, api ) {
	auto& le_path_i = static_cast<le_path_api*>( api )->le_path_i;

	le_path_i.create          = le_path_create;
	le_path_i.destroy         = le_path_destroy;
	le_path_i.move_to         = le_path_move_to;
	le_path_i.line_to         = le_path_line_to;
	le_path_i.quad_bezier_to  = le_path_quad_bezier_to;
	le_path_i.cubic_bezier_to = le_path_cubic_bezier_to;
	le_path_i.arc_to          = le_path_arc_to;
	le_path_i.close           = le_path_close_path;

	le_path_i.hobby   = le_path_apply_hobby_on_last_contour;
	le_path_i.ellipse = le_path_ellipse;

	le_path_i.add_from_simplified_svg = le_path_add_from_simplified_svg;

	le_path_i.get_num_contours                 = le_path_get_num_contours;
	le_path_i.get_num_polylines                = le_path_get_num_polylines;
	le_path_i.get_vertices_for_polyline        = le_path_get_vertices_for_polyline;
	le_path_i.get_tangents_for_polyline        = le_path_get_tangents_for_polyline;
	le_path_i.get_polyline_at_pos_interpolated = le_path_get_polyline_at_pos_interpolated;

	le_path_i.generate_offset_outline_for_contour = le_path_generate_offset_outline_for_contour;
	le_path_i.tessellate_thick_contour            = le_path_tessellate_thick_contour;

	le_path_i.iterate_vertices_for_contour     = le_path_iterate_vertices_for_contour;
	le_path_i.iterate_quad_beziers_for_contour = le_path_iterate_quad_beziers_for_contour;

	le_path_i.trace    = le_path_trace_path;
	le_path_i.flatten  = le_path_flatten_path;
	le_path_i.resample = le_path_resample;
	le_path_i.clear    = le_path_clear;
}
