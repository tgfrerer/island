/*

# LICENSE

Much of this module was adapted from [vello](https://github.com/linebender/vello) source code.

We are grateful to the vello authors for releasing Vello under a permissive license.

Vello is released by the Vello Authors under Apache License, Version 2.0, or the MIT License.

You can find the original vello renderer implementation here:
<https://github.com/linebender/vello>

<https://github.com/linebender/vello/blob/main/vello_shaders/LICENSE-MIT>
<https://github.com/linebender/vello/blob/main/vello_shaders/LICENSE-APACHE>

GPU rasterizer shaders are taken verbatim (in spirv-compiled form)
from vello_shaders.
<https://github.com/linebender/vello/tree/main/vello_shaders>

Vello shaders are licensed under Apache License, Version 2.0, MIT, or Unilicense.
<https://github.com/linebender/vello/blob/main/vello_shaders/shader/UNLICENSE>
<https://github.com/linebender/vello/blob/main/vello_shaders/shader/LICENSE-APACHE>
<https://github.com/linebender/vello/blob/main/vello_shaders/shader/LICENSE-MIT>

 */

#include "le_2d.h"

#include <cassert>
#include <cmath>
#include <vector>
// ----------------------------------------------------------------------
#include "private/le_2d/le_2d_shared.h"
#include "3rdparty/src/glm/vec2.hpp"

// ----------------------------------------------------------------------

enum class PathSegmentType : uint8_t {
	eUndefined = 0,
	eLineTo    = 1,
	eQuadTo    = 2,
	eCubicTo   = 3,
};

class PathTag {
	/// Bit for path segments that are represented as f32 values. If unset
	/// they are represented as i16.
	static constexpr uint8_t F32_BIT = 0x8;
	/// Mask for bottom 3 bits that contain the [`PathSegmentType`].
	static constexpr uint8_t SEGMENT_MASK = 0x3;

  public:
	uint8_t data = 0;

	/// 32-bit floating point line segment.
	static constexpr uint8_t LINE_TO_F32 = uint8_t( PathSegmentType::eLineTo ) | F32_BIT;

	/// 32-bit floating point quadratic segment.
	static constexpr uint8_t QUAD_TO_F32 = uint8_t( PathSegmentType::eQuadTo ) | F32_BIT;

	/// 32-bit floating point cubic segment.
	static constexpr uint8_t CUBIC_TO_F32 = uint8_t( PathSegmentType::eCubicTo ) | F32_BIT;

	/// 16-bit integral line segment.
	static constexpr uint8_t LINE_TO_I16 = uint8_t( PathSegmentType::eLineTo );

	/// 16-bit integral quadratic segment.
	static constexpr uint8_t QUAD_TO_I16 = uint8_t( PathSegmentType::eQuadTo );

	/// 16-bit integral cubic segment.
	static constexpr uint8_t CUBIC_TO_I16 = uint8_t( PathSegmentType::eCubicTo );

	/// Transform marker.
	static constexpr uint8_t TRANSFORM = 0x20;

	/// Path marker.
	static constexpr uint8_t PATH = 0x10;

	/// Style setting.
	static constexpr uint8_t STYLE = 0x40;

	/// Bit that marks a segment that is the end of a subpath.
	static constexpr uint8_t SUBPATH_END_BIT = 0x4;

	inline PathSegmentType get_path_segment_type() const {
		return PathSegmentType( data & SEGMENT_MASK );
	}

	inline bool is_path_segment() const {
		return data & SEGMENT_MASK;
	}

	inline bool is_subpath_end() const {
		return data & SUBPATH_END_BIT;
	}

	inline void set_subpath_end() {
		data |= SUBPATH_END_BIT;
	}

	inline bool is_f32() const {
		return data & F32_BIT;
	}
};

static_assert( sizeof( PathTag ) == sizeof( uint8_t ), "pathtag must be one byte in size." );

using namespace le_2d;

struct Style {

	// ***********************************************************************
	//
	// The float 32->float 16 conversion function is by Fabian "ryg" Giesen.
	// it was released under public domain and copied from here:
	//
	// https://gist.github.com/rygorous/2156668#file-gistfile1-cpp-L285
	//

	union FP32 {
		uint32_t u;
		float    f;
		struct
		{
			uint32_t Mantissa : 23;
			uint32_t Exponent : 8;
			uint32_t Sign : 1;
		};
	};

	union FP16 {
		unsigned short u;
		struct
		{
			uint32_t Mantissa : 10;
			uint32_t Exponent : 5;
			uint32_t Sign : 1;
		};
	};

