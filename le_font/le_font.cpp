#include "le_font.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/stb_truetype.h"

#include <vector>
#include <fstream>
#include "experimental/filesystem" // for parsing shader source file paths
#include <iostream>
#include <assert.h>

namespace std {
using namespace experimental;
}

struct le_font_o {
	// members
	stbtt_fontinfo             info;
	std::vector<unsigned char> data; // ttf file data
};

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path &file_path, bool *success ) {

	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		std::cerr << "Unable to open file: " << std::filesystem::canonical( file_path ) << std::endl
		          << std::flush;
		*success = false;
		return contents;
	}

	//	std::cout << "OK Opened file:" << std::filesystem::canonical( file_path ) << std::endl
	//	          << std::flush;

	// ----------| invariant: file is open

	auto endOfFilePos = file.tellg();

	if ( endOfFilePos > 0 ) {
		fileSize = size_t( endOfFilePos );
	} else {
		*success = false;
		return contents;
	}

	// ----------| invariant: file has some bytes to read
	contents.resize( fileSize );

	file.seekg( 0, std::ios::beg );
	file.read( contents.data(), endOfFilePos );
	file.close();

	*success = true;
	return contents;
}

struct Vertex {
	float x;
	float y;
	float z;
};

struct Contour {
	// closed loop of vertices.
	std::vector<Vertex> vertices;
};

struct Shape {
	// a series of contours
	std::vector<Contour> contours;
};

// ----------------------------------------------------------------------

void contour_move_to( Contour &c, float const &x, float const &y ) {
	c.vertices.push_back( {x, y, 0.f} );
}

// ----------------------------------------------------------------------

void contour_line_to( Contour &c, float const &x, float const &y ) {
	c.vertices.push_back( {x, y, 0.f} );
}

// ----------------------------------------------------------------------

// Trace a quadratic bezier curve from previous point p0 to target point p2 (p2_x,p2_y),
// controlled by control point p1 (p1_x, p1_y), in steps iterations.
void contour_curve_to( Contour &     c,
                       double const &p2_x,      // end point
                       double const &p2_y,      // end point
                       double const &p1_x,      // control point
                       double const &p1_y,      // control point
                       int           resolution // number of segments
) {

	if ( resolution == 0 ) {
		// nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		c.vertices.push_back( {float( p2_x ), float( p2_y ), 0.f} );
		return;
	}

	// --------| invariant: resolution > 1

	c.vertices.reserve( c.vertices.size() + resolution );

	assert( !c.vertices.empty() ); // Contour vertices must not be empty.

	double p0_x = double( c.vertices.back().x ); // start points
	double p0_y = double( c.vertices.back().y ); // start points

	double delta_t = 1. / double( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( int i = 1; i <= resolution; i++ ) {
		double t              = i * delta_t;
		double t_sq           = t * t;
		double one_minus_t    = ( 1. - t );
		double one_minus_t_sq = one_minus_t * one_minus_t;

		double b_x = one_minus_t_sq * p0_x + 2 * one_minus_t * t * p1_x + t_sq * p2_x;
		double b_y = one_minus_t_sq * p0_y + 2 * one_minus_t * t * p1_y + t_sq * p2_y;

		c.vertices.push_back( {float( b_x ), float( b_y ), 0.f} );
	}
}

// ----------------------------------------------------------------------

void contour_cubic_curve_to( Contour &     c,
                             double const &p3_x,      // end point
                             double const &p3_y,      // end point
                             double const &p1_x,      // control point 1
                             double const &p1_y,      // control point 1
                             double const &p2_x,      // control point 2
                             double const &p2_y,      // control point 2
                             int           resolution // number of segments
) {

	if ( resolution == 0 ) {
		// Nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		c.vertices.push_back( {float( p3_x ), float( p3_y ), 0.f} );
		return;
	}

	// --------| invariant: resolution > 1

	c.vertices.reserve( c.vertices.size() + resolution );

	assert( !c.vertices.empty() ); // Contour vertices must not be empty.

	double p0_x = c.vertices.back().x;
	double p0_y = c.vertices.back().y;

	double delta_t = 1. / double( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( int i = 1; i <= resolution; i++ ) {
		double t               = i * delta_t;
		double t_sq            = t * t;
		double t_cub           = t_sq * t;
		double one_minus_t     = ( 1. - t );
		double one_minus_t_sq  = one_minus_t * one_minus_t;
		double one_minus_t_cub = one_minus_t_sq * one_minus_t;

		double b_x = one_minus_t_cub * p0_x + 3 * one_minus_t_sq * t * p1_x + 3 * one_minus_t * t_sq * p2_x + t_cub * p3_x;
		double b_y = one_minus_t_cub * p0_y + 3 * one_minus_t_sq * t * p1_y + 3 * one_minus_t * t_sq * p2_y + t_cub * p3_y;

		c.vertices.push_back( {float( b_x ), float( b_y ), 0.f} );
	}
}

// ----------------------------------------------------------------------

// Converts a list of path points (pp) into a vector of contours
// and returns these as a shape.
static Shape get_shape( stbtt_vertex const *pp_arr, int const pp_count ) {
	Shape shape;

	fprintf( stdout, "** Getting glyph shape **\n" );
	fprintf( stdout, "Number of vertices: %i\n", pp_count );

	stbtt_vertex const *const pp_end = pp_arr + pp_count;

	int current_contour_idx = -1;

	for ( auto pp = pp_arr; pp != pp_end; pp++ ) {
		switch ( pp->type ) {
		case STBTT_vmove:
			// a move means a new glyph
			shape.contours.emplace_back(); // Add new contour
			current_contour_idx++;         // Point to current contour
			contour_move_to( shape.contours[ current_contour_idx ], pp->x, pp->y );
		    break;
		case STBTT_vline:
			// line from last position to this pos
			contour_line_to( shape.contours[ current_contour_idx ], pp->x, pp->y );
		    break;
		case STBTT_vcurve:
			// quadratic bezier to pos
			contour_curve_to( shape.contours[ current_contour_idx ], pp->x, pp->y, pp->cx, pp->cy, 3 );
		    break;
		case STBTT_vcubic:
			// cubic bezier to pos
			contour_cubic_curve_to( shape.contours[ current_contour_idx ], pp->x, pp->y, pp->cx, pp->cy, pp->cx1, pp->cy1, 3 );
		    break;
		}
	}

	return shape;
}

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

static le_font_o *le_font_create() {
	auto self = new le_font_o();

	/* prepare font */

	bool loadOk{};

	auto data  = load_file( "resources/fonts/IBMPlexSans-Text.ttf", &loadOk );
	self->data = {data.data(), data.data() + data.size()};

	if ( loadOk ) {
		stbtt_InitFont( &self->info, self->data.data(), 0 );

		stbtt_vertex *vertices    = nullptr;
		int           numVertices = stbtt_GetCodepointShape( &self->info, 'e', &vertices );

		Shape s = get_shape( vertices, numVertices );

		stbtt_FreeShape( &self->info, vertices );
	}

	return self;
}

// ----------------------------------------------------------------------

static void le_font_destroy( le_font_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_font_api( void *api ) {
	auto &le_font_i = static_cast<le_font_api *>( api )->le_font_i;

	le_font_i.create  = le_font_create;
	le_font_i.destroy = le_font_destroy;
}
