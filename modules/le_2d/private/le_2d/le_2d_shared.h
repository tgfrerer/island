#pragma once

#include "le_2d.h"
#include <stdint.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
// ----------------------------------------------------------------------
#include "glm/vec2.hpp"

static constexpr uint32_t PATH_REDUCE_WG_SZ = 256;


struct rasterizer_layout_data_t {
	uint32_t n_drawobj;      /// number of draw objects
	uint32_t n_paths;        /// number of paths
	uint32_t n_clips;        /// number of clips
	uint32_t bin_data_start; /// start of binning data
	uint32_t path_tag_base;  /// start of path tag stream
	uint32_t path_data_base; /// start of path data stream
	uint32_t draw_tag_base;  /// start of draw tag stream
	uint32_t draw_data_base; /// start of draw data stream
	uint32_t transform_base; /// start of transform stream
	uint32_t style_base;     /// start of style stream
};

enum class PathTag : uint8_t {
	/// Bit for path segments that are represented as f32 values. If unset
	/// they are represented as i16.
	F32_BIT = 0x8,
	/// Mask for bottom 3 bits that contain the [`PathSegmentType`].
	SEGMENT_MASK = 0x3,

	/// 16-bit integral line segment.
	LINE_TO_I16 = uint8_t( 1 ),

	/// 16-bit integral quadratic segment.
	QUAD_TO_I16 = uint8_t( 2 ),

	/// 16-bit integral cubic segment.
	CUBIC_TO_I16 = uint8_t( 3 ),

	/// 32-bit floating point line segment.
	LINE_TO_F32 = LINE_TO_I16 | F32_BIT,

	/// 32-bit floating point quadratic segment.
	QUAD_TO_F32 = QUAD_TO_I16 | F32_BIT,

	/// 32-bit floating point cubic segment.
	CUBIC_TO_F32 = CUBIC_TO_I16 | F32_BIT,

	/// Transform marker.
	TRANSFORM = 0x20,

	/// Path marker.
	PATH = 0x10,

	/// Style setting.
	STYLE = 0x40,

	/// Bit that marks a segment that is the end of a subpath.
	SUBPATH_END_BIT = 0x4,
};

// static inline PathSegmentType get_path_segment_type( PathTag const& e ) {
// 	return PathSegmentType( uint8_t( e ) & uint8_t( PathTag::SEGMENT_MASK ) );
// }

// ----------------------------------------------------------------------

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

	static Style from_fill( le_2d::FillStyle const& fill ) {
		return Style{
		    .flags_and_miter_limit = ( fill == le_2d::FillStyle::EvenOdd ) ? FLAGS_FILL_BIT : 0,
		    .line_width            = 0.f,
		};
	};

	static Style from_stroke( le_2d::Stroke const& stroke ) {
		uint32_t style     = FLAGS_STYLE_BIT;
		uint32_t join      = 0;
		uint32_t start_cap = 0;
		uint32_t end_cap   = 0;
		// clang-format off
		switch ( stroke.join ) {
			case le_2d::Join::eBevel: join = FLAGS_JOIN_BITS_BEVEL; break;
			case le_2d::Join::eMiter: join = FLAGS_JOIN_BITS_MITER; break;
			case le_2d::Join::eRound: join = FLAGS_JOIN_BITS_ROUND; break;
		};
		switch ( stroke.start_cap ) {
			case le_2d::Cap::eButt:   start_cap = FLAGS_START_CAP_BITS_BUTT; break;
			case le_2d::Cap::eRound:  start_cap = FLAGS_START_CAP_BITS_ROUND; break;
			case le_2d::Cap::eSquare: start_cap = FLAGS_START_CAP_BITS_SQUARE; break;
		};
		switch ( stroke.end_cap) {
			case le_2d::Cap::eButt:   end_cap = FLAGS_END_CAP_BITS_BUTT; break;
			case le_2d::Cap::eRound:  end_cap = FLAGS_END_CAP_BITS_ROUND; break;
			case le_2d::Cap::eSquare: end_cap = FLAGS_END_CAP_BITS_SQUARE; break;
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



struct Patch {
	enum class Type : uint8_t {
		Undefined = 0,
		Ramp,
		// GlyphRun, // NOT YET IMPLEMENTED
		// Image,    // NOT YET IMPLEMENTED
	};
	Type type = {};

	struct RampData {
		size_t     draw_data_offset = 0; // given in count of uint32_t
		size_t     stops_start;
		size_t     stops_end;
		le_2d_api::ExtendMode extend;
	};

	struct GlyphRunData {
		size_t index; // index  in the glyph run buffer
	};

	struct ImageData {
		size_t                             draw_data_offset; // offset to the atlas coordinates in the draw data stream
		struct le_image_resource_handle_t* image_handle;     // NOT YET IMPLEMENTED
	};

	union Data {
		RampData     as_ramp;
		GlyphRunData as_glyph_run;
		ImageData    as_image;
	} data = {};
};

struct ResolvedPatch {
	enum class Type : uint8_t {
		Undefined = 0,
		Ramp,
		// GlyphRun, // NOT YET IMPLEMENTED
		// Image,    // NOT YET IMPLEMENTED
	};
	Type type = {};

	struct RampData {
		size_t     draw_data_offset = 0; // given in count of uint32_t
		uint32_t   ramp_id;              // resolved ramp index (=line number into ramp image)
		le_2d_api::ExtendMode extend;
	};

	struct GlyphRunData {
		size_t      index; // index of the glyph in the glyph run buffer
		size_t      glyphs_start;
		size_t      glyphs_end; // range into the glyphs encoding range buffer
		Transform2D transform;  // global transform
		float       scale;      // additional scale factor
		bool        hint;       // whether the glyph was hinted
	};

	struct ImageData {
		// FIXME: NOT YET IMPLEMENTED
		size_t index;            // index of pending image element
		size_t draw_data_offset; // offset to the atlas location in the draw data stream
	};

	union Data {
		RampData     as_ramp;
		GlyphRunData as_glyph_run;
		ImageData    as_image;
	} data = {};
};

// ----------------------------------------------------------------------

struct DrawBeginClip {

	uint32_t blend_mode;
	union {
		float    as_float;
		uint32_t as_uint32;
	} alpha;

	DrawBeginClip( le_2d::BlendMode const& b, float a )
	    : blend_mode( uint32_t( b.mix ) << 8 | uint32_t( b.compose ) )
	    , alpha( a ) {
	    };
};

struct Resources {
	// Fill this in once we want to make more advanced rendering available.
	// this is for gradients, and images, and glyph runs.
	std::vector<le_2d_colour_stop_t> colour_stops;
	std::vector<Patch>               patches;
};

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

struct le_2d_encoder_o : NoCopy, NoMove {

	path_encoder_o path = {};

	// /// The path tag stream.
	std::vector<PathTag> path_tags;

	// /// The path data stream.
	// /// Stores all coordinates on paths.
	std::vector<float> path_data;

	// /// The draw tag stream.
	std::vector<DrawTag> draw_tags;

	// /// The draw data stream.
	std::vector<uint32_t> draw_data;

	// /// The transform stream.
	std::vector<Transform2D> transforms;

	// /// The style stream
	std::vector<Style> styles;

	// /// Late bound resource data.
	Resources resources; ///< TODO: resources need to be cleared at some point?

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