	static FP16 float_to_half_fast3( FP32 f ) {
		FP32     f32infty   = { 255 << 23 };
		FP32     f16infty   = { 31 << 23 };
		FP32     magic      = { 15 << 23 };
		uint32_t sign_mask  = 0x80000000u;
		uint32_t round_mask = ~0xfffu;
		FP16     o          = { 0 };

		uint32_t sign = f.u & sign_mask;
		f.u ^= sign;

		// NOTE all the integer compares in this function can be safely
		// compiled into signed compares since all operands are below
		// 0x80000000. Important if you want fast straight SSE2 code
		// (since there's no unsigned PCMPGTD).

		if ( f.u >= f32infty.u )                          // Inf or NaN (all exponent bits set)
			o.u = ( f.u > f32infty.u ) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
		else                                              // (De)normalized number or zero
		{
			f.u &= round_mask;
			f.f *= magic.f;
			f.u -= round_mask;
			if ( f.u > f16infty.u )
				f.u = f16infty.u; // Clamp to signed infinity if overflowed

			o.u = f.u >> 13; // Take the bits!
		}

		o.u |= sign >> 16;
		return o;
	}
	// ***********************************************************************

	///
	/// This is taken/adapted from vello_encoding, which can be found here:
	/// <https://github.com/linebender/vello/tree/main/vello_encoding>
	///
	/// Encodes the stroke and fill style parameters. This field is split into two 16-bit
	/// parts:
	///
	/// - `flags: u16` - encodes fill vs stroke, even-odd vs non-zero fill
	///   mode for fills and cap and join style for strokes. See the
	///   `FLAGS_*` constants below for more information.
	///
	///   ```text
	///   flags: |style|fill|join|start cap|end cap|reserved|
	///    bits:  0     1    2-3  4-5       6-7     8-15
	///   ```
	///
	/// - `miter_limit: u16` - The miter limit for a stroke, encoded in
	///   binary16 (half) floating point representation. This field is
	///   only meaningful for the `Join::Miter` join style. It's ignored
	///   for other stroke styles and fills.
	uint32_t flags_and_miter_limit;
	//
	// 	                               /// Encodes the stroke width. This field is ignored for fills.
	float line_width;

	/// 0 for a fill, 1 for a stroke
	static constexpr uint32_t FLAGS_STYLE_BIT = 0x8000'0000;

	/// 0 for non-zero, 1 for even-odd
	static constexpr uint32_t FLAGS_FILL_BIT = 0x4000'0000;

	/// Encodings for join style:
	///    - 0b00 -> bevel
	///    - 0b01 -> miter
	///    - 0b10 -> round
	static constexpr uint32_t FLAGS_JOIN_BITS_BEVEL = 0;
	static constexpr uint32_t FLAGS_JOIN_BITS_MITER = 0x1000'0000;
	static constexpr uint32_t FLAGS_JOIN_BITS_ROUND = 0x2000'0000;
	static constexpr uint32_t FLAGS_JOIN_MASK       = 0x3000'0000;

	/// Encodings for cap style:
	///    - 0b00 -> butt
	///    - 0b01 -> square
	///    - 0b10 -> round
	static constexpr uint32_t FLAGS_CAP_BITS_BUTT   = 0;
	static constexpr uint32_t FLAGS_CAP_BITS_SQUARE = 0x0100'0000;
	static constexpr uint32_t FLAGS_CAP_BITS_ROUND  = 0x0200'0000;

	static constexpr uint32_t FLAGS_START_CAP_BITS_BUTT   = FLAGS_CAP_BITS_BUTT << 2;
	static constexpr uint32_t FLAGS_START_CAP_BITS_SQUARE = FLAGS_CAP_BITS_SQUARE << 2;
	static constexpr uint32_t FLAGS_START_CAP_BITS_ROUND  = FLAGS_CAP_BITS_ROUND << 2;
	static constexpr uint32_t FLAGS_END_CAP_BITS_BUTT     = FLAGS_CAP_BITS_BUTT;
	static constexpr uint32_t FLAGS_END_CAP_BITS_SQUARE   = FLAGS_CAP_BITS_SQUARE;
	static constexpr uint32_t FLAGS_END_CAP_BITS_ROUND    = FLAGS_CAP_BITS_ROUND;

	static constexpr uint32_t FLAGS_START_CAP_MASK = 0x0C00'0000;
	static constexpr uint32_t FLAGS_END_CAP_MASK   = 0x0300'0000;
	static constexpr uint32_t MITER_LIMIT_MASK     = 0xFFFF;

	static Style from_fill( FillStyle const& fill ) {
		return Style{
		    .flags_and_miter_limit = ( fill == FillStyle::EvenOdd ) ? FLAGS_FILL_BIT : 0,
		    .line_width            = 0.f,
		};
	};

