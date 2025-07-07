#ifndef GUARD_le_2d_H
#define GUARD_le_2d_H

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
<https://github.com/linebender/vello/tree/main/vello_shaders/shader>

Vello shaders are licensed under Unilicense.
<https://github.com/linebender/vello/blob/main/vello_shaders/shader/UNLICENSE>

 */

#include "le_core.h"
#include <cstring>
#include "3rdparty/src/glm/fwd.hpp"

struct le_2d_o;
struct le_rendergraph_o;
struct le_image_resource_handle_t;
struct le_resource_info_t;

//
struct le_2d_encoder_o;
struct rasterizer_layout_data_t;

//
//

static constexpr size_t align_up( size_t n, size_t alignment ) {
	// n = ( ( n + ( alignment - 1 ) ) / alignment ) * alignment;
	n = n + ( -n & ( alignment - 1 ) );
	return n;
}

static_assert( align_up( 3, 4 ) == 4, "must produce the correct alignment" );
static_assert( align_up( 0, 4 ) == 0, "must produce the correct alignment" );
static_assert( align_up( 12, 4 ) == 12, "must produce the correct alignment" );
static_assert( align_up( 13, 4 ) == 16, "must produce the correct alignment" );

struct Transform2D {
	float transform[ 4 ]   = { 1, 0, 0, 1 }; // 2x2 matrix, column major
	float translation[ 2 ] = { 0, 0 };

	inline Transform2D operator*( Transform2D const& rhs ) const {
		auto const& t = this->transform;
		auto const& o = rhs.transform;
		return {
		    {
		        // transform
		        t[ 0 ] * o[ 0 ] + t[ 2 ] * o[ 1 ],
		        t[ 1 ] * o[ 0 ] + t[ 3 ] * o[ 1 ],
		        t[ 0 ] * o[ 2 ] + t[ 2 ] * o[ 3 ],
		        t[ 1 ] * o[ 2 ] + t[ 3 ] * o[ 3 ],
		    },
		    {
		        // translation
		        t[ 0 ] * rhs.translation[ 0 ] + t[ 2 ] * rhs.translation[ 1 ] + this->translation[ 0 ],
		        t[ 1 ] * rhs.translation[ 0 ] + t[ 3 ] * rhs.translation[ 1 ] + this->translation[ 1 ],
		    },
		};
	}

	const bool operator==( Transform2D const& rhs ) const {
		return ( 0 == memcmp( this, &rhs, sizeof( Transform2D ) ) );
	}

	const bool operator!=( Transform2D const& rhs ) const {
		return !( *this == rhs );
	}
};

// clang-format off
struct le_2d_api {

//

	struct BlendMode {
	
		enum class Mix : uint8_t {
			/// Default attribute which specifies no blending. The blending formula simply selects the source color.
			Normal = 0,
			/// Source color is multiplied by the destination color and replaces the destination.
			Multiply = 1,
			/// Multiplies the complements of the backdrop and source color values, then complements the result.
			Screen = 2,
			/// Multiplies or screens the colors, depending on the backdrop color value.
			Overlay = 3,
			/// Selects the darker of the backdrop and source colors.
			Darken = 4,
			/// Selects the lighter of the backdrop and source colors.
			Lighten = 5,
			/// Brightens the backdrop color to reflect the source color. Painting with black produces no
			/// change.
			ColorDodge = 6,
			/// Darkens the backdrop color to reflect the source color. Painting with white produces no
			/// change.
			ColorBurn = 7,
			/// Multiplies or screens the colors, depending on the source color value. The effect is
			/// similar to shining a harsh spotlight on the backdrop.
			HardLight = 8,
			/// Darkens or lightens the colors, depending on the source color value. The effect is similar
			/// to shining a diffused spotlight on the backdrop.
			SoftLight = 9,
			/// Subtracts the darker of the two constituent colors from the lighter color.
			Difference = 10,
			/// Produces an effect similar to that of the `Difference` mode but lower in contrast. Painting
			/// with white inverts the backdrop color; painting with black produces no change.
			Exclusion = 11,
			/// Creates a color with the hue of the source color and the saturation and luminosity of the
			/// backdrop color.
			Hue = 12,
			/// Creates a color with the saturation of the source color and the hue and luminosity of the
			/// backdrop color. Painting with this mode in an area of the backdrop that is a pure gray
			/// (no saturation) produces no change.
			Saturation = 13,
			/// Creates a color with the hue and saturation of the source color and the luminosity of the
			/// backdrop color. This preserves the gray levels of the backdrop and is useful for coloring
			/// monochrome images or tinting color images.
			Color = 14,
			/// Creates a color with the luminosity of the source color and the hue and saturation of the
			/// backdrop color. This produces an inverse effect to that of the `Color` mode.
			Luminosity = 15,
			/// `Clip` is the same as `Normal`, but the latter always creates an isolated blend group and the
			/// former can optimize that out.
			Clip = 128,
	
		};
	
