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

#include "le_log.h"
#include "private/le_2d/le_2d_shared.h"

// ----------------------------------------------------------------------

static constexpr auto FRAC_PI_2 = 1.57079632679489661923132169163975144; // 1.57079637f32
static constexpr auto TWO_PI    = 6.28318530717958647692528676655900576;

static auto logger() {
	static auto logger = le::Log( "le_2d_encoder" );
	return logger;
}

using le_2d_colour_stop_t     = le_2d_api::le_2d_colour_stop_t;
using le_2d_linear_gradient_t = le_2d_api::le_2d_linear_gradient_t;
using ExtendMode              = le_2d_api::ExtendMode;

// ----------------------------------------------------------------------

static inline bool is_path_segment( PathTag const& e ) {
	return uint8_t( e ) & uint8_t( PathTag::SEGMENT_MASK );
}

static inline bool is_subpath_end( PathTag const& e ) {
	return uint8_t( e ) & uint8_t( PathTag::SUBPATH_END_BIT );
}

static inline void set_subpath_end( PathTag& e ) {
	e = PathTag( uint8_t( e ) | uint8_t( PathTag::SUBPATH_END_BIT ) );
}

static inline bool is_f32( PathTag const& e ) {
	return uint8_t( e ) & uint8_t( PathTag::F32_BIT );
}

using namespace le_2d;
// ----------------------------------------------------------------------

static void encoder_reset( le_2d_encoder_o* e ) {
	e->path = {};
	e->path_tags.clear();
	e->path_data.clear();
	e->draw_tags.clear();
	e->draw_data.clear();
	e->transforms.clear();
	e->styles.clear();
	// e->resources       = {};
	e->n_paths         = 0;
	e->n_path_segments = 0;
	e->n_clips         = 0;
	e->n_open_clips    = 0;
	e->flags           = 0;
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
		e->path_tags.emplace_back( PathTag::PATH );
		e->n_paths++;
		e->n_clips++;
		e->n_open_clips--;
	}
}

// ----------------------------------------------------------------------

static void encoder_encode_linear_gradient( le_2d_encoder_o* e, le_2d_linear_gradient_t const* gradient, le_2d_colour_stop_t const* colour_stops, size_t colour_stops_sz, float alpha, ExtendMode extend ) {

	// Special cases:
	//
	// + if zero stops, then encode transparent colour, and encode as solid shape
	// + if one stop, just encode a colour and ignore gradient treatment, encode as solid shape

	if ( colour_stops_sz == 0 ) {
		encoder_encode_colour( e, 0x00000000 ); // encode transparent colour
		return;
	} else if ( colour_stops_sz == 1 ) {
		encoder_encode_colour( e, colour_stops[ 0 ].colour.to_premult_rgba_u32() ); // encode solid colour
		return;
	}

	// ----------| invariant: more than one colour stops

	// This is actually a gradient

	size_t offset = e->draw_data.size(); // NOTE granularity is uint32_t

	// Append colour stops
	size_t stops_start = e->resources.colour_stops.size();
	e->resources.colour_stops.insert( e->resources.colour_stops.end(), colour_stops, colour_stops + colour_stops_sz );
	size_t stops_end = e->resources.colour_stops.size();

	// If alpha is not 1.0, then we must premultiply alpha for all added colour stops
	// we do this after appending so that we don't have to allocate temporary objects.
	if ( alpha != 1.0 ) {
		for ( auto col = e->resources.colour_stops.end() - colour_stops_sz; col != e->resources.colour_stops.end(); col++ ) {
			col->colour.r *= alpha;
			col->colour.g *= alpha;
			col->colour.b *= alpha;
		}
	}

	// Push patch for ramp

	Patch p{
	    .type = Patch::Type::Ramp,
	    .data = {
	        .as_ramp = {
	            .draw_data_offset = offset,
	            .stops_start      = stops_start,
	            .stops_end        = stops_end,
	            .extend           = extend,
	        },

	    },
	};

	e->resources.patches.emplace_back( std::move( p ) );

	e->draw_tags.push_back( DrawTag::LINEAR_GRADIENT );

	// now, we must serialise the gradient into draw data - first allocate some memory inside draw data
	e->draw_data.resize( offset + sizeof( le_2d_linear_gradient_t ) / sizeof( uint32_t ) );
	// copy gradient into draw_data
	memcpy( e->draw_data.data() + offset, gradient, sizeof( le_2d_linear_gradient_t ) );
};
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
		memcpy( &pt, e->path_data.data() + e->path_data.size() - 2, sizeof( pt ) );
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
			set_subpath_end( e->path_tags.back() );
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
		set_subpath_end(e->path_tags.back() );
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
			set_subpath_end( e->path_tags.back() );
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

// ----------------------------------------------------------------------

static void encoder_path_rect( le_2d_encoder_o* e, glm::vec2 const& top_left, glm::vec2 const& bottom_right ) {
	encoder_path_move_to( e, top_left );
	encoder_path_line_to( e, { bottom_right.x, top_left.y } );
	encoder_path_line_to( e, bottom_right );
	encoder_path_line_to( e, { top_left.x, bottom_right.y } );
	encoder_path_close( e );
};

// ----------------------------------------------------------------------

/// Rotate `pt` about the origin by `angle` radians.
static inline glm::vec2 rotate_pt( glm::vec2 const& pt, double angle ) {
	// This method was adapted from kurbo-0.11.2/src/arc.rs
	//
	// Copyright 2019 the Kurbo Authors
	// SPDX-License-Identifier: Apache-2.0 OR MIT
	//

	double angle_sin = sin( angle );
	double angle_cos = cos( angle );

	return {
	    pt.x * angle_cos - pt.y * angle_sin,
	    pt.x * angle_sin + pt.y * angle_cos,
	};
}