	static Style from_stroke( Stroke const& stroke ) {
		uint32_t style     = FLAGS_STYLE_BIT;
		uint32_t join      = 0;
		uint32_t start_cap = 0;
		uint32_t end_cap   = 0;
		// clang-format off
		switch ( stroke.join ) {
			case Join::eBevel: join = FLAGS_JOIN_BITS_BEVEL; break;
			case Join::eMiter: join = FLAGS_JOIN_BITS_MITER; break;
			case Join::eRound: join = FLAGS_JOIN_BITS_ROUND; break;
		};
		switch ( stroke.start_cap ) {
			case Cap::eButt:   start_cap = FLAGS_START_CAP_BITS_BUTT; break;
			case Cap::eRound:  start_cap = FLAGS_START_CAP_BITS_ROUND; break;
			case Cap::eSquare: start_cap = FLAGS_START_CAP_BITS_SQUARE; break;
		};
		switch ( stroke.end_cap) {
			case Cap::eButt:   end_cap = FLAGS_END_CAP_BITS_BUTT; break;
			case Cap::eRound:  end_cap = FLAGS_END_CAP_BITS_ROUND; break;
			case Cap::eSquare: end_cap = FLAGS_END_CAP_BITS_SQUARE; break;
		};
		// clang-format on
		uint32_t miter_limit = float_to_half_fast3( FP32( stroke.miter_limit ) ).u;

		return Style{
		    .flags_and_miter_limit = style | join | start_cap | end_cap | miter_limit,
		    .line_width            = float( stroke.width ),
		};
	}

	const bool operator==( Style const& rhs ) const {
		return ( 0 == memcmp( this, &rhs, sizeof( Style ) ) );
	}

	const bool operator!=( Style const& rhs ) const {
		return !( *this == rhs );
	}
};

enum class DrawTag : uint32_t {
	NOP             = 0,
	COLOUR          = 0x44,
	LINEAR_GRADIENT = 0x114,
	RADIAL_GRADIENT = 0x29c,
	SWEEP_GRADIENT  = 0x254,
	IMAGE           = 0x28c,
	BLUR_RECT       = 0x2d4,
	BEGIN_CLIP      = 0x9,
	END_CLIP        = 0x21,
};

static uint32_t draw_tag_get_info_size( DrawTag const& t ) {
	return ( ( uint32_t( t ) >> 6 ) & uint32_t( 0xf ) );
}

struct Resources {
	// Fill this in once we want to make more advanced rendering available.
	// this is for gradients, and images, and glyph runs.
};

// ----------------------------------------------------------------------

struct DrawBeginClip {

	uint32_t blend_mode;
	union {
		float    as_float;
		uint32_t as_uint32;
	} alpha;

	DrawBeginClip( BlendMode const& b, float a )
	    : blend_mode( uint32_t( b.mix ) << 8 | uint32_t( b.compose ) )
	    , alpha( a ) {
	    };
};

// ----------------------------------------------------------------------
struct path_encoder_o {

	enum PathState : uint32_t {
		eStart = 0,
		eMoveTo,
		eNonemptySubpath,
	};

	glm::vec2 first_point             = {};
	glm::vec2 first_start_tangent_end = {};
	PathState state                   = eStart;

	uint32_t n_encoded_segments = 0;

	bool is_fill = false;
};

// ----------------------------------------------------------------------

struct le_2d_encoder_o {

	path_encoder_o path = {};

	// /// The path tag stream.
	std::vector<PathTag> path_tags;

	// /// The path data stream.
	// /// Stores all coordinates on paths.
	std::vector<float> path_data;

	// /// The draw tag stream.
	std::vector<DrawTag> draw_tags;

	// /// The draw data stream.
	std::vector<uint32_t> draw_data; // TODO: (tig) we should use float here, so that it becomes easier to debug -- we can still do comparisons by bitfields

	// /// The transform stream.
	std::vector<Transform2D> transforms;

	// /// The style stream
	std::vector<Style> styles;

	// /// Late bound resource data.
	Resources resources; // TODO: this is not used for now.

	// /// Number of encoded paths.
	uint32_t n_paths;

	// /// Number of encoded path segments.
	uint32_t n_path_segments;

	// /// Number of encoded clips/layers.
	uint32_t n_clips;

	// /// Number of unclosed clips/layers.
	uint32_t n_open_clips;

	// /// Flags that capture the current state of the encoding.
	uint32_t flags;

	static constexpr uint32_t FORCE_NEXT_TRANSFORM = 1;
	static constexpr uint32_t FORCE_NEXT_STYLE     = 2;

	// TODO (tig): we could have an atomic counter here that makes sure that there is only ever
	// one path encoder active; we should also assert that the atomic counter is 0 when
	// destroying the encoder_o.
};