		enum class Compose : uint8_t {
			/// No regions are enabled.
			Clear = 0,
			/// Only the source will be present.
			Copy = 1,
			/// Only the destination will be present.
			Dest = 2,
			/// The source is placed over the destination.
			SrcOver = 3,
			/// The destination is placed over the source.
			DestOver = 4,
			/// The parts of the source that overlap with the destination are placed.
			SrcIn = 5,
			/// The parts of the destination that overlap with the source are placed.
			DestIn = 6,
			/// The parts of the source that fall outside of the destination are placed.
			SrcOut = 7,
			/// The parts of the destination that fall outside of the source are placed.
			DestOut = 8,
			/// The parts of the source which overlap the destination replace the destination. The
			/// destination is placed everywhere else.
			SrcAtop = 9,
			/// The parts of the destination which overlaps the source replace the source. The source is
			/// placed everywhere else.
			DestAtop = 10,
			/// The non-overlapping regions of source and destination are combined.
			Xor = 11,
			/// The sum of the source image and destination image is displayed.
			Plus = 12,
			/// Allows two elements to cross fade by changing their opacities from 0 to 1 on one
			/// element and 1 to 0 on the other element.
			PlusLighter = 13,
		};
	
		Mix     mix     = Mix::Clip;
		Compose compose = Compose::SrcOver;
	};
	
	enum class FillStyle : uint8_t {
		NonZero = 0,
		EvenOdd = 1,
	};

	enum class Join : uint32_t {
		eBevel,
		eMiter,
		eRound,
	};
	
	enum class Cap : uint32_t {
		eButt,
		eSquare,
		eRound,
	};

	struct Stroke {
		/// Width of the stroke.
		double width = 1.0;
		/// Style for connecting segments of the stroke.
		Join join = Join::eRound;
		/// Limit for miter joins.
		double miter_limit = 4.0;
		/// Style for capping the beginning of an open subpath.
		Cap start_cap = Cap::eRound;
		/// Style for capping the end of an open subpath.
		Cap end_cap = Cap::eRound;
		/// Unsupported: Lengths of dashes in alternating on/off order.
		double dash_pattern[ 4 ] = { 0, 0, 0, 0 };
		/// Unsupported: Offset of the first dash.
		double dash_offset = { 0 };
	};


/*
 * 
 * INFO 
 * - the encoder is independent of the 2d context.
 * - Each encoder has a path internally that keeps track of the current 
 *   state for encoding a path.
 *   
 * TODO
 * - add a method to combine encoders (append from one onto the other, and apply last transform)
 * 
 */

	struct le_2d_encoder_interface_t {

		le_2d_encoder_o* (*create)();
		void ( *reset   )(le_2d_encoder_o* self );
		void ( *destroy )(le_2d_encoder_o* self );


		void (* encode_stroke_style )( le_2d_encoder_o* e, Stroke const* stroke);
		void (* encode_fill_style )( le_2d_encoder_o* e, FillStyle const fill );

		void (* encode_colour)(le_2d_encoder_o* self, uint32_t colour);
		bool (* encode_transform)(le_2d_encoder_o* e, Transform2D const *t);
 		void (* encode_begin_clip)( le_2d_encoder_o* e, BlendMode const* blend_mode, float alpha );
 		void (* encode_end_clip)( le_2d_encoder_o* e);

		// ---------- path methods

		void (* path_begin     )( le_2d_encoder_o* e, bool is_fill );
		uint32_t (* path_end )(le_2d_encoder_o* self, bool insert_path_marker);

		void (* path_move_to   )( le_2d_encoder_o* e, glm::vec2 const& p );
		void (* path_line_to   )( le_2d_encoder_o* e, glm::vec2 const& p );
		void (* path_quad_to   )( le_2d_encoder_o* e, glm::vec2 const& c1, glm::vec2 const& p );
		void (* path_cubic_to  )( le_2d_encoder_o* e, glm::vec2 const& c1, glm::vec2 const& c2, glm::vec2 const& p );
		void (* path_close     )( le_2d_encoder_o* self);


		// ---------- macro methods

		void (* path_circle ) (le_2d_encoder_o*e, glm::vec2 const & centre, float r, float tolerance);

		// ---------- private methods

		// todo: make this private - as it is only used by le_2d_o internally
		bool (* encode_to_bytes )( le_2d_encoder_o const* e, uint8_t* bytes, size_t* bytes_count, rasterizer_layout_data_t* p_layout );
	};

	struct le_2d_interface_t {

		le_2d_o* ( *create )();
		void     ( *destroy )( le_2d_o* self );
		void     ( *update  )( le_2d_o* self, le_rendergraph_o* rg, le_2d_encoder_o* encoder, le_image_resource_handle_t* img_output, le_resource_info_t* img_output_info, uint32_t background_colour_argb  );
	};

	le_2d_encoder_interface_t le_2d_encoder_i;
	le_2d_interface_t le_2d_i;
};
// clang-format on

LE_MODULE( le_2d );
LE_MODULE_LOAD_DEFAULT( le_2d );

#ifdef __cplusplus

