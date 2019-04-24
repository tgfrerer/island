#include "le_font.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/stb_truetype.h"
#include "3rdparty/stb_rect_pack.h"

#include <vector>
#include <array>
#include <fstream>
#include "experimental/filesystem" // for parsing shader source file paths
#include <iostream>
#include <assert.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace std {
using namespace experimental;
}

struct UnicodeRange {
	uint32_t                      start_range;
	uint32_t                      end_range;
	std::vector<stbtt_packedchar> data;
};

struct le_font_o {
	// members
	static constexpr uint16_t                                      PIXELS_WIDTH  = 512;
	static constexpr uint16_t                                      PIXELS_HEIGHT = 256;
	static constexpr uint16_t                                      PIXELS_BPP    = 1; // bytes per pixels
	stbtt_fontinfo                                                 info;
	std::vector<uint8_t>                                           data;                     // ttf file data
	std::array<uint8_t, PIXELS_WIDTH * PIXELS_HEIGHT * PIXELS_BPP> pixels;                   // pixels for texture_atlas
	float                                                          font_size         = 24.f; // font size in pixels. TODO: check units for font size.
	bool                                                           has_texture_atlas = false;
	std::vector<UnicodeRange>                                      unicode_ranges; // available unicode ranges, assumed to be sorted.
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
                       size_t        resolution // number of segments
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

	c.vertices.reserve( c.vertices.size() + resolution );

	assert( !c.vertices.empty() ); // Contour vertices must not be empty.

	auto const &p0 = c.vertices.back(); // start points

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

		c.vertices.emplace_back( b );
	}
}

// ----------------------------------------------------------------------

static void contour_cubic_curve_to( Contour &     c,
                                    Vertex const &p3,        // end point
                                    Vertex const &p1,        // control point 1
                                    Vertex const &p2,        // control point 2
                                    size_t        resolution // number of segments
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

	c.vertices.reserve( c.vertices.size() + resolution );

	assert( !c.vertices.empty() ); // Contour vertices must not be empty.

	auto const &p0 = c.vertices.back();

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

		c.vertices.emplace_back( b );
	}
}

// ----------------------------------------------------------------------