// NOTE: this method should not need to be exposed - we can keep this internal to the
// 2d renderer... only the rasterizer needs to know how to encode the data. the data
// itself does not need to be exposed.
//
// static rasterizer_layout_data_t encoder_encode_to_bytes( le_2d_encoder_o const* e, std::vector<uint8_t>& bytes ) {
static bool encoder_encode_to_bytes( le_2d_encoder_o const* e, uint8_t* bytes, size_t* bytes_count, rasterizer_layout_data_t* p_layout ) {

#define vec_count_bytes( X ) \
	( X.size() * sizeof( decltype( X )::value_type ) )

#define append_to_stream( X )                              \
	{                                                      \
		size_t num_bytes = vec_count_bytes( X );           \
		memcpy( bytes + used_bytes, X.data(), num_bytes ); \
		used_bytes += num_bytes;                           \
	}

	assert( e );

	size_t n_path_tags     = e->path_tags.size() + e->n_open_clips;
	size_t path_tag_padded = align_up( n_path_tags, 4 * PATH_REDUCE_WG_SZ );

	// size_t path_tags_size  = n_path_tags * sizeof( decltype( e->path_tags )::value_type );
	size_t path_data_size  = e->path_data.size() * sizeof( decltype( e->path_data )::value_type );
	size_t draw_tags_size  = e->draw_tags.size() * sizeof( decltype( e->draw_tags )::value_type );
	size_t open_clips_size = e->n_open_clips * sizeof( decltype( e->draw_tags )::value_type );
	size_t draw_data_size  = e->draw_data.size() * sizeof( decltype( e->draw_data )::value_type );
	size_t transforms_size = e->transforms.size() * sizeof( decltype( e->transforms )::value_type );
	size_t styles_size     = e->styles.size() * sizeof( decltype( e->styles )::value_type );

	size_t buffer_size =
	    path_tag_padded +
	    path_data_size +
	    draw_tags_size + open_clips_size +
	    draw_data_size +
	    transforms_size +
	    styles_size;

	if ( nullptr == bytes_count ) {
		return false;
	}

	if ( *bytes_count < buffer_size ) {
		*bytes_count = buffer_size;
		return false;
	}

	if ( nullptr == bytes ) {
		return false;
	}

	if ( nullptr == p_layout ) {
		return 0;
	}

	size_t used_bytes = 0;

	rasterizer_layout_data_t& layout = *p_layout;

	layout = {
	    .n_paths = e->n_paths,
	    .n_clips = e->n_clips,
	};

	layout.path_tag_base = used_bytes;

	append_to_stream( e->path_tags );

	if ( e->n_open_clips ) {
		// Append any open clips as pathtag::Path
		std::vector<PathTag> tmpOpenClips( e->n_open_clips, { PathTag::PATH } );
		append_to_stream( tmpOpenClips );
	}

	assert( align_up( used_bytes, 4 * PATH_REDUCE_WG_SZ ) == path_tag_padded );

	used_bytes = path_tag_padded;

	layout.path_data_base = used_bytes / sizeof( uint32_t );

	append_to_stream( e->path_data );

	layout.draw_tag_base = used_bytes / sizeof( uint32_t );

	{
		layout.bin_data_start = 0;

		for ( auto const& t : e->draw_tags ) {
			layout.bin_data_start += draw_tag_get_info_size( t );
		}
	}

	append_to_stream( e->draw_tags );

	if ( e->n_open_clips ) {

		// Append any open clips as pathtag::Path
		std::vector<DrawTag> tmpOpenClips( e->n_open_clips, { DrawTag::END_CLIP } );

		append_to_stream( tmpOpenClips );
	}

	// draw data stream

	layout.draw_data_base = used_bytes / sizeof( uint32_t );
	append_to_stream( e->draw_data );

	layout.transform_base = used_bytes / sizeof( uint32_t );

	append_to_stream( e->transforms );

	layout.style_base = used_bytes / sizeof( uint32_t );

	append_to_stream( e->styles );

	layout.n_drawobj = layout.n_paths;

	assert( used_bytes == buffer_size );

#undef vec_count_bytes
#undef append_to_stream

	*bytes_count = used_bytes;
	return true;
}

// ----------------------------------------------------------------------

static void encoder_encode_style( le_2d_encoder_o* e, Style const& style ) {
	if ( ( e->flags & le_2d_encoder_o::FORCE_NEXT_STYLE ) || e->styles.empty() || e->styles.back() != style ) {
		e->path_tags.emplace_back( PathTag::STYLE );
		e->styles.emplace_back( style );
		e->flags &= !le_2d_encoder_o::FORCE_NEXT_STYLE; // unset force flag
	}
}

