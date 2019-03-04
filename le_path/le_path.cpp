#include "le_path.h"
#include "pal_api_loader/ApiRegistry.hpp"
#include "glm/vec2.hpp"
#include <vector>

#include <cstring>
#include <cstdio>
#include <cstdlib>

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
                                  Vertex const &       p1,        // end point
                                  Vertex const &       c1,        // control point
                                  size_t               resolution // number of segments
) {

	if ( resolution == 0 ) {
		// nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		polyline.emplace_back( p1 );
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

		Vertex b = one_minus_t_sq * p0 + 2 * one_minus_t * t * c1 + t_sq * p1;

		polyline.emplace_back( b );
	}
}

// ----------------------------------------------------------------------
// Trace a cubic bezier curve from previous point p0 to target point p3
// controlled by control points p1, and p2.
static void trace_cubic_bezier_to( std::vector<Vertex> &polyline,
                                   Vertex const &       p1,        // end point
                                   Vertex const &       c1,        // control point 1
                                   Vertex const &       c2,        // control point 2
                                   size_t               resolution // number of segments
) {
	if ( resolution == 0 ) {
		// Nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		polyline.emplace_back( p1 );
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

		Vertex b = one_minus_t_cub * p0 + 3 * one_minus_t_sq * t * c1 + 3 * one_minus_t * t_sq * c2 + t_cub * p1;

		polyline.emplace_back( b );
	}
}

// ----------------------------------------------------------------------
// Traces the path with all its subpaths into a list of polylines.
// Each subpath will be translated into one polyline.
// A polyline is a list of vertices which may be thought of being
// connected by lines.
//
static void le_path_trace_path( le_path_o *self ) {
	self->polylines.clear();
	self->polylines.reserve( self->subpaths.size() );

	constexpr size_t resolution = 12; // Curves sample resolution

	for ( auto const &s : self->subpaths ) {

		std::vector<Vertex> polyline;

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

		self->polylines.emplace_back( polyline );
	}
}

// ----------------------------------------------------------------------

static void le_path_move_to( le_path_o *self, Vertex const &p ) {
	// move_to means a new subpath, unless the last command was a
	self->subpaths.emplace_back(); // add empty subpath
	self->subpaths.back().commands.push_back( {PathCommand::eMoveTo, p} );
}

// ----------------------------------------------------------------------

static void le_path_line_to( le_path_o *self, Vertex const &p ) {
	assert( !self->subpaths.empty() ); //subpath must exist
	self->subpaths.back().commands.push_back( {PathCommand::eLineTo, p} );
}

// Fetch the current pen point by grabbing the previous target point
// from the command stream.
Vertex const *le_path_get_previous_p( le_path_o *self ) {
	assert( !self->subpaths.empty() );                 // Subpath must exist
	assert( !self->subpaths.back().commands.empty() ); // previous command must exist

	Vertex const *p = nullptr;

	auto const &c = self->subpaths.back().commands.back(); // fetch last command

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

static void le_path_line_horiz_to( le_path_o *self, float const &px ) {
	assert( !self->subpaths.empty() );                 // Subpath must exist
	assert( !self->subpaths.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		le_path_line_to( self, {px, p->y} );
	}
}

// ----------------------------------------------------------------------

static void le_path_line_vert_to( le_path_o *self, float const &py ) {
	assert( !self->subpaths.empty() );                 // Subpath must exist
	assert( !self->subpaths.back().commands.empty() ); // previous command must exist

	auto p = le_path_get_previous_p( self );

	if ( p ) {
		le_path_line_to( self, {p->x, py} );
	}
}

// ----------------------------------------------------------------------

static void le_path_quad_bezier_to( le_path_o *self, Vertex const &p, Vertex const &c1 ) {
	assert( !self->subpaths.empty() ); //subpath must exist
	self->subpaths.back().commands.push_back( {PathCommand::eQuadBezierTo, p, c1} );
}

// ----------------------------------------------------------------------

static void le_path_cubic_bezier_to( le_path_o *self, Vertex const &p, Vertex const &c1, Vertex const &c2 ) {
	assert( !self->subpaths.empty() ); //subpath must exist
	self->subpaths.back().commands.push_back( {PathCommand::eCubicBezierTo, p, c1, c2} );
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
			le_path_move_to( self, p0 );
			c += offset;
			continue;
		}
		if ( is_l_instruction( c, &offset, &p0 ) ) {
			// lineto event
			le_path_line_to( self, p0 );
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
			le_path_cubic_bezier_to( self, p2, p0, p1 ); // Note that end vertex is p2 from SVG,
			                                             // as SVG has target vertex as last vertex
			c += offset;
			continue;
		}
		if ( is_q_instruction( c, &offset, &p0, &p1 ) ) {
			// quadratic bezier event
			le_path_quad_bezier_to( self, p1, p0 ); // Note that target vertex is p1 from SVG,
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
	le_path_i.close_path              = le_path_close_path;
	le_path_i.add_from_simplified_svg = le_path_add_from_simplified_svg;

	le_path_i.get_num_polylines         = le_path_get_num_polylines;
	le_path_i.get_vertices_for_polyline = le_path_get_vertices_for_polyline;
	le_path_i.trace_path                = le_path_trace_path;
}