// Converts an array of path instructions (pp) into a list of contours.
// A list of contours represents a shape.
static le_glyph_shape_o *get_shape( stbtt_vertex const *pp_arr, int const pp_count, size_t resolution ) {
	auto shape = new le_glyph_shape_o();

	//	fprintf( stdout, "** Getting glyph shape **\n" );
	//	fprintf( stdout, "Number of vertices: %i\n", pp_count );

	stbtt_vertex const *const pp_end = pp_arr + pp_count;

	size_t current_contour_idx = ~( 0ul );

	for ( auto pp = pp_arr; pp != pp_end; pp++ ) {
		switch ( pp->type ) {
		case STBTT_vmove:
			// a move signals the start of a new glyph
			shape->contours.emplace_back(); // Add new contour
			current_contour_idx++;          // Point to current contour
			assert( current_contour_idx < shape->contours.size() );
			contour_move_to( shape->contours[ current_contour_idx ], {pp->x, pp->y} );
		    break;
		case STBTT_vline:
			// line from last position to this pos
			assert( current_contour_idx < shape->contours.size() );
			contour_line_to( shape->contours[ current_contour_idx ], {pp->x, pp->y} );
		    break;
		case STBTT_vcurve:
			// quadratic bezier to pos
			assert( current_contour_idx < shape->contours.size() );
			contour_curve_to( shape->contours[ current_contour_idx ], {pp->x, pp->y}, {pp->cx, pp->cy}, resolution );
		    break;
		case STBTT_vcubic:
			// cubic bezier to pos
			assert( current_contour_idx < shape->contours.size() );
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

	le_glyph_shape_o *shape = get_shape( pathInstructions, pathInstructionsCount, 10 );

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

static le_font_o *le_font_create( char const *font_filename, float font_size ) {
	auto self = new le_font_o();

	/* prepare font */

	bool loadOk{};

	auto data  = load_file( font_filename, &loadOk );
	self->data = {data.data(), data.data() + data.size()};

	if ( loadOk ) {
		stbtt_InitFont( &self->info, self->data.data(), 0 );
	} else {
		std::cerr << "Could not load font file: '" << font_filename << "'" << std::endl
		          << std::flush;
	}

	self->font_size = font_size;

	return self;
}

// ----------------------------------------------------------------------

// Creates - (or re-creates) texture atlas for a given font
static bool le_font_create_atlas( le_font_o *self ) {
	if ( false == self->has_texture_atlas ) {

		stbtt_pack_context pack_context{};
		stbtt_PackBegin( &pack_context, self->pixels.data(), self->PIXELS_WIDTH, self->PIXELS_HEIGHT, 0, 1, nullptr ); // stride 0 means tightly packed, leave 1 pixel padding around pixels

		stbtt_PackSetOversampling( &pack_context, 2, 1 );

		auto pack_uniform_range = []( stbtt_pack_context *ctx, unsigned char const *font_data, float font_size, uint32_t start_range, uint32_t end_range ) -> UnicodeRange {
			UnicodeRange unicode_latin_extended;
			unicode_latin_extended.start_range = start_range;
			unicode_latin_extended.end_range   = end_range;
			unicode_latin_extended.data.resize( unicode_latin_extended.end_range - unicode_latin_extended.start_range );

			stbtt_PackFontRange( ctx, font_data, 0, font_size,
			                     int( unicode_latin_extended.start_range ),
			                     int( unicode_latin_extended.end_range - unicode_latin_extended.start_range ),
			                     unicode_latin_extended.data.data() );

			return unicode_latin_extended;
		};

		self->unicode_ranges.emplace_back( pack_uniform_range( &pack_context, self->data.data(), self->font_size, 0x00, 0x7F ) );     // ascii
		self->unicode_ranges.emplace_back( pack_uniform_range( &pack_context, self->data.data(), self->font_size, 0x80, 0xff ) );     // latin-extended
		self->unicode_ranges.emplace_back( pack_uniform_range( &pack_context, self->data.data(), self->font_size, 0x20A0, 0x20CF ) ); // currency symbols

		stbtt_PackEnd( &pack_context );

		self->has_texture_atlas = true;
	}
	return true;
}

// ----------------------------------------------------------------------
// Returns the length of uninterrupted sequence of '1' bits
// starting with the highestmost bit.
//
// e.g.:
// 0b110001 -> 2
// 0b101101 -> 1
// 0b111010 -> 3
// 0b001001 -> 0
//
static inline uint8_t count_leading_bits( uint8_t in ) {

	uint8_t count_bits;
	for ( count_bits = 0; count_bits != 8; ++count_bits ) {
		if ( ( in & ( 1 << 7 ) ) == 0 ) {
			break;
		};
		in = uint8_t( in << 1 );
	}

	return count_bits;
}
// ----------------------------------------------------------------------

// Places geometry into vertices to draw an utf-8 string using font.
//
// Returns count of used vertices - calculated as 6 * codepoint count.
// Note that we count utf-8 code points, not ascii characters.
//
// Place nullptr in vertices to only return vertex count.
//
// max_vertices is optional - if set, it marks the maximum number of
// vertices we may write into.
size_t le_font_draw_utf8_string( le_font_o *self, const char *str, float x_pos, float y_pos, glm::vec4 *vertices, size_t max_vertices ) {

	size_t glyph_count = 0;

	const float x_anchor     = x_pos;
	const float y_anchor     = y_pos;
	size_t      num_newlines = 0;

	static constexpr uint8_t mask_bits[] = {
	    0b00000000,
	    0b10000000,
	    0b11000000,
	    0b11100000,
	    0b11110000,
	};

	std::vector<uint32_t> codepoints;
	codepoints.reserve( max_vertices );

	{
		// Iterate over utf-8 glyphs: <https://en.m.wikipedia.org/wiki/UTF-8>
		// We need to keep track of number of bytes to process.
		uint8_t count_remaining_bytes = 0;

		uint32_t code_point = 0;

		for ( char const *c = str; *c != '\0'; c++ ) {

			uint8_t cur_byte = uint8_t( *c );

			if ( cur_byte & 0x80 ) {
				//This codepoint is from beyond the ASCII range.

				// Let's count the leading '1' bits.
				uint8_t leading_bit_count = count_leading_bits( cur_byte );

				// If there are no remaining bits to process this marks the beginning
				// of a new code point. The leadnig count of '1' bits will tell us how
				// many bytes of input to expect for this code point.
				if ( count_remaining_bytes == 0 ) {
					// new glyph
					code_point            = 0;
					count_remaining_bytes = leading_bit_count;
				}

				// We mask out the leading bits from the current byte,
				// and shift it into place based on the number of remaining bytes.
				code_point |= ( cur_byte & ~mask_bits[ leading_bit_count + 1 ] ) << ( ( --count_remaining_bytes ) * 6 );

			} else {
				// Codepoint is part of the ASCII range
				code_point            = cur_byte;
				count_remaining_bytes = 0;
			}

			if ( count_remaining_bytes == 0 ) {
				// Add code point.
				codepoints.push_back( code_point );
				++glyph_count;
			}
		}
	}

	if ( nullptr == vertices ) {
		// Don't update vertices, only return number of glyphs * 6, which is the number of required vertices.
		return glyph_count * 6;
	}

	// --------| invariant: vertices is set

	size_t num_vertices = 0;

	{
		stbtt_aligned_quad quad{};

		for ( auto const &cp : codepoints ) {

			if ( cp == '\n' ) {
				y_pos = y_anchor + int( ( ++num_newlines ) * self->font_size * 1.2f ); // We increase y position - assumed line height 1.2, and align to pixels.
				x_pos = x_anchor;                                                      // And reset x position
				continue;
			}

			// we must check that our codepoint is contained within a range of
			// available codepoints from the current font.

			UnicodeRange *            range      = self->unicode_ranges.data();
			UnicodeRange const *const end_ranges = range + self->unicode_ranges.size();

			while ( range != end_ranges && cp > range->end_range ) {
				range++;
			}

			if ( range == end_ranges || cp < range->start_range ) {
				// could not find codepoint in known ranges.
				continue;
			}

			// -------| invariant: cp is < range->end_range

			stbtt_GetPackedQuad( range->data.data(), self->PIXELS_WIDTH, self->PIXELS_HEIGHT, int( cp - range->start_range ), &x_pos, &y_pos, &quad, 0 );

			if ( num_vertices + 6 > max_vertices ) {
				// we don't have enough vertex memory left, we must return early.
				return num_vertices / 6;
			}

			// Update vertices - stb_tt packed quad returns top-left,
			// and bottom-right vertex, we must expand this to two
			// triangles.

			// Our return vertices will be x/y s/t per-vertex
			// (we store texture coordinates per vertex in .zw coordinates to save bandwidth)

			glm::vec4 *vtx = vertices + num_vertices;

			vtx[ 0 ] = {quad.x0, quad.y0, quad.s0, quad.t0}; // top-left
			vtx[ 1 ] = {quad.x0, quad.y1, quad.s0, quad.t1}; // bottom-left
			vtx[ 2 ] = {quad.x1, quad.y1, quad.s1, quad.t1}; // bottom-right

			vtx[ 3 ] = {quad.x1, quad.y0, quad.s1, quad.t0}; // top-right
			vtx[ 4 ] = {quad.x0, quad.y0, quad.s0, quad.t0}; // top-left
			vtx[ 5 ] = {quad.x1, quad.y1, quad.s1, quad.t1}; // bottom-right

			num_vertices += 6;
		}
	}

	return num_vertices;
}

// ----------------------------------------------------------------------

static bool le_font_get_atlas( le_font_o *self, uint8_t const **pixels, uint32_t *width, uint32_t *height, uint32_t *pix_stride_in_bytes ) {

	if ( false == self->has_texture_atlas ) {
		return false;
	}

	*pixels              = self->pixels.data();
	*pix_stride_in_bytes = self->PIXELS_BPP;
	*width               = self->PIXELS_WIDTH;
	*height              = self->PIXELS_HEIGHT;

	return true;
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
	le_font_i.create_atlas        = le_font_create_atlas;
	le_font_i.get_atlas           = le_font_get_atlas;

	le_font_i.draw_utf8_string = le_font_draw_utf8_string;

	auto &le_glyph_shape_i = static_cast<le_font_api *>( api )->le_glyph_shape_i;

	le_glyph_shape_i.destroy                        = le_glyph_shape_destroy;
	le_glyph_shape_i.get_vertices_for_shape_contour = le_glyph_shape_get_vertices_for_shape_contour;
	le_glyph_shape_i.get_num_contours               = le_glyph_shape_get_num_contours;
}
