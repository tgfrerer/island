#include "le_font.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/stb_truetype.h"

#include <vector>
#include <fstream>
#include "experimental/filesystem" // for parsing shader source file paths
#include <iostream>
#include <assert.h>

#include <glm/vec2.hpp>

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

typedef glm::vec2 Vertex;

struct Contour {
	// closed loop of vertices.
	std::vector<Vertex> vertices;
};

struct le_glyph_shape_o {
	// a series of contours
	std::vector<Contour> contours;
};

// ----------------------------------------------------------------------

static void contour_move_to( Contour &c, Vertex const &p ) {
	c.vertices.emplace_back( p );
}

// ----------------------------------------------------------------------

static void contour_line_to( Contour &c, Vertex const &p ) {
	c.vertices.emplace_back( p );
}

// ----------------------------------------------------------------------

// Trace a quadratic bezier curve from previous point p0 to target point p2 (p2_x,p2_y),
// controlled by control point p1 (p1_x, p1_y), in steps iterations.
void contour_curve_to( Contour &     c,
                       Vertex const &p2,        // end point
                       Vertex const &p1,        // control point
                       int           resolution // number of segments
) {

	if ( resolution == 0 ) {
		// nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		c.vertices.emplace_back( p2 );
		return;
	}

	// --------| invariant: resolution > 1

	c.vertices.reserve( c.vertices.size() + size_t( resolution ) );

	assert( !c.vertices.empty() ); // Contour vertices must not be empty.

	auto const &p0 = c.vertices.back(); // start points

	float delta_t = 1.f / float( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( int i = 1; i <= resolution; i++ ) {
		float t              = i * delta_t;
		float t_sq           = t * t;
		float one_minus_t    = ( 1.f - t );
		float one_minus_t_sq = one_minus_t * one_minus_t;

		Vertex b = one_minus_t_sq * p0 + 2 * one_minus_t * t * p1 + t_sq * p2;

		c.vertices.emplace_back( b );
	}
}

// ----------------------------------------------------------------------

static void contour_cubic_curve_to( Contour &     c,
                                    Vertex const &p3,        // end point
                                    Vertex const &p1,        // control point 1
                                    Vertex const &p2,        // control point 2
                                    int           resolution // number of segments
) {

	if ( resolution == 0 ) {
		// Nothing to do.
		return;
	}

	if ( resolution == 1 ) {
		// If we are to add but one segment, we may directly add the target point and return.
		c.vertices.emplace_back( p3 );
		return;
	}

	// --------| invariant: resolution > 1

	c.vertices.reserve( c.vertices.size() + size_t( resolution ) );

	assert( !c.vertices.empty() ); // Contour vertices must not be empty.

	auto const &p0 = c.vertices.back();

	float delta_t = 1.f / float( resolution );

	// Note that we begin the following loop at 1,
	// because element 0 (the starting point) is
	// already part of the contour.
	//
	// Loop goes over the set: ]0,resolution]
	//
	for ( int i = 1; i <= resolution; i++ ) {
		float t               = i * delta_t;
		float t_sq            = t * t;
		float t_cub           = t_sq * t;
		float one_minus_t     = ( 1.f - t );
		float one_minus_t_sq  = one_minus_t * one_minus_t;
		float one_minus_t_cub = one_minus_t_sq * one_minus_t;

		Vertex b = one_minus_t_cub * p0 + 3 * one_minus_t_sq * t * p1 + 3 * one_minus_t * t_sq * p2 + t_cub * p3;

		c.vertices.emplace_back( b );
	}
}

// ----------------------------------------------------------------------

// Converts an array of path instructions (pp) into a list of contours.
// A list of contours represents a shape.
static le_glyph_shape_o *get_shape( stbtt_vertex const *pp_arr, int const pp_count ) {
	auto shape = new le_glyph_shape_o();

	fprintf( stdout, "** Getting glyph shape **\n" );
	fprintf( stdout, "Number of vertices: %i\n", pp_count );

	stbtt_vertex const *const pp_end = pp_arr + pp_count;

	int current_contour_idx = -1;
	int resolution          = 30;

	for ( auto pp = pp_arr; pp != pp_end; pp++ ) {
		switch ( pp->type ) {
		case STBTT_vmove:
			// a move signals the start of a new glyph
			shape->contours.emplace_back(); // Add new contour
			current_contour_idx++;          // Point to current contour
			contour_move_to( shape->contours[ current_contour_idx ], {pp->x, pp->y} );
		    break;
		case STBTT_vline:
			// line from last position to this pos
			contour_line_to( shape->contours[ current_contour_idx ], {pp->x, pp->y} );
		    break;
		case STBTT_vcurve:
			// quadratic bezier to pos
			contour_curve_to( shape->contours[ current_contour_idx ], {pp->x, pp->y}, {pp->cx, pp->cy}, resolution );
		    break;
		case STBTT_vcubic:
			// cubic bezier to pos
			contour_cubic_curve_to( shape->contours[ current_contour_idx ], {pp->x, pp->y}, {pp->cx, pp->cy}, {pp->cx1, pp->cy1}, resolution );
		    break;
		}
	}

	return shape;
}

// ----------------------------------------------------------------------

static le_glyph_shape_o *le_font_get_shape_for_glyph( le_font_o *self, int32_t codepoint, size_t *num_contours ) {
	stbtt_vertex *pathInstructions = nullptr;

	int pathInstructionsCount = stbtt_GetCodepointShape( &self->info, codepoint, &pathInstructions );

	le_glyph_shape_o *shape = get_shape( pathInstructions, pathInstructionsCount );

	stbtt_FreeShape( &self->info, pathInstructions );

	if ( num_contours ) {
		*num_contours = shape->contours.size();
	}

	return shape;
}

// ----------------------------------------------------------------------

static Vertex *le_glyph_shape_get_vertices_for_shape_contour( le_glyph_shape_o *self, size_t const &contour_idx, size_t *num_vertices ) {
	*num_vertices = self->contours[ contour_idx ].vertices.size();
	return self->contours[ contour_idx ].vertices.data();
};

// ----------------------------------------------------------------------

static size_t le_glyph_shape_get_num_contours( le_glyph_shape_o *self ) {
	return self->contours.size();
}

// ----------------------------------------------------------------------

static le_font_o *le_font_create() {
	auto self = new le_font_o();

	/* prepare font */

	//	char const *font_file_name = "resources/fonts/IBMPlexSans-Text.ttf";
	char const *font_file_name = "resources/fonts/IBMPlexSans-Regular.otf";

	bool loadOk{};

	auto data  = load_file( font_file_name, &loadOk );
	self->data = {data.data(), data.data() + data.size()};

	if ( loadOk ) {
		stbtt_InitFont( &self->info, self->data.data(), 0 );
	} else {
		std::cerr << "Could not load font file: '" << font_file_name << "'" << std::endl
		          << std::flush;
	}

	return self;
}

// ----------------------------------------------------------------------

static void le_font_destroy( le_font_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_glyph_shape_destroy( le_glyph_shape_o *self ) {
	self->contours.clear();
	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_font_api( void *api ) {
	auto &le_font_i = static_cast<le_font_api *>( api )->le_font_i;

	le_font_i.create              = le_font_create;
	le_font_i.destroy             = le_font_destroy;
	le_font_i.get_shape_for_glyph = le_font_get_shape_for_glyph;

	auto &le_glyph_shape_i = static_cast<le_font_api *>( api )->le_glyph_shape_i;

	le_glyph_shape_i.destroy                        = le_glyph_shape_destroy;
	le_glyph_shape_i.get_vertices_for_shape_contour = le_glyph_shape_get_vertices_for_shape_contour;
	le_glyph_shape_i.get_num_contours               = le_glyph_shape_get_num_contours;
}