// ----------------------------------------------------------------------

static void encoder_encode_colour( le_2d_encoder_o* e, uint32_t color ) {
	e->draw_tags.emplace_back( DrawTag::COLOUR );
	e->draw_data.emplace_back( color );
}

// ----------------------------------------------------------------------

static bool encoder_encode_transform( le_2d_encoder_o* e, Transform2D const* t ) {
	if ( ( e->flags & le_2d_encoder_o::FORCE_NEXT_TRANSFORM ) || e->transforms.empty() || e->transforms.back() != *t ) {

		e->path_tags.emplace_back( PathTag::TRANSFORM );
		e->transforms.emplace_back( *t );
		e->flags &= !le_2d_encoder_o::FORCE_NEXT_TRANSFORM; // unset force flag

		return true;
	}
	return false;
}

// ----------------------------------------------------------------------

static void encoder_encode_stroke_style( le_2d_encoder_o* e, Stroke const* stroke ) {
	encoder_encode_style( e, Style::from_stroke( *stroke ) );
}

// ----------------------------------------------------------------------

static void encoder_encode_fill_style( le_2d_encoder_o* e, FillStyle fill ) {
	encoder_encode_style( e, Style::from_fill( fill ) );
}

// ----------------------------------------------------------------------

static void encoder_encode_begin_clip( le_2d_encoder_o* e, BlendMode const* blend_mode, float alpha ) {
	e->draw_tags.emplace_back( DrawTag::BEGIN_CLIP );
	DrawBeginClip db{ *blend_mode, alpha };
	e->draw_data.insert(
	    e->draw_data.end(),
	    {
	        db.blend_mode,
	        db.alpha.as_uint32,
	    } );
	e->n_clips++;
	e->n_open_clips++;
}

// ----------------------------------------------------------------------

static void encoder_encode_end_clip( le_2d_encoder_o* e ) {
	if ( e->n_open_clips ) {
		e->draw_tags.emplace_back( DrawTag::END_CLIP );
		// dummy path (todo: can we do without this?)
		e->path_tags.emplace_back( PathTag::PATH );
		e->n_paths++;
		e->n_clips++;
		e->n_open_clips--;
	}
}

// ----------------------------------------------------------------------

// ----

static void     encoder_path_close( le_2d_encoder_o* e );                                            // ffdecl;
static void     encoder_path_insert_stroke_cap_marker_segment( le_2d_encoder_o* e, bool is_closed ); // ffdecl
static uint32_t encoder_path_end( le_2d_encoder_o* e, bool insert_path_marker );

static const float EPSILON = 1e-12;
// ----

bool path_encoder_get_last_point( le_2d_encoder_o const* e, glm::vec2& pt ) {
	size_t sz = e->path_data.size();
	if ( sz < 2 ) {
		return false;
	} else {
		memcpy( &pt, e->path_data.data() - 2, sizeof( pt ) );
		return true;
	}
}

static bool is_zero_length_segment( le_2d_encoder_o const* e, glm::vec2 const& p1, glm::vec2 const* maybe_p2, glm::vec2 const* maybe_p3 ) {
	glm::vec2 p0;
	if ( false == path_encoder_get_last_point( e, p0 ) ) {
		return false;
	}

	glm::vec2 p2 = ( maybe_p2 ) ? *maybe_p2 : p1;
	glm::vec2 p3 = ( maybe_p3 ) ? *maybe_p3 : p1;

	float x_min = fminf( p0.x, fminf( p1.x, fminf( p2.x, p3.x ) ) );
	float x_max = fmaxf( p0.x, fmaxf( p1.x, fmaxf( p2.x, p3.x ) ) );
	float y_min = fminf( p0.y, fminf( p1.y, fminf( p2.y, p3.y ) ) );
	float y_max = fmaxf( p0.y, fmaxf( p1.y, fmaxf( p2.y, p3.y ) ) );

	return !( x_max - x_min > EPSILON || y_max - y_min > EPSILON );
}

static bool start_tangent_for_line( le_2d_encoder_o const* self, glm::vec2 const& p1, glm::vec2& pt ) {
	glm::vec2 const p0 = self->path.first_point;

	if ( fabs( p1.x - p0.x ) > EPSILON || fabs( p1.y - p0.y ) > EPSILON ) {
		pt = {
		    p0.x + 1.f / 3.f * ( p1.x - p0.x ),
		    p0.y + 1.f / 3.f * ( p1.y - p0.y ),
		};
		return true;
	}

	return false;
}

