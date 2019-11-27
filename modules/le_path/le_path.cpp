#include "le_path.h"
#include "pal_api_loader/ApiRegistry.hpp"
#include "glm.hpp"
#include <vector>

#include <cstring>
#include <cstdio>
#include <cstdlib>

using Vertex = le_path_api::Vertex;

struct PathCommand {

	enum Type : uint32_t {
		eUnknown = 0,
		eMoveTo,
		eLineTo,
		eCurveTo,
		eQuadBezierTo = eCurveTo,
		eCubicBezierTo,
		eClosePath,
	} type = eUnknown;

	Vertex p  = {}; // end point
	Vertex c1 = {}; // control point 1
	Vertex c2 = {}; // control point 2
};

struct Contour {
	std::vector<PathCommand> commands; // svg-style commands+parameters creating the path
};

struct Polyline {
	std::vector<Vertex> vertices;
	std::vector<Vertex> tangents;
	std::vector<float>  distances;
	float               total_distance = 0;
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

struct InflectionData {
	float t_cusp;
	float t_1;
	float t_2;
};

// ----------------------------------------------------------------------

inline static float clamp( float val, float range_min, float range_max ) {
	return val < range_min ? range_min : val > range_max ? range_max : val;
}

// ----------------------------------------------------------------------

inline static float map( float val_, float range_min_, float range_max_, float min_, float max_ ) {
	return clamp( min_ + ( max_ - min_ ) * ( ( clamp( val_, range_min_, range_max_ ) - range_min_ ) / ( range_max_ - range_min_ ) ), min_, max_ );
}

// ----------------------------------------------------------------------

inline static bool is_contained_0_1( float f ) {
	return ( f >= 0.f && f <= 1.f );
}

// ----------------------------------------------------------------------

static le_path_o *le_path_create() {
	auto self = new le_path_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_path_destroy( le_path_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_path_clear( le_path_o *self ) {
	self->contours.clear();
	self->polylines.clear();
}

// ----------------------------------------------------------------------

static void trace_move_to( Polyline &polyline, Vertex const &p ) {
	polyline.distances.emplace_back( 0 );
	polyline.vertices.emplace_back( p );
	// NOTE: we dont insert a tangent here, as we need at least two
	// points to calculate tangents. In an open path, there will be n-1
	// tangent vectors than vertices, closed paths have same number of
	// tangent vectors as vertices.
}

// ----------------------------------------------------------------------

static void trace_line_to( Polyline &polyline, Vertex const &p ) {

	// We must check if the current point is identical with previous point -
	// in which case we will not add this point.

	auto const &p0               = polyline.vertices.back();
	Vertex      relativeMovement = p - p0;

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

static void trace_close_path( Polyline &polyline ) {
	// eClosePath is the same as a direct line to the very first vertex.
	trace_line_to( polyline, polyline.vertices.front() );
}

// ----------------------------------------------------------------------

// Trace a quadratic bezier curve from previous point p0 to target point p2 (p2_x,p2_y),
// controlled by control point p1 (p1_x, p1_y), in steps iterations.
static void trace_quad_bezier_to( Polyline &    polyline,
                                  Vertex const &p1,        // end point
                                  Vertex const &c1,        // control point
                                  size_t        resolution // number of segments
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

	Vertex const &p0     = polyline.vertices.back(); // copy start point
	Vertex        p_prev = p0;

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

		Vertex b = one_minus_t_sq * p0 + 2 * one_minus_t * t * c1 + t_sq * p1;

		polyline.total_distance += glm::distance( b, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );
		p_prev = b;
		polyline.vertices.emplace_back( b );

		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
		polyline.tangents.emplace_back( 2 * one_minus_t * ( c1 - p0 ) + 2 * t * ( p1 - c1 ) );
	}
}

// ----------------------------------------------------------------------
// Trace a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void trace_cubic_bezier_to( Polyline &    polyline,
                                   Vertex const &p1,        // end point
                                   Vertex const &c1,        // control point 1
                                   Vertex const &c2,        // control point 2
                                   size_t        resolution // number of segments
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

	Vertex const p0     = polyline.vertices.back(); // copy start point
	Vertex       p_prev = p0;

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

		Vertex b = one_minus_t_cub * p0 + 3 * one_minus_t_sq * t * c1 + 3 * one_minus_t * t_sq * c2 + t_cub * p1;

		polyline.total_distance += glm::distance( b, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );
		p_prev = b;

		polyline.vertices.emplace_back( b );

		// First derivative with respect to t, see: https://en.m.wikipedia.org/wiki/B%C3%A9zier_curve
		polyline.tangents.emplace_back( 3 * one_minus_t_sq * ( c1 - p0 ) + 6 * one_minus_t * t * ( c2 - c1 ) + 3 * t_sq * ( p1 - c2 ) );
	}
}

// ----------------------------------------------------------------------
// Traces the path with all its subpaths into a list of polylines.
// Each subpath will be translated into one polyline.
// A polyline is a list of vertices which may be thought of being
// connected by lines.
//
static void le_path_trace_path( le_path_o *self, size_t resolution ) {

	self->polylines.clear();
	self->polylines.reserve( self->contours.size() );

	for ( auto const &s : self->contours ) {

		Polyline polyline;

		for ( auto const &command : s.commands ) {

			switch ( command.type ) {
			case PathCommand::eMoveTo:
				trace_move_to( polyline, command.p );
				break;
			case PathCommand::eLineTo:
				trace_line_to( polyline, command.p );
				break;
			case PathCommand::eQuadBezierTo:
				trace_quad_bezier_to( polyline,
				                      command.p,
				                      command.c1,
				                      resolution );
				break;
			case PathCommand::eCubicBezierTo:
				trace_cubic_bezier_to( polyline,
				                       command.p,
				                       command.c1,
				                       command.c2,
				                       resolution );
				break;
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
static void bezier_subdivide( CubicBezier const b, float t, CubicBezier *s_0, CubicBezier *s_1 ) {

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
static bool cubic_bezier_calculate_inflection_points( CubicBezier const &b, InflectionData *infl ) {

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
static void split_cubic_bezier_into_monotonous_sub_segments( CubicBezier &b, std::vector<CubicBezier> &curves, float tolerance ) {
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

	float t1_m, t1_p;
	float t2_m, t2_p;

	auto calc_inflection_point_offsets = []( CubicBezier const &b, float tolerance, float infl, float *infl_m, float *infl_p ) {
		CubicBezier b_sub{};
		bezier_subdivide( b, infl, nullptr, &b_sub );

		glm::vec2 r = glm::normalize( b_sub.c1 - b_sub.p0 );
		glm::vec2 s = {r.y, -r.x};

		// Define a coordinate basis built on the first two points, b0, and b1
		glm::mat2 const basis = {r, s};

		float s3  = 3 * fabsf( ( basis * ( b_sub.p1 - b_sub.p0 ) ).y );
		float t_f = powf( tolerance / s3, 1.f / 3.f ); // cubic root

		*infl_m = infl - t_f * ( 1 - infl );
		*infl_p = infl + t_f * ( 1 - infl );
	};

	calc_inflection_point_offsets( b, tolerance, infl.t_1, &t1_m, &t1_p );
	calc_inflection_point_offsets( b, tolerance, infl.t_2, &t2_m, &t2_p );

	// It's possible that our bezier curve self-intersects through a cusp,
	// in which case inflection points are out of order.

	bool curve_has_cusp = t2_m <= t1_p;
	if ( curve_has_cusp ) {
		std::swap( t1_m, t2_m );
		std::swap( t1_p, t2_p );
	}

	{

		// ----------| invariant: curve does not have a cusp.

		auto which_region = []( float *boundaries, size_t num_boundaries, float marker ) -> size_t {
			size_t i = 0;
			for ( ; i != num_boundaries; i++ ) {
				if ( boundaries[ i ] > marker ) {
					return i;
				}
			}
			return i;
		};
		float boundaries[ 4 ] = {
		    t1_m,
		    t1_p,
		    t2_m,
		    t2_p,
		};

		// Calculate into which of the 5 segments of an infinite cubic bezier
		// the given start and end points (based on t = 0..1 ) fall:
		//
		// ---0--- t1_m ---1--- t1_p ---2--- t2_m ---3--- t2_p ---4---
		//
		size_t c_start = which_region( boundaries, 4, 0.f );
		size_t c_end   = which_region( boundaries, 4, 1.f );

		if ( c_start == c_end ) {
			// curve contained within a single segment.

			// Note segments 1, and 3 are flat, they
			// should better be represented by lines.

			curves.push_back( b );
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
				curves.push_back( b_0 );
			}

			if ( c_end == 1 ) {
				// curve ends within the 1st segment, but does not start here.
				// this means that from t1m to end the curve can be approximated
				// by a straight line.

				bezier_subdivide( b, t1_m, nullptr, &b_0 );
				curves.push_back( b_0 );
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
				bezier_subdivide( b, clamp( t2_p, 0, 1 ), &b_0, nullptr );
				curves.push_back( b_0 );
			}

			if ( c_end == 3 ) {
				bezier_subdivide( b, 0.999f, &b_0, &b_1 ); // equivalent to adding just another point at the end.
				                                           //				curves.push_back( b_0 );
				curves.push_back( b_1 );
			}

			if ( c_end == 4 ) {
				bezier_subdivide( b, t2_p, nullptr, &b_0 );
				curves.push_back( b_0 );
			}
		}
	}
}

// ----------------------------------------------------------------------

static void flatten_cubic_bezier_segment_to( Polyline &         polyline,
                                             CubicBezier const &b_,
                                             float              tolerance ) {

	CubicBezier b = b_; // fixme: not necessary.

	float t = 0;

	glm::vec2 p_prev = b.p0;

	// Note that we limit the number of iterations by setting a maximum of 100 - this
	// should only ever be reached when tolerance is super small.
	for ( int i = 0; i != 100; i++ ) {

		// create a coordinate basis based on the first point, and the first control point
		glm::vec2 r = glm::normalize( b.c1 - b.p0 );
		glm::vec2 s = {r.y, -r.x};

		// Define a coordinate basis built on the first two points, b0, and b1
		glm::mat2 const basis = {r, s};

		glm::vec2 P2 = basis * ( b.c2 - b.p0 );

		float s2 = ( P2 ).y;

		float t_dash = sqrtf( tolerance / ( 3 * fabsf( s2 ) ) );
		t            = std::min<float>( 1.f, 2 * t_dash );

		// Apply subdivision at (t). This means that the start point of the sub-segment
		// will be the point we can add to the polyline while respecting flatness.
		bezier_subdivide( b, t, nullptr, &b );

		glm::vec2 pt = b.p0;

		polyline.vertices.emplace_back( pt );
		polyline.total_distance += glm::distance( pt, p_prev );
		polyline.distances.emplace_back( polyline.total_distance );

		polyline.tangents.emplace_back(); // todo: add tangent

		if ( t >= 1.0f )
			break;

		p_prev = pt;
	}
}

// ----------------------------------------------------------------------
// Flatten a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void flatten_cubic_bezier_to( Polyline &    polyline,
                                     Vertex const &p1,       // end point
                                     Vertex const &c1,       // control point 1
                                     Vertex const &c2,       // control point 2
                                     float         tolerance // max distance for arc segment
) {

	assert( !polyline.vertices.empty() ); // Contour vertices must not be empty.

	Vertex const p0 = polyline.vertices.back(); // copy start point

	CubicBezier b{
	    p0,
	    c1,
	    c2,
	    p1,
	};

	std::vector<CubicBezier> curves;

	split_cubic_bezier_into_monotonous_sub_segments( b, curves, tolerance );

	// ---
	for ( auto &c : curves ) {
		flatten_cubic_bezier_segment_to( polyline, c, tolerance );
	}
}

// ----------------------------------------------------------------------

static void le_path_flatten_path( le_path_o *self, float tolerance ) {

	self->polylines.clear();
	self->polylines.reserve( self->contours.size() );

	for ( auto const &s : self->contours ) {

		Polyline polyline;

		glm::vec2 prev_point = {};

		for ( auto const &command : s.commands ) {

			switch ( command.type ) {
			case PathCommand::eMoveTo:
				trace_move_to( polyline, command.p );
				prev_point = command.p;
				break;
			case PathCommand::eLineTo:
				trace_line_to( polyline, command.p );
				prev_point = command.p;
				break;
			case PathCommand::eQuadBezierTo:
				flatten_cubic_bezier_to( polyline,
				                         command.p,
				                         prev_point + 2 / 3.f * ( command.c1 - prev_point ),
				                         command.c2 + 2 / 3.f * ( command.c1 - command.c2 ),
				                         tolerance );
				prev_point = command.p;
				break;
			case PathCommand::eCubicBezierTo:
				flatten_cubic_bezier_to( polyline,
				                         command.p,
				                         command.c1,
				                         command.c2,
				                         tolerance );
				prev_point = command.p;
				break;
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

static void generate_offset_outline_line_to( std::vector<glm::vec2> &outline, Vertex const &p0, Vertex const &p1, float offset ) {

	if ( p1 == p0 ) {
		return;
	}

	glm::vec2 r = glm::normalize( p1 - p0 );
	glm::vec2 s = {r.y, -r.x};

	outline.push_back( p0 + offset * s );

	auto p = p1 + offset * s;

	outline.push_back( p );
}

// ----------------------------------------------------------------------

static void flatten_cubic_bezier_segment_to( std::vector<glm::vec2> &outline,
                                             CubicBezier const &     b_,
                                             float                   tolerance,
                                             float                   offset ) {

	CubicBezier b = b_;

	float determinant = dot( b.p1, {-b.p0.y, b.p0.x} );
	if ( fabsf( determinant ) <= std::numeric_limits<float>::epsilon() ) {
		// start point equals end point - we must not consider this curve segment.
		return;
	}

	float t = 0;

	// Prepare for a coordinate basis based on the first point, and the first control point
	glm::vec2 r = glm::normalize( b.c1 - b.p0 );
	glm::vec2 s = {r.y, -r.x};

	glm::vec2 pt = b.p0 + offset * s;

	outline.emplace_back( pt );

	for ( int i = 0; i < 1000; i++ ) {

		// Define a coordinate basis built on the first two points, b0, and b1
		glm::mat2 const basis = {r, s};

		glm::vec2 P1 = basis * ( b.c1 - b.p0 );
		glm::vec2 P2 = basis * ( b.c2 - b.p0 );

		float s2 = P2.y;
		float r1 = P1.x;

		float x      = ( 1 - ( offset * s2 / ( 3 * r1 * r1 ) ) );
		float t_dash = sqrtf( tolerance / fabsf( 3 * s2 * std::max<float>( x, tolerance * tolerance ) ) );

		t = std::min<float>( 1.f, t_dash );

		// Apply subdivision at (t). This means that the start point of the sub-segment
		// will be the point we can add to the polyline while respecting flatness.
		bezier_subdivide( b, t, nullptr, &b );

		// update the coordinate basis based on the first point, and the first control point

		if ( t < 1.f ) {
			r = glm::normalize( b.c1 - b.p0 );
			s = {r.y, -r.x};
		}

		pt = b.p0 + offset * s;

		if ( x > 0 ) {
			outline.emplace_back( pt );
		}

		if ( t >= 1.0f )
			break;
	}
}

// ----------------------------------------------------------------------

static void generate_offset_outline_cubic_bezier_to( std::vector<glm::vec2> &outline_l,
                                                     std::vector<glm::vec2> &outline_r,
                                                     glm::vec2 const &       p0, // start point
                                                     glm::vec2 const &       c1, // control point 1
                                                     glm::vec2 const &       c2, // control point 2
                                                     glm::vec2 const &       p1, // end point
                                                     float                   tolerance,
                                                     float                   line_weight ) {

	CubicBezier b{
	    p0,
	    c1,
	    c2,
	    p1,
	};

	std::vector<CubicBezier> curves;

	split_cubic_bezier_into_monotonous_sub_segments( b, curves, tolerance );

	// ---
	for ( auto &c : curves ) {
		flatten_cubic_bezier_segment_to( outline_l, c, tolerance, -line_weight * 0.5f );
		flatten_cubic_bezier_segment_to( outline_r, c, tolerance, line_weight * 0.5f );
	}
}

// ----------------------------------------------------------------------

static void generate_offset_outline_close_path( std::vector<glm::vec2> &outline ) {
	// We need to have at least 3 elements in outline to be able to close a path.
	// If so, we duplicate the first point as a last point.
	if ( outline.size() > 2 ) {
		outline.push_back( outline.front() );
	}
}

// ----------------------------------------------------------------------
// generate vertices for path outline by flattening first left, then right outline
// based on the t.f. hain paper for bezier curve outlines.
static bool le_path_generate_offset_outline_for_contour(
    le_path_o *self, size_t contour_index,
    float   line_weight,
    float   tolerance,
    Vertex *outline_l_, size_t *max_count_outline_l,
    Vertex *outline_r_, size_t *max_count_outline_r ) {

	// We allocate space internally to store the results of our outline generation.
	// We do this because if we were to directly write back to the caller, we would
	// have to bounds-check against `max_count_outline[l|r]` on every write.
	//
	// This way we do the bounds-check only at the very end, and if the bounds check
	// fails, we can at least tell the caller how many elements to reserve next time.

	std::vector<glm::vec2> outline_l;
	std::vector<glm::vec2> outline_r;

	outline_l.reserve( *max_count_outline_l );
	outline_r.reserve( *max_count_outline_r );

	// Now process the commands for this contour

	glm::vec2 prev_point  = {};
	float     line_offset = line_weight * 0.5f;

	auto &s = self->contours[ contour_index ];
	for ( auto const &command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:
			prev_point = command.p;
			break;
		case PathCommand::eLineTo:
			generate_offset_outline_line_to( outline_l, prev_point, command.p, -line_offset );
			generate_offset_outline_line_to( outline_r, prev_point, command.p, line_offset );
			prev_point = command.p;
			break;
		case PathCommand::eQuadBezierTo:
			generate_offset_outline_cubic_bezier_to( outline_l,
			                                         outline_r,
			                                         prev_point,
			                                         prev_point + 2 / 3.f * ( command.c1 - prev_point ),
			                                         command.c2 + 2 / 3.f * ( command.c1 - command.c2 ),
			                                         command.p,
			                                         tolerance,
			                                         line_weight );

			prev_point = command.p;
			break;
		case PathCommand::eCubicBezierTo:
			generate_offset_outline_cubic_bezier_to( outline_l,
			                                         outline_r,
			                                         prev_point,
			                                         command.c1,
			                                         command.c2,
			                                         command.p,
			                                         tolerance,
			                                         line_weight );
			prev_point = command.p;
			break;
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
		memcpy( outline_l_, outline_l.data(), sizeof( Vertex ) * outline_l.size() );
	} else {
		success = false;
	}

	if ( outline_r_ && outline_r.size() <= *max_count_outline_r ) {
		memcpy( outline_r_, outline_r.data(), sizeof( Vertex ) * outline_r.size() );
	} else {
		success = false;
	}

	// update outline counts with actual number of generated vertices.
	*max_count_outline_l = outline_l.size();
	*max_count_outline_r = outline_r.size();

	return success;
}
// ----------------------------------------------------------------------

static void le_path_iterate_vertices_for_contour( le_path_o *self, size_t const &contour_index, le_path_api::contour_vertex_cb callback, void *user_data ) {

	assert( self->contours.size() > contour_index );

	auto const &s = self->contours[ contour_index ];

	for ( auto const &command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:        // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eLineTo:        // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eQuadBezierTo:  // fall-through, as we're allways just issueing the vertex, ignoring control points
		case PathCommand::eCubicBezierTo: // fall-through, as we're allways just issueing the vertex, ignoring control points
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

static void le_path_iterate_quad_beziers_for_contour( le_path_o *self, size_t const &contour_index, le_path_api::contour_quad_bezier_cb callback, void *user_data ) {

	assert( self->contours.size() > contour_index );

	auto const &s = self->contours[ contour_index ];

	Vertex p0 = {};

	for ( auto const &command : s.commands ) {

		switch ( command.type ) {
		case PathCommand::eMoveTo:
			p0 = command.p;
			break;
		case PathCommand::eLineTo:
			p0 = command.p;
			break;
		case PathCommand::eQuadBezierTo:
			callback( user_data, p0, command.p, command.c1 );
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
static void le_polyline_get_at( Polyline const &polyline, float t, Vertex &result ) {

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

	Vertex const &start_vertex = polyline.vertices[ a ];
	Vertex const &end_vertex   = polyline.vertices[ b ];

	result = start_vertex + scalar * ( end_vertex - start_vertex );
}

// ----------------------------------------------------------------------
// return calculated position on polyline
static void le_path_get_polyline_at_pos_interpolated( le_path_o *self, size_t const &polyline_index, float t, Vertex &result ) {
	assert( polyline_index < self->polylines.size() );
	le_polyline_get_at( self->polylines[ polyline_index ], t, result );
}

// ----------------------------------------------------------------------

static void le_polyline_resample( Polyline &polyline, float interval ) {
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
	Vertex vertex;
	le_polyline_get_at( polyline, 0.f, vertex );
	trace_move_to( poly_resampled, vertex );

	// Note that we must add an extra vertex at the end so that we
	// capture the correct number of segments.
	for ( size_t i = 1; i <= n_segments; ++i ) {
		le_polyline_get_at( polyline, i * delta, vertex );
		// We use trace_line_to, because this will get us more accurate distance
		// calculations - trace_line_to updates the distances as a side-effect,
		// effectively redrawing the polyline as if it was a series of `line_to`s.
		trace_line_to( poly_resampled, vertex );
	}

	std::swap( polyline, poly_resampled );
}

// ----------------------------------------------------------------------

static void le_path_resample( le_path_o *self, float interval ) {

	if ( self->contours.empty() ) {
		// nothing to do.
		return;
	}

	// --------| invariant: subpaths exist

	if ( self->polylines.empty() ) {
		le_path_trace_path( self, 100 ); // We must trace path - we will do it at a fairy high resolution.
	}

	// Resample each polyline, turn by turn

	for ( auto &p : self->polylines ) {
		le_polyline_resample( p, interval );
		// -- Enforce invariant that says for closed paths:
		// First and last vertex must be identical.
	}
}

// ----------------------------------------------------------------------

static void le_path_move_to( le_path_o *self, Vertex const *p ) {
	// move_to means a new subpath, unless the last command was a
	self->contours.emplace_back(); // add empty subpath
	self->contours.back().commands.push_back( {PathCommand::eMoveTo, *p} );
}

// ----------------------------------------------------------------------

static void le_path_line_to( le_path_o *self, Vertex const *p ) {
	if ( self->contours.empty() ) {
		constexpr static auto v0 = Vertex{};
		le_path_move_to( self, &v0 );
	}
	assert( !self->contours.empty() ); //subpath must exist
	self->contours.back().commands.push_back( {PathCommand::eLineTo, *p} );
}

// ----------------------------------------------------------------------

// Fetch the current pen point by grabbing the previous target point
// from the command stream.
static Vertex const *le_path_get_previous_p( le_path_o *self ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	Vertex const *p = nullptr;

	auto const &c = self->contours.back().commands.back(); // fetch last command

	switch ( c.type ) {
	case PathCommand::eMoveTo:        // fall-through
	case PathCommand::eLineTo:        // fall-through
	case PathCommand::eQuadBezierTo:  // fall-through
	case PathCommand::eCubicBezierTo: // fall-through
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

static void le_path_line_horiz_to( le_path_o *self, float px ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		Vertex p2 = *p;
		p2.x      = px;
		le_path_line_to( self, &p2 );
	}
}

// ----------------------------------------------------------------------

static void le_path_line_vert_to( le_path_o *self, float py ) {
	assert( !self->contours.empty() );                 // Subpath must exist
	assert( !self->contours.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		Vertex p2 = *p;
		p2.y      = py;
		le_path_line_to( self, &p2 );
	}
}

// ----------------------------------------------------------------------

static void le_path_quad_bezier_to( le_path_o *self, Vertex const *p, Vertex const *c1 ) {
	assert( !self->contours.empty() ); //contour must exist
	self->contours.back().commands.push_back( {PathCommand::eQuadBezierTo, *p, *c1} );
}

// ----------------------------------------------------------------------

static void le_path_cubic_bezier_to( le_path_o *self, Vertex const *p, Vertex const *c1, Vertex const *c2 ) {
	assert( !self->contours.empty() ); //subpath must exist
	self->contours.back().commands.push_back( {PathCommand::eCubicBezierTo, *p, *c1, *c2} );
}

// ----------------------------------------------------------------------

static void le_path_close_path( le_path_o *self ) {
	self->contours.back().commands.push_back( {PathCommand::eClosePath} );
}

// ----------------------------------------------------------------------

static size_t le_path_get_num_polylines( le_path_o *self ) {
	return self->polylines.size();
}

static size_t le_path_get_num_contours( le_path_o *self ) {
	return self->contours.size();
}

// ----------------------------------------------------------------------

static void le_path_get_vertices_for_polyline( le_path_o *self, size_t const &polyline_index, Vertex const **vertices, size_t *numVertices ) {
	assert( polyline_index < self->polylines.size() );

	auto const &polyline = self->polylines[ polyline_index ];

	*vertices    = polyline.vertices.data();
	*numVertices = polyline.vertices.size();
}

// ----------------------------------------------------------------------

static void le_path_get_tangents_for_polyline( le_path_o *self, size_t const &polyline_index, Vertex const **tangents, size_t *numTangents ) {
	assert( polyline_index < self->polylines.size() );

	auto const &polyline = self->polylines[ polyline_index ];

	*tangents    = polyline.tangents.data();
	*numTangents = polyline.tangents.size();
}

// ----------------------------------------------------------------------

// Accumulates `*offset_local` into `*offset_total`.
// Always returns true.
static bool add_offsets( int *offset_local, int *offset_total ) {
	( *offset_total ) += ( *offset_local );
	return true;
}

// Returns true if string c may be interpreted as
// a number,
// If true,
// + increases *offset by the count of characters forming the number.
// + sets *f to value of parsed number.
//
static bool is_float_number( char const *c, int *offset, float *f ) {
	if ( *c == 0 )
		return false;

	char *num_end;

	*f = strtof( c, &num_end ); // num_end will point to one after last number character
	*offset += ( num_end - c ); // add number of number characters to offset

	return num_end != c; // if at least one number character was extracted, we were successful
}

// Returns true if needle matches c.
// Increases *offset by 1 if true.
static bool is_character_match( char const needle, char const *c, int *offset ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	if ( *c == needle ) {
		++( *offset );
		return true;
	} else {
		return false;
	}
}

// Returns true if what c points to may be interpreted as
// whitespace, and sets offset to the count of whitespace
// characters.
static bool is_whitespace( char const *c, int *offset ) {
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

// Returns true if c points to a coordinate pair.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*coord` will hold the vertex defined by the coordinate pair
static bool is_coordinate_pair( char const *c, int *offset, Vertex *v ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	// we want the pattern:

	int local_offset = 0;

	return is_float_number( c, &local_offset, &v->x ) &&                 // note how offset is re-used
	       is_character_match( ',', c + local_offset, &local_offset ) && // in subsequent tests, so that
	       is_float_number( c + local_offset, &local_offset, &v->y ) &&  // each test begins at the previous offset
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'm' instruction.
// An 'm' instruction is a move_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the target point
static bool is_m_instruction( char const *c, int *offset, Vertex *p0 ) {
	if ( *c == 0 ) {
		return false;
	}
	// ---------| invarant: c is not end of string

	int local_offset = 0;

	return is_character_match( 'M', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'l' instruction.
// An 'l' instruction is a line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the target point
static bool is_l_instruction( char const *c, int *offset, Vertex *p0 ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'L', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'h' instruction.
// A 'h' instruction is a horizontal line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*px` will hold the value of the target point's x coordinate
static bool is_h_instruction( char const *c, int *offset, float *px ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'H', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_float_number( c + local_offset, &local_offset, px ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as an 'l' instruction.
// A 'v' instruction is a vertical line_to instruction
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*px` will hold the value of the target point's x coordinate
static bool is_v_instruction( char const *c, int *offset, float *py ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'V', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_float_number( c + local_offset, &local_offset, py ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as a 'c' instruction.
// A 'c' instruction is a cubic bezier instruction.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of control point 0
// + `*p1` will hold the value of control point 1
// + `*p2` will hold the value of the target point
static bool is_c_instruction( char const *c, int *offset, Vertex *p0, Vertex *p1, Vertex *p2 ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'C', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p1 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p2 ) &&
	       add_offsets( &local_offset, offset );
}

// Return true if string `c` can be evaluated as a 'q' instruction.
// A 'q' instruction is a quadratic bezier instruction.
// In case this method returns true,
// + `*offset` will hold the count of characters from `c` spent on the instruction.
// + `*p0` will hold the value of the control point
// + `*p1` will hold the value of the target point
static bool is_q_instruction( char const *c, int *offset, Vertex *p0, Vertex *p1 ) {
	if ( *c == 0 )
		return false;

	int local_offset = 0;

	return is_character_match( 'Q', c, &local_offset ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p0 ) &&
	       is_whitespace( c + local_offset, &local_offset ) &&
	       is_coordinate_pair( c + local_offset, &local_offset, p1 ) &&
	       add_offsets( &local_offset, offset );
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
//
// You may set up Inkscape to output simplified SVG via:
// `Edit -> Preferences -> SVG Output ->
// (tick) Force Repeat Commands, Path string format (select: Absolute)`
//
static void le_path_add_from_simplified_svg( le_path_o *self, char const *svg ) {

	char const *c = svg;

	Vertex p0 = {};
	Vertex p1 = {};
	Vertex p2 = {};

	for ( ; *c != 0; ) // We test for the \0 character, end of c-string
	{

		int offset = 0;

		if ( is_m_instruction( c, &offset, &p0 ) ) {
			// moveto event
			le_path_move_to( self, &p0 );
			c += offset;
			continue;
		}
		if ( is_l_instruction( c, &offset, &p0 ) ) {
			// lineto event
			le_path_line_to( self, &p0 );
			c += offset;
			continue;
		}
		if ( is_h_instruction( c, &offset, &p0.x ) ) {
			// lineto event
			le_path_line_horiz_to( self, p0.x );
			c += offset;
			continue;
		}
		if ( is_v_instruction( c, &offset, &p0.y ) ) {
			// lineto event
			le_path_line_vert_to( self, p0.y );
			c += offset;
			continue;
		}
		if ( is_c_instruction( c, &offset, &p0, &p1, &p2 ) ) {
			// cubic bezier event
			le_path_cubic_bezier_to( self, &p2, &p0, &p1 ); // Note that end vertex is p2 from SVG,
			                                                // as SVG has target vertex as last vertex
			c += offset;
			continue;
		}
		if ( is_q_instruction( c, &offset, &p0, &p1 ) ) {
			// quadratic bezier event
			le_path_quad_bezier_to( self, &p1, &p0 ); // Note that target vertex is p1 from SVG,
			                                          // as SVG has target vertex as last vertex
			c += offset;
			continue;
		}
		if ( is_character_match( 'Z', c, &offset ) ) {
			// close path event.
			le_path_close_path( self );
			c += offset;
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

ISL_API_ATTR void register_le_path_api( void *api ) {
	auto &le_path_i = static_cast<le_path_api *>( api )->le_path_i;

	le_path_i.create                  = le_path_create;
	le_path_i.destroy                 = le_path_destroy;
	le_path_i.move_to                 = le_path_move_to;
	le_path_i.line_to                 = le_path_line_to;
	le_path_i.quad_bezier_to          = le_path_quad_bezier_to;
	le_path_i.cubic_bezier_to         = le_path_cubic_bezier_to;
	le_path_i.close                   = le_path_close_path;
	le_path_i.add_from_simplified_svg = le_path_add_from_simplified_svg;

	le_path_i.get_num_contours                 = le_path_get_num_contours;
	le_path_i.get_num_polylines                = le_path_get_num_polylines;
	le_path_i.get_vertices_for_polyline        = le_path_get_vertices_for_polyline;
	le_path_i.get_tangents_for_polyline        = le_path_get_tangents_for_polyline;
	le_path_i.get_polyline_at_pos_interpolated = le_path_get_polyline_at_pos_interpolated;

	le_path_i.generate_offset_outline_for_contour = le_path_generate_offset_outline_for_contour;

	le_path_i.iterate_vertices_for_contour     = le_path_iterate_vertices_for_contour;
	le_path_i.iterate_quad_beziers_for_contour = le_path_iterate_quad_beziers_for_contour;

	le_path_i.trace    = le_path_trace_path;
	le_path_i.flatten  = le_path_flatten_path;
	le_path_i.resample = le_path_resample;
	le_path_i.clear    = le_path_clear;
}