namespace le_2d {
static const auto& api             = le_2d_api_i;
static const auto& le_2d_i         = api->le_2d_i;
static const auto& le_2d_encoder_i = api->le_2d_encoder_i;

using BlendMode = le_2d_api::BlendMode;
using FillStyle = le_2d_api::FillStyle;
using Stroke    = le_2d_api::Stroke;
using Join      = le_2d_api::Join;
using Cap       = le_2d_api::Cap;

} // namespace le_2d
class Le2D : NoCopy, NoMove {

	le_2d_o* self;

  public:
	Le2D()
	    : self( le_2d::le_2d_i.create() ) {
	}

	~Le2D() {
		le_2d::le_2d_i.destroy( self );
	}

	void update( le_rendergraph_o* rg, le_2d_encoder_o* encoder, le_image_resource_handle_t* img_output, le_resource_info_t* img_output_info, uint32_t background_colour_argb = 0xff000000 ) {
		le_2d::le_2d_i.update( self, rg, encoder, img_output, img_output_info, background_colour_argb );
	}

	inline static uint32_t colour_to_abgr( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
		uint32_t c =
		    ( uint32_t( a ) << 24 ) |
		    ( uint32_t( b ) << 16 ) |
		    ( uint32_t( g ) << 8 ) |
		    uint32_t( r );
		return c;
	};

	operator auto() {
		return self;
	}
};

class Encoder2D : NoCopy, NoMove {

  public:
	class Path : NoCopy, NoMove {

		Encoder2D& parent;

		Path( Encoder2D& e )
		    : parent( e ) {
			e.transform( {} ); // add an initial identity transform
		};

	  public:
		Path& move_to( glm::vec2 const& p ) {
			le_2d::le_2d_encoder_i.path_move_to( static_cast<le_2d_encoder_o*>( parent ), p );
			return *this;
		}

		Path& line_to( glm::vec2 const& p ) {
			le_2d::le_2d_encoder_i.path_line_to( static_cast<le_2d_encoder_o*>( parent ), p );
			return *this;
		}

		Path& quad_to( glm::vec2 const& c1, glm::vec2 const& p ) {
			le_2d::le_2d_encoder_i.path_quad_to( static_cast<le_2d_encoder_o*>( parent ), c1, p );
			return *this;
		}

		Path& cubic_to( glm::vec2 const& c1, glm::vec2 const& c2, glm::vec2 const& p ) {
			le_2d::le_2d_encoder_i.path_cubic_to( static_cast<le_2d_encoder_o*>( parent ), c1, c2, p );
			return *this;
		}

		Path& circle( glm::vec2 const& centre, float radius, float tolerance = 0.1 ) {
			le_2d::le_2d_encoder_i.path_circle( static_cast<le_2d_encoder_o*>( parent ), centre, radius, tolerance );
			return *this;
		};

		Path& close() {
			le_2d::le_2d_encoder_i.path_close( static_cast<le_2d_encoder_o*>( parent ) );
			return *this;
		}

		Encoder2D& path_end( bool insert_path_marker = true ) {
			le_2d::le_2d_encoder_i.path_end( static_cast<le_2d_encoder_o*>( parent ), insert_path_marker );
			return parent;
		}

	  private:
		Path() = delete;

		~Path() {
			// todo - we need to implement closing the path
		}

	  public:
		friend class Encoder2D;
	};

  private:
	le_2d_encoder_o* self;
	Path             m_path{ *this };

  public:
	Encoder2D()
	    : self( le_2d::le_2d_encoder_i.create() )
	    , m_path( *this ) {
	}

	~Encoder2D() {
		le_2d::le_2d_encoder_i.destroy( self );
	}

	Encoder2D& colour( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
		le_2d::le_2d_encoder_i.encode_colour( self, Le2D::colour_to_abgr( r, g, b, a ) );
		return *this;
	};

	Encoder2D& colour_abgr( uint32_t colour ) {
		le_2d::le_2d_encoder_i.encode_colour( self, colour );
		return *this;
	};

	Encoder2D& transform( Transform2D const& t ) {
		le_2d::le_2d_encoder_i.encode_transform( self, &t );
		return *this;
	};

	Encoder2D& begin_clip( le_2d::BlendMode const& blend_mode, float alpha ) {
		le_2d::le_2d_encoder_i.encode_begin_clip( self, &blend_mode, alpha );
		return *this;
	};

	Encoder2D& end_clip() {
		le_2d::le_2d_encoder_i.encode_end_clip( self );
		return *this;
	};

	Encoder2D& reset() {
		le_2d::le_2d_encoder_i.reset( self );
		return *this;
	}

	Path& path_begin( le_2d::FillStyle const fill_style ) {
		le_2d::le_2d_encoder_i.encode_fill_style( self, fill_style );
		le_2d::le_2d_encoder_i.path_begin( self, true );
		return m_path;
	};

	Path& path_begin( le_2d::Stroke const& stroke_style = {} ) {
		le_2d::le_2d_encoder_i.encode_stroke_style( self, &stroke_style );
		le_2d::le_2d_encoder_i.path_begin( self, false );
		return m_path;
	};

	operator le_2d_encoder_o*() {
		return self;
	}

	explicit operator le_2d_encoder_o const*() const {
		return self;
	}
};

#endif // __cplusplus

#endif