/// Take the ellipse radii, how the radii are rotated, and the sweep angle, and return a point on
/// the ellipse.
static glm::vec2 sample_ellipse( glm::vec2 const& radii, double x_rotation, double angle ) {
	// This method was adapted from kurbo-0.11.2/src/arc.rs
	//
	// Copyright 2019 the Kurbo Authors
	// SPDX-License-Identifier: Apache-2.0 OR MIT
	//
	double angle_sin = sin( angle );
	double angle_cos = cos( angle );
	double u         = radii.x * angle_cos;
	double v         = radii.y * angle_sin;
	return rotate_pt( glm::vec2( u, v ), x_rotation );
}

// ----------------------------------------------------------------------

static void encoder_path_arc( le_2d_encoder_o* e, glm::vec2 const& centre, glm::vec2 const& radii, double start_angle_rad, double sweep_angle_rad, double x_rotation_rad, float tolerance = 0.1 ) {

	// This method was adapted from kurbo-0.11.2/src/arc.rs
	//
	// Copyright 2019 the Kurbo Authors
	// SPDX-License-Identifier: Apache-2.0 OR MIT
	//

	double sign       = std::copysign( 1.0, sweep_angle_rad );
	double scaled_err = std::max( radii.x, radii.y ) / tolerance;

	// Number of subdivisions per ellipse based on error tolerance.
	// Note: this may slightly underestimate the error for quadrants.
	double n_err = std::max( pow( 1.1163 * scaled_err, ( 1.0 / 6.0 ) ), 3.999999 );

	double n          = ceilf( n_err * fabs( sweep_angle_rad ) * ( 1.0 / ( TWO_PI ) ) );
	double angle_step = sweep_angle_rad / n;
	size_t n_sz       = n == n ? n : 0; // test for NaN
	double arm_len    = ( 4.0 / 3.0 ) * tan( fabs( ( 0.25 * angle_step ) ) ) * sign;
	double angle0     = start_angle_rad;

	glm::vec2 p0 = sample_ellipse( radii, x_rotation_rad, angle0 );

	// Move to first point if there is no earlier point in the current path
	if ( e->path.state == path_encoder_o::eStart ) {
		encoder_path_move_to( e, centre + p0 );
	} else {
		// if there was a previous path, we trace a line to the first point
		encoder_path_line_to( e, centre + p0 );
	}

	for ( int i = 0; i != n_sz; i++ ) {
		double angle1 = angle0 + angle_step;

		glm::vec2 p1 = p0 + float( arm_len ) * sample_ellipse( radii, x_rotation_rad, angle0 + FRAC_PI_2 );
		glm::vec2 p3 = sample_ellipse( radii, x_rotation_rad, angle1 );
		glm::vec2 p2 = p3 - float( arm_len ) * sample_ellipse( radii, x_rotation_rad, angle1 + FRAC_PI_2 );

		encoder_path_cubic_to( e, centre + p1, centre + p2, centre + p3 );

		angle0 = angle1;
		p0     = p3;
	}
}

// ----------------------------------------------------------------------
// Note that this assumes that both encoders, self, and rhs are in a valid state.
static void encoder_append_into_encoder( le_2d_encoder_o* self, le_2d_encoder_o const* rhs, Transform2D const* maybe_transform ) {

	Transform2D t = maybe_transform ? *maybe_transform : Transform2D();

	// TODO apply optional transform to all transform objects in rhs

	size_t old_transforms_count = self->transforms.size();
	self->transforms.insert( self->transforms.end(), rhs->transforms.begin(), rhs->transforms.end() );

	// Apply transform to all added elements
	for ( size_t i = old_transforms_count; i != self->transforms.size(); i++ ) {
		self->transforms[ i ] = t * self->transforms[ i ];
	}

	self->path_tags.insert( self->path_tags.end(), rhs->path_tags.begin(), rhs->path_tags.end() );
	self->path_data.insert( self->path_data.end(), rhs->path_data.begin(), rhs->path_data.end() );

	self->draw_tags.insert( self->draw_tags.end(), rhs->draw_tags.begin(), rhs->draw_tags.end() );
	self->draw_data.insert( self->draw_data.end(), rhs->draw_data.begin(), rhs->draw_data.end() );

	self->styles.insert( self->styles.end(), rhs->styles.begin(), rhs->styles.end() );

	self->n_paths += rhs->n_paths;
	self->n_path_segments += rhs->n_path_segments;
	self->n_clips += rhs->n_clips;
	self->n_open_clips += rhs->n_open_clips;

	self->flags |= rhs->flags;

	// reset path encoder -- just so that there is no accidental state leftover
	self->path = {};
};

// ----------------------------------------------------------------------

static le_2d_encoder_o* encoder_create() {
	le_2d_encoder_o* e = new le_2d_encoder_o();
	encoder_reset( e );
	return e;
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
	encoder_i.append_into_encoder = encoder_append_into_encoder;

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
	encoder_i.path_rect   = encoder_path_rect;
	encoder_i.path_arc    = encoder_path_arc;

	// TODO:
	// - add the rest of the encoder functions,
	// - then add the encode_to_bytes function -- although this one might just want to be used internally...
	// - bonus: add functions to encode circles, and perhaps even arcs
}
