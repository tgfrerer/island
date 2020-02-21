#include "le_font.h"
#include "le_core/le_core.hpp"

#include "3rdparty/stb_truetype.h"
#include "3rdparty/stb_rect_pack.h"

#include <vector>
#include <array>
#include <fstream>
#include <filesystem> // for parsing source filepaths
#include <iostream>
#include <assert.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include "modules/le_path/le_path.h" // for get_path_for_glyph

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

// ----------------------------------------------------------------------

static void le_font_add_paths_for_glyph( le_font_o const *self, le_path_o *path, int32_t const codepoint, float const scale, glm::vec2 *offset, int32_t const codepoint_prev ) {
	stbtt_vertex *pp_arr   = nullptr;
	int           pp_count = stbtt_GetCodepointShape( &self->info, codepoint, &pp_arr );

	stbtt_vertex const *const pp_end = pp_arr + pp_count;

	using namespace le_path;

	float kern_advance = 0.f;

	if ( codepoint_prev ) {
		kern_advance = stbtt_GetCodepointKernAdvance( &self->info, codepoint_prev, codepoint );
	}

	glm::vec2 p0{};
	glm::vec2 p1{};
	glm::vec2 p2{};

	for ( auto pp = pp_arr; pp != pp_end; pp++ ) {
		// Note that since the font coordinate system has origin at top/left we must flip y
		// to make it work with our standard coordinate system which has positive y pointing
		// upwards.

		switch ( pp->type ) {
		case STBTT_vmove:
			// a move signals the start of a new glyph
			p0 = *offset + scale * glm::vec2{pp->x + kern_advance, -pp->y};
			le_path_i.move_to( path, &p0 );
			break;
		case STBTT_vline:
			// line from last position to this pos
			p0 = *offset + scale * glm::vec2{pp->x + kern_advance, -pp->y};
			le_path_i.line_to( path, &p0 );
			break;
		case STBTT_vcurve:
			// quadratic bezier to pos
			p0 = *offset + scale * glm::vec2{pp->x + kern_advance, -pp->y};
			p1 = *offset + scale * glm::vec2{pp->cx + kern_advance, -pp->cy};
			le_path_i.quad_bezier_to( path, &p0, &p1 );
			break;
		case STBTT_vcubic:
			// cubic bezier to pos
			p0 = *offset + scale * glm::vec2{pp->x + kern_advance, -pp->y};
			p1 = *offset + scale * glm::vec2{pp->cx + kern_advance, -pp->cy};
			p2 = *offset + scale * glm::vec2{pp->cx1 + kern_advance, -pp->cy1};
			le_path_i.cubic_bezier_to( path, &p0, &p1, &p2 );
			break;
		}
	}
	int advanceWidth, leftSideBearing;
	stbtt_GetCodepointHMetrics( &self->info, codepoint, &advanceWidth, &leftSideBearing );

	// Update offset
	offset->x += scale * ( kern_advance + advanceWidth );

	// TODO: if we know the previous codepoint, we may add kerning to offset before drawing
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
			UnicodeRange r;
			r.start_range = start_range;
			r.end_range   = end_range;
			r.data.resize( r.end_range - r.start_range );

			stbtt_PackFontRange( ctx, font_data, 0, font_size,
			                     int( r.start_range ),
			                     int( r.end_range - r.start_range ),
			                     r.data.data() );

			return r;
		};

		self->unicode_ranges.emplace_back( pack_uniform_range( &pack_context, self->data.data(), self->font_size, 0x00, 0x7F ) );     // ascii
		self->unicode_ranges.emplace_back( pack_uniform_range( &pack_context, self->data.data(), self->font_size, 0x80, 0xff ) );     // latin-extended
		self->unicode_ranges.emplace_back( pack_uniform_range( &pack_context, self->data.data(), self->font_size, 0x20A0, 0x20CF ) ); // currency symbols
		self->unicode_ranges.emplace_back( pack_uniform_range( &pack_context, self->data.data(), self->font_size, 0x2190, 0x21FF ) ); // arrows

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
		}
		in = uint8_t( in << 1 );
	}

	return count_bits;
}

// ----------------------------------------------------------------------
// Iterate over utf-8 glyphs: <https://en.m.wikipedia.org/wiki/UTF-8>
// Calls given callback for each codepoint in str.
// Runs until it meets '\0' (end of c-string) character.
// Returns true on success, false if the last codepoint was not completely
// parsed.
static bool le_utf8_iterator( char const *str, void *user_data, le_font_api::le_uft8_iterator_cb_t cb ) {
	static constexpr uint8_t mask_bits[] = {
	    0b00000000,
	    0b10000000,
	    0b11000000,
	    0b11100000,
	    0b11110000,
	};

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
			// of a new code point. The leading count of '1' bits will tell us how
			// many bytes of input to expect for this code point.
			if ( count_remaining_bytes == 0 ) {
				// new glyph
				code_point            = 0;
				count_remaining_bytes = leading_bit_count;
			}

			// We mask out the leading bits from the current byte,
			// and shift the current byte into place based on the
			// number of remaining bytes.
			code_point |= ( cur_byte & ~mask_bits[ leading_bit_count + 1 ] ) << ( ( --count_remaining_bytes ) * 6 );

		} else {
			// Codepoint is part of the ASCII range
			code_point            = cur_byte;
			count_remaining_bytes = 0;
		}

		if ( count_remaining_bytes == 0 ) {
			// Add code point.
			cb( code_point, user_data );
		}
	}

	// There must not be any leftover bytes to process when the end of the input
	// string was reached.
	// We return false to signal that the string was prematurely cut short.
	return ( count_remaining_bytes == 0 );
}

