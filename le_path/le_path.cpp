#include "le_path.h"
#include "pal_api_loader/ApiRegistry.hpp"
#include "glm/vec2.hpp"
#include <vector>

#include <cstring>

using Vertex = glm::vec2;

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

	uint32_t isRelative{}; // bool signaling whether coordinates are relative to the last pen point or absolute.

	Vertex p  = {}; // end point
	Vertex c1 = {}; // control point 1
	Vertex c2 = {}; // control point 2
};

struct SubPath {
	std::vector<PathCommand> commands; // svg-style commands+parameters creating the path
};

struct le_path_o {
	std::vector<SubPath>             subpaths;  // an array of sub-paths, a subpath must start with a moveto instruction
	std::vector<std::vector<Vertex>> polylines; // an array of polylines, each corresponding to a sub-path.
};

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

static void trace_move_to( std::vector<Vertex> &polyline, Vertex const &p ) {
	polyline.emplace_back( p );
}

// ----------------------------------------------------------------------

static void trace_line_to( std::vector<Vertex> &polyline, Vertex const &p ) {
	polyline.emplace_back( p );
}

// ----------------------------------------------------------------------

static void trace_close_path( std::vector<Vertex> &polyline ) {
	// eClosePath is the same as a direct line to the very first vertex.
	trace_line_to( polyline, polyline.front() );
}

// ----------------------------------------------------------------------

// Trace a quadratic bezier curve from previous point p0 to target point p2 (p2_x,p2_y),
// controlled by control point p1 (p1_x, p1_y), in steps iterations.
static void trace_quad_bezier_to( std::vector<Vertex> &polyline,
                                  Vertex const &       p2,        // end point
                                  Vertex const &       p1,        // control point
                                  size_t               resolution // number of segments
) {

	if ( resolution == 0 ) {
		// nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		polyline.emplace_back( p2 );
		return;
	}

	// --------| invariant: resolution > 1

	polyline.reserve( polyline.size() + resolution );

	assert( !polyline.empty() ); // Contour vertices must not be empty.

	auto const &p0 = polyline.back(); // start points

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

		Vertex b = one_minus_t_sq * p0 + 2 * one_minus_t * t * p1 + t_sq * p2;

		polyline.emplace_back( b );
	}
}

// ----------------------------------------------------------------------
// Trace a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void trace_cubic_bezier_to( std::vector<Vertex> &polyline,
                                   Vertex const &       p3,        // end point
                                   Vertex const &       p1,        // control point 1
                                   Vertex const &       p2,        // control point 2
                                   size_t               resolution // number of segments
) {
	if ( resolution == 0 ) {
		// Nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		polyline.emplace_back( p3 );
		return;
	}

	// --------| invariant: resolution > 1

	polyline.reserve( polyline.size() + resolution );

	assert( !polyline.empty() ); // Contour vertices must not be empty.

	auto const &p0 = polyline.back();

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

		Vertex b = one_minus_t_cub * p0 + 3 * one_minus_t_sq * t * p1 + 3 * one_minus_t * t_sq * p2 + t_cub * p3;

		polyline.emplace_back( b );
	}
}

// ----------------------------------------------------------------------
// Traces the path with all its subpaths into a list of polylines.
// Each subpath will be translated into one polyline.
// A polyline is a list of vertices which may be thought of being
// connected by lines.
//
//
static void le_path_trace_path( le_path_o *self ) {
	self->polylines.clear();
	self->polylines.reserve( self->subpaths.size() );

	constexpr size_t resolution = 12; // Curves sample resolution
	Vertex           penPos{};        // pen position state

	for ( auto const &s : self->subpaths ) {

		std::vector<Vertex> polyline;

		for ( auto const &command : s.commands ) {

			Vertex offset = command.isRelative ? penPos : Vertex{0};

			switch ( command.type ) {
			case PathCommand::eMoveTo:
				trace_move_to( polyline, command.p + offset );
			    break;
			case PathCommand::eLineTo:
				trace_line_to( polyline, command.p + offset );
			    break;
			case PathCommand::eQuadBezierTo:
				trace_quad_bezier_to( polyline, command.p + offset, command.c1 + offset, resolution );
			    break;
			case PathCommand::eCubicBezierTo:
				trace_cubic_bezier_to( polyline, command.p + offset, command.c1 + offset, command.c2, resolution );
			    break;
			case PathCommand::eClosePath:
				trace_close_path( polyline );
			    break;
			case PathCommand::eUnknown:
				assert( false );
			    break;
			}

			// Set pen position to last added point.
			penPos = polyline.back();
		}

		self->polylines.emplace_back( polyline );
	}
}

// ----------------------------------------------------------------------

static void le_path_move_to( le_path_o *self, Vertex const &p, bool isRelative ) {
	// move_to means a new subpath, unless the last command was a
	self->subpaths.emplace_back(); // add empty subpath
	self->subpaths.back().commands.push_back( {PathCommand::eMoveTo, isRelative, p} );
}

// ----------------------------------------------------------------------

static void le_path_line_to( le_path_o *self, Vertex const &p, bool isRelative ) {
	assert( !self->subpaths.empty() ); //subpath must exist
	self->subpaths.back().commands.push_back( {PathCommand::eLineTo, isRelative, p} );
}

// ----------------------------------------------------------------------

static void le_path_quad_bezier_to( le_path_o *self, Vertex const &p, Vertex const &c1, bool isRelative ) {
	assert( !self->subpaths.empty() ); //subpath must exist
	self->subpaths.back().commands.push_back( {PathCommand::eQuadBezierTo, isRelative, p, c1} );
}

// ----------------------------------------------------------------------

static void le_path_cubic_bezier_to( le_path_o *self, Vertex const &p, Vertex const &c1, Vertex const &c2, bool isRelative ) {
	assert( !self->subpaths.empty() ); //subpath must exist
	self->subpaths.back().commands.push_back( {PathCommand::eCubicBezierTo, isRelative, p, c1, c2} );
}

// ----------------------------------------------------------------------

static void le_path_close_path( le_path_o *self ) {
	self->subpaths.back().commands.push_back( {PathCommand::eClosePath} );
}

// ----------------------------------------------------------------------

static size_t le_path_get_num_polylines( le_path_o *self ) {
	return self->polylines.size();
}

// ----------------------------------------------------------------------

static void le_path_get_vertices_for_polyline( le_path_o *self, size_t const &polyline_index, Vertex const **vertices, size_t *numVertices ) {
	assert( polyline_index < self->polylines.size() );

	auto const &polyline = self->polylines[ polyline_index ];

	*vertices    = polyline.data();
	*numVertices = polyline.size();
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_path_api( void *api ) {
	auto &le_path_i = static_cast<le_path_api *>( api )->le_path_i;

	le_path_i.create          = le_path_create;
	le_path_i.destroy         = le_path_destroy;
	le_path_i.move_to         = le_path_move_to;
	le_path_i.line_to         = le_path_line_to;
	le_path_i.quad_bezier_to  = le_path_quad_bezier_to;
	le_path_i.cubic_bezier_to = le_path_cubic_bezier_to;
	le_path_i.close_path      = le_path_close_path;

	le_path_i.get_num_polylines         = le_path_get_num_polylines;
	le_path_i.get_vertices_for_polyline = le_path_get_vertices_for_polyline;
	le_path_i.trace_path                = le_path_trace_path;
}