static bool start_tangent_for_quad( le_2d_encoder_o const* self, glm::vec2 const& p1, glm::vec2 const& p2, glm::vec2& pt ) {
	auto const p0 = self->path.first_point;

	if ( fabs( p1.x - p0.x ) > EPSILON || fabs( p1.y - p0.y ) > EPSILON ) {
		pt = {
		    p1.x + 1.f / 3.f * ( p0.x - p1.x ),
		    p1.y + 1.f / 3.f * ( p0.y - p1.y ),
		};
		return true;
	} else if ( fabs( p2.x - p0.x ) > EPSILON || fabs( p2.y - p0.y ) > EPSILON ) {
		pt = {
		    p1.x + 1.f / 3.f * ( p2.x - p1.x ),
		    p1.y + 1.f / 3.f * ( p2.y - p1.y ),
		};
		return true;
	}

	return false;
}

// Returns the end point of the start tangent of a curve starting at `(x0, y0)`, or `None` if the
// curve is degenerate / has zero-length. The inputs are a sequence of control points that
// represent a cubic Bezier.
static bool start_tangent_for_curve( le_2d_encoder_o const* self, glm::vec2 const& p1, glm::vec2 const& p2, glm::vec2 const& p3, glm::vec2& pt ) {
	glm::vec2 const p0 = self->path.first_point;
	if ( fabs( p1.x - p0.x ) > EPSILON || fabs( p1.y - p0.y ) > EPSILON ) {
		pt = p1;
		return true;
	} else if ( fabs( p2.x - p0.x ) > EPSILON || fabs( p2.y - p0.y ) > EPSILON ) {
		pt = p2;
		return true;
	} else if ( fabs( p3.x - p0.x ) > EPSILON || fabs( p3.y - p0.y ) > EPSILON ) {
		pt = p3;
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------

static void encoder_path_move_to( le_2d_encoder_o* e, glm::vec2 const& p ) {

	if ( e->path.is_fill ) {
		encoder_path_close( e );
	}

	if ( e->path.state == path_encoder_o::eMoveTo ) {
		// if there is a moveto already, we just want to replace it
		e->path_data.resize( e->path_data.size() - 2 );
	} else if ( e->path.state == path_encoder_o::eNonemptySubpath ) {
		if ( false == e->path.is_fill ) {
			encoder_path_insert_stroke_cap_marker_segment( e, false );
		}
		if ( false == e->path_tags.empty() ) {
			e->path_tags.back().set_subpath_end();
		}
	}

	e->path.first_point = p;

	e->path_data.insert(
	    e->path_data.end(),
	    {
	        p.x,
	        p.y,
	    } );

	e->path.state = path_encoder_o::eMoveTo;
}

// ----------------------------------------------------------------------

static void encoder_path_begin( le_2d_encoder_o* e, bool is_fill ) {
	if ( e->path.state != path_encoder_o::eStart ) {
		encoder_path_end( e, true );
	}
	e->path.is_fill = is_fill;
}

// ----------------------------------------------------------------------

static void encoder_path_line_to( le_2d_encoder_o* e, glm::vec2 const& p ) {

	if ( e->path.state == path_encoder_o::eStart ) {
		if ( e->path.n_encoded_segments == 0 ) {
			// mirror the behaviour of kurbo which treats an initial line, quad or curve as a move
			encoder_path_move_to( e, p );
			return;
		}
		encoder_path_move_to( e, e->path.first_point );
	}

	if ( e->path.state == path_encoder_o::eMoveTo ) {
		// Make sure that we don't end up with a zero-length start tangent
		//
		// NOTE: this sets first_start_tangent_end as a side-effect if true
		if ( false == start_tangent_for_line( e, p, e->path.first_start_tangent_end ) ) {
			return;
		}
	}

	// Drop the segment if its length is zero
	if ( is_zero_length_segment( e, p, nullptr, nullptr ) ) {
		return;
	}

	e->path_data.insert(
	    e->path_data.end(),
	    {
	        p.x,
	        p.y,
	    } );

	e->path_tags.emplace_back( PathTag::LINE_TO_F32 );

	e->path.state = path_encoder_o::eNonemptySubpath;
	e->path.n_encoded_segments++;
}

// ----------------------------------------------------------------------

static void encoder_path_quad_to( le_2d_encoder_o* e, glm::vec2 const& c1, glm::vec2 const& p ) {

	if ( e->path.state == path_encoder_o::eStart ) {
		if ( 0 == e->path.n_encoded_segments ) {
			encoder_path_move_to( e, p );
			return;
		}
		encoder_path_move_to( e, e->path.first_point );
	}

	if ( e->path.state == path_encoder_o::eMoveTo ) {
		// Make sure that we don't end up with a zero-length start tangent
		if ( false == start_tangent_for_quad( e, c1, p, e->path.first_start_tangent_end ) ) {
			return;
		};
	}

	// Drop the segment if the length is zero
	if ( is_zero_length_segment( e, c1, &p, nullptr ) ) {
		return;
	}

	e->path_data.insert(
	    e->path_data.end(),
	    {
	        c1.x,
	        c1.y,
	        //
	        p.x,
	        p.y,
	    } );

	e->path_tags.emplace_back( PathTag::QUAD_TO_F32 );

	e->path.state = path_encoder_o::eNonemptySubpath;
	e->path.n_encoded_segments++;
}

// ----------------------------------------------------------------------

static void encoder_path_cubic_to( le_2d_encoder_o* e, glm::vec2 const& c1, glm::vec2 const& c2, glm::vec2 const& p ) {

	if ( e->path.state == path_encoder_o::eStart ) {
		if ( 0 == e->path.n_encoded_segments ) {
			encoder_path_move_to( e, p );
			return;
		}
		encoder_path_move_to( e, e->path.first_point );
	}
	if ( e->path.state == path_encoder_o::eMoveTo ) {
		if ( false == start_tangent_for_curve( e, c1, c2, p, e->path.first_start_tangent_end ) ) {
			return;
		};
	}
	// drop the segment if its length is zero
	if ( is_zero_length_segment( e, c1, &c2, &p ) ) {
		return;
	}

	e->path_data.insert(
	    e->path_data.end(),
	    {
	        c1.x,
	        c1.y,
	        //
	        c2.x,
	        c2.y,
	        //
	        p.x,
	        p.y,
	    } );

	e->path_tags.emplace_back( PathTag::CUBIC_TO_F32 );

	e->path.state = path_encoder_o::eNonemptySubpath;
	e->path.n_encoded_segments++;
}

// ----------------------------------------------------------------------

static void encoder_path_insert_stroke_cap_marker_segment( le_2d_encoder_o* e, bool is_closed ) {

	assert( !e->path.is_fill );
	assert( e->path.state == path_encoder_o::eNonemptySubpath );

	if ( is_closed ) {
		// We expect that the most recently encoded pair of coordinates in the path data stream
		// contain the first control point in the path segment (see `PathEncoder::close`).
		// Hence a line-to encoded here should embed the subpath's start tangent.
		encoder_path_line_to( e, e->path.first_start_tangent_end );
	} else {
		encoder_path_quad_to( e, e->path.first_point, e->path.first_start_tangent_end );
	}
}

// ----------------------------------------------------------------------
// close the current subpath

static void encoder_path_close( le_2d_encoder_o* e ) {

	size_t data_sz = e->path_data.size();

	switch ( e->path.state ) {
	case path_encoder_o::eStart:
		return;
	case path_encoder_o::eMoveTo:
		// if a new-opened path is being closed, we want to delete it.
		e->path_data.resize( data_sz - 2 ); // TODO: data here is assumed to be two uint32_t
		e->path.state = path_encoder_o::eStart;
		return;
	case path_encoder_o::eNonemptySubpath:
		break;
	}

	if ( data_sz < 2 ) {
		assert( false && "This is an open path, there must be data" );
		return;
	}

	// invariant: sz >=2

	// if the last two points in data are not the same as the first two points in data
	// then add the first point to the end of the data

	if ( 0 != memcmp( &e->path.first_point, e->path_data.data() + data_sz - 2, sizeof( e->path.first_point ) ) ) {
		e->path_data.insert(
		    e->path_data.end(),
		    {
		        e->path.first_point.x,
		        e->path.first_point.y,
		    } );

		e->path_tags.emplace_back( PathTag::LINE_TO_F32 );
		e->path.n_encoded_segments++;
	}

	if ( false == e->path.is_fill ) {
		encoder_path_insert_stroke_cap_marker_segment( e, true );
	}

	if ( false == e->path_tags.empty() ) {
		e->path_tags.back().set_subpath_end();
	}

	e->path.state = path_encoder_o::eStart;
}

// ----------------------------------------------------------------------
/// Completes path encoding and returns the actual number of encoded segments.
///
/// If `insert_path_marker` is true, encodes the [`PathTag::PATH`] tag to signify
/// the end of a complete path object. Setting this to false allows encoding
/// multiple paths with differing transforms for a single draw object.
static uint32_t encoder_path_end( le_2d_encoder_o* e, bool insert_path_marker ) {

	if ( e->path.is_fill ) {
		encoder_path_close( e );
	}
	if ( e->path.state == path_encoder_o::eMoveTo ) {
		// if there is an orphaned moveto instruction, then remove it
		e->path_data.resize( e->path_data.size() - 2 );
	}
	if ( e->path.n_encoded_segments != 0 ) {
		if ( !e->path.is_fill && e->path.state == path_encoder_o::eNonemptySubpath ) {
			encoder_path_insert_stroke_cap_marker_segment( e, false );
		}
		if ( !e->path_tags.empty() ) {
			e->path_tags.back().set_subpath_end();
		}
		e->n_path_segments += e->path.n_encoded_segments;
		if ( insert_path_marker ) {
			e->path_tags.emplace_back( PathTag::PATH );
			e->n_paths++;
		}
	}

	uint32_t encoded_segments = e->path.n_encoded_segments;

	// reset the encoder path automatically after finish
	e->path = {};

	return encoded_segments;
}

// ----------------------------------------------------------------------

static void encoder_path_circle( le_2d_encoder_o* e, glm::vec2 const& centre, float r, float tolerance = 0.1 ) {

	// This method was adapted from kurbo-0.11.2/src/circle.rs
	//
	// Copyright 2019 the Kurbo Authors
	// SPDX-License-Identifier: Apache-2.0 OR MIT
	//

	static constexpr auto FRAC_PI_2  = 1.57079632679489661923132169163975144; // 1.57079637f32
	static constexpr auto TWO_PI     = 6.28318530717958647692528676655900576;
	double                scaled_err = fabs( r ) / tolerance;
	size_t                n          = 0;
	double                arm_len    = 0;
	if ( scaled_err < 1.9608e-4 ) {
		// Solution from http://spencermortensen.com/articles/bezier-circle/
		n       = 4;
		arm_len = 0.551915024494;
	} else {
		// This is empirically determined to fall within error tolerance.
		n = ceil( powf( 1.1163 * scaled_err, 1.0 / 6.0 ) );
		// Note: this isn't minimum error, but it is simple and we can easily
		// estimate the error.
		arm_len = ( 4.0 / 3.0 ) * tanf( FRAC_PI_2 / ( double( n ) ) );
	}

	double delta_th = TWO_PI / double( n );

	encoder_path_move_to( e, { centre.x + r, centre.y } );

	for ( size_t i = 1; i <= n; i++ ) {
		double& a   = arm_len;
		float   x   = centre.x;
		float   y   = centre.y;
		double  th1 = delta_th * i;
		double  th0 = th1 - delta_th;
		double  s0  = sinf( th0 );
		double  c0  = cosf( th0 );

		double s1;
		double c1;
		if ( i == n ) {
			s1 = 0;
			c1 = 1.0;
		} else {
			s1 = sin( th1 );
			c1 = cos( th1 );
		}

		encoder_path_cubic_to( e,
		                       { float( x + r * ( c0 - a * s0 ) ), float( y + r * ( s0 + a * c0 ) ) },
		                       { float( x + r * ( c1 + a * s1 ) ), float( y + r * ( s1 - a * c1 ) ) },
		                       { float( x + r * c1 ), float( y + r * s1 ) } );
	}

	encoder_path_close( e );
}

static le_2d_encoder_o* encoder_create() {
	return new le_2d_encoder_o{};
}

static void encoder_reset( le_2d_encoder_o* self ) {
	self = {};
}
// ----------------------------------------------------------------------

static void encoder_destroy( le_2d_encoder_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

void register_le_2d_encoder_api( void* api_ ) {

	auto api_i = static_cast<le_2d_api*>( api_ );

	auto& encoder_i = api_i->le_2d_encoder_i;

	encoder_i.create  = encoder_create;
	encoder_i.destroy = encoder_destroy;
	encoder_i.reset   = encoder_reset;
	//

	encoder_i.encode_colour       = encoder_encode_colour;
	encoder_i.encode_transform    = encoder_encode_transform;
	encoder_i.encode_stroke_style = encoder_encode_stroke_style;
	encoder_i.encode_fill_style   = encoder_encode_fill_style;
	encoder_i.encode_begin_clip   = encoder_encode_begin_clip;
	encoder_i.encode_end_clip     = encoder_encode_end_clip;

	//
	encoder_i.encode_to_bytes = encoder_encode_to_bytes;

	//
	encoder_i.path_end      = encoder_path_end;
	encoder_i.path_close    = encoder_path_close;
	encoder_i.path_cubic_to = encoder_path_cubic_to;
	encoder_i.path_quad_to  = encoder_path_quad_to;
	encoder_i.path_move_to  = encoder_path_move_to;
	encoder_i.path_line_to  = encoder_path_line_to;
	encoder_i.path_begin    = encoder_path_begin;

	//

	encoder_i.path_circle = encoder_path_circle;

	// TODO:
	// - add the rest of the encoder functions,
	// - then add the encode_to_bytes function -- although this one might just want to be used internally...
	// - bonus: add functions to encode circles, and perhaps even arcs
}