// Places geometry into vertices to draw an utf-8 string using given font.
//
// Returns count of used vertices - calculated as 6 * codepoint count.
// Note that we count utf-8 code points, not ascii characters.
//
// Place nullptr in `vertices` to calculate vertex count and return early.
//
// `max_vertices` marks the maximum number of vertices we may write into.
//
// `vertex_offset` tells us at which position in `vertices` to begin writing vertex data
//
// If vertex data was written, x_pos and y_pos will be updated to the current
// advance of the virtual text cursor.
static size_t le_font_draw_utf8_string( le_font_o *self, const char *str, float *x_pos, float *y_pos, glm::vec4 *vertices, size_t max_vertices, size_t vertex_offset ) {

	size_t glyph_count = 0;

	std::vector<uint32_t> codepoints;
	codepoints.reserve( max_vertices / 6 );

	le_utf8_iterator( str, &codepoints, []( uint32_t cp, void *user_data ) {
		auto &cps = *static_cast<std::vector<uint32_t> *>( user_data );
		cps.push_back( uint32_t( cp ) );
	} );

	glyph_count = codepoints.size();

	if ( nullptr == vertices ) {
		// Don't update vertices, only return number of glyphs * 6, which is the number of required vertices.
		return glyph_count * 6;
	}

	// --------| invariant: vertices is set

	const float x_anchor     = x_pos ? *x_pos : 0; // In case nullptr, set to zero.
	const float y_anchor     = y_pos ? *y_pos : 0; // In case nullptr, set to zero.
	size_t      num_newlines = 0;

	size_t num_vertices = 0;

	{
		stbtt_aligned_quad quad{};

		for ( auto const &cp : codepoints ) {

			if ( cp == '\n' ) {
				*y_pos = y_anchor + int( ( ++num_newlines ) * self->font_size * 1.2f ); // We increase y position - assumed line height 1.2, aligned to pixels,
				*x_pos = x_anchor;                                                      // and reset x position
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

			stbtt_GetPackedQuad( range->data.data(), self->PIXELS_WIDTH, self->PIXELS_HEIGHT, int( cp - range->start_range ), x_pos, y_pos, &quad, 0 );

			if ( num_vertices + 6 > max_vertices ) {
				// we don't have enough vertex memory left, we must return early.
				return num_vertices / 6;
			}

			// Update vertices - stb_tt_packed_quad returns top-left,
			// and bottom-right vertex, and we must expand this to two
			// triangles.

			// Our return vertices will be x/y s/t per-vertex
			// (we store texture coordinates per vertex in .zw coordinates to save bandwidth)

			glm::vec4 *vtx = vertices + vertex_offset + num_vertices;

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

static float le_font_get_scale_for_pixels_height( le_font_o *self, float height_in_pixels ) {
	return stbtt_ScaleForPixelHeight( &self->info, height_in_pixels );
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

static uint8_t *le_font_create_codepoint_sdf_bitmap( le_font_o *self, float scale, int codepoint, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff ) {
	return stbtt_GetCodepointSDF( &self->info, scale, codepoint, padding, onedge_value, pixel_dist_scale, width, height, xoff, yoff );
}

// ----------------------------------------------------------------------

static void le_font_destroy_codepoint_sdf_bitmap( le_font_o *self, uint8_t *bitmap ) {
	stbtt_FreeSDF( bitmap, &self->info );
}

// ----------------------------------------------------------------------

static void le_font_destroy( le_font_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_font_api( void *api ) {
	auto &le_font_i = static_cast<le_font_api *>( api )->le_font_i;

	le_font_i.create                       = le_font_create;
	le_font_i.destroy                      = le_font_destroy;
	le_font_i.create_atlas                 = le_font_create_atlas;
	le_font_i.get_atlas                    = le_font_get_atlas;
	le_font_i.add_paths_for_glyph          = le_font_add_paths_for_glyph;
	le_font_i.get_scale_for_pixel_height   = le_font_get_scale_for_pixels_height;
	le_font_i.create_codepoint_sdf_bitmap  = le_font_create_codepoint_sdf_bitmap;
	le_font_i.destroy_codepoint_sdf_bitmap = le_font_destroy_codepoint_sdf_bitmap;

	le_font_i.draw_utf8_string = le_font_draw_utf8_string;

	auto &utf8_iterator = static_cast<le_font_api *>( api )->le_utf8_iterator;
	utf8_iterator       = le_utf8_iterator;
}
