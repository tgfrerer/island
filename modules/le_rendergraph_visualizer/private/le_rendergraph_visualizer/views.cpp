#include "views.h"
#include <unordered_map>
#include "le_font.h"
#include "le_path.h"
#include "glm/glm.hpp"
#include <string>
#include <vector>
#include "private/le_renderer/le_rendergraph.h"
#include "private/le_renderer/le_resource_handle_t.inl"

#include "shared_constants.inl"

// ----------------------------------------------------------------------

static void path_move_to( void* user_data, glm::vec2 const* p ) {
	auto self = ( RenderPassView* )user_data;
	le_2d::le_2d_encoder_i.path_move_to( self->get_encoder(), *p );
};

static void path_line_to( void* user_data, glm::vec2 const* p ) {
	auto self = ( RenderPassView* )user_data;
	le_2d::le_2d_encoder_i.path_line_to( self->get_encoder(), *p );
};

static void path_quad_bezier_to( void* user_data, glm::vec2 const* c1, glm::vec2 const* p ) {
	auto self = ( RenderPassView* )user_data;
	le_2d::le_2d_encoder_i.path_quad_to( self->get_encoder(), *c1, *p );
};

static void path_cubic_bezier_to( void* user_data, glm::vec2 const* c1, glm::vec2 const* c2, glm::vec2 const* p ) {
	auto self = ( RenderPassView* )user_data;
	le_2d::le_2d_encoder_i.path_cubic_to( self->get_encoder(), *c1, *c2, *p );
};

static void path_close( void* user_data ) {
	auto self = ( RenderPassView* )user_data;
	le_2d::le_2d_encoder_i.path_close( self->get_encoder() );
};

static void path_arc_to( void* user_data, glm::vec2 const* p, glm::vec2 const* radii, float phi, bool large_arc, bool sweep ) {
	// noop
};


// ----------------------------------------------------------------------

RenderPassView::RenderPassView( le::Font* const font, le_renderpass_o const* rp, uint32_t epoch_ )
    : pFont( font )
    , epoch( epoch_ )
    , name( rp->debug_name )
    , is_contributing( rp->is_contributing )
    , is_root( rp->is_root ) {

	std::vector<uint32_t> codepoints_rp_name;
	font_cache_encoder.reset();


	auto cp_callback = []( uint32_t cp, void* user_data ) {
		auto& cps = *static_cast<std::vector<uint32_t>*>( user_data );
		cps.push_back( uint32_t( cp ) );
	};

	le_font::le_utf8_iterator( this->name.c_str(), &codepoints_rp_name, cp_callback );

	float left_indent = 20.f;
	float top_offset  = c_line_height - 4.f;

	glm::vec2 offset_initial = { left_indent, top_offset };
	glm::vec2 offset         = offset_initial;

	le_path_operations_interface_t path_ops{
	    .move_to         = path_move_to,
	    .line_to         = path_line_to,
	    .quad_bezier_to  = path_quad_bezier_to,
	    .cubic_bezier_to = path_cubic_bezier_to,
	    .arc_to          = path_arc_to,
	    .close           = path_close,
	};

	font_cache_encoder.transform( {} );

	uint8_t alpha_value = this->is_contributing ? 255 : 63;
	alpha_value         = 255;

	if ( this->is_contributing ) {
		if ( this->is_root ) {
			font_cache_encoder.colour( 255, 255, 255, alpha_value );
		} else {
			font_cache_encoder.colour( 0, 0, 0, alpha_value );
		}
	} else {
		font_cache_encoder.colour( 128, 128, 128, alpha_value );
	}

	font_cache_encoder.path_begin( le_2d::FillStyle::EvenOdd );

	uint32_t prev_cp = 0;
	for ( auto& c : codepoints_rp_name ) {
		le_font::le_font_i.add_paths_for_glyph( *pFont, this, c, 0.125 * 0.25 * .6, &offset, prev_cp, &path_ops );
		prev_cp = c;
	}
	font_cache_encoder.path_end();

	this->right_most_x = offset.x;

	offset.y += 10;

	glm::vec2 pos = glm::vec2{ 0.f, c_line_height + 10.f };

	for ( int i = 0; i != rp->resources.size(); i++ ) {

		// Draw resource names and calculate port positions
		// based on drawn text size.

		auto const& r     = rp->resources[ i ];
		auto const& r_acc = rp->resources_access_flags[ i ];

		bool is_read           = ( LE_ALL_READ_ACCESS_FLAGS & r_acc );
		bool is_implicit_write = ( r_acc & LE_ALL_IMAGE_IMPLIED_WRITE_ACCESS_FLAGS );
		bool is_explicit_write = ( r_acc & LE_ALL_WRITE_ACCESS_FLAGS );
		bool is_write          = is_implicit_write | is_explicit_write;
		bool is_root_resource  = r->data->type == LeResourceType::eImage && ( r->data->flags & le_img_resource_usage_flags_t::eIsRoot );

		std::string resource_name = std::string( r->data->debug_name );

		std::vector<uint32_t> resource_name_cp;
		le_font::le_utf8_iterator( resource_name.c_str(), &resource_name_cp, cp_callback );

		offset.x = offset_initial.x;
		offset.y += c_line_height;
		uint32_t prev_cp = 0;

		if ( is_root_resource ) {
			// this resource is a root resource (a swapchain resource probably)
			font_cache_encoder.colour( le_2d::Colour( 0x7101f1f0 ) );
		} else {
			font_cache_encoder.colour( 0, 0, 0, alpha_value );
		}

		font_cache_encoder.path_begin( le_2d::FillStyle::EvenOdd );
		for ( auto& c : resource_name_cp ) {
			le_font::le_font_i.add_paths_for_glyph( *pFont, this, c, 0.125 * 0.25 * .6, &offset, prev_cp, &path_ops );
			prev_cp = c;
		}
		font_cache_encoder.path_end();

		if ( offset.x > this->right_most_x ) {
			this->right_most_x = offset.x;
		}

		this->ports[ r ] = offset.y - c_line_height * .25;
	}

	for ( int i = 0; i != rp->resources.size(); i++ ) {

		// NOTE: we must do this in a separate pass from the first run-though all resources
		// because we only know the max width once we have drawn all resource names.
		//
		// We cache the resource port locations here instead of drawing them directly because
		// drawing here would mean that the ports will get clipped by the card shape. We want
		// ports to go slightly over the card boundary.

		auto const& r     = rp->resources[ i ];
		auto const& r_acc = rp->resources_access_flags[ i ];

		bool is_read           = ( LE_ALL_READ_ACCESS_FLAGS & r_acc );
		bool is_implicit_write = ( r_acc & LE_ALL_IMAGE_IMPLIED_WRITE_ACCESS_FLAGS );
		bool is_explicit_write = ( r_acc & LE_ALL_WRITE_ACCESS_FLAGS );
		bool is_write          = is_implicit_write | is_explicit_write;
		bool is_root_resource  = r->data->type == LeResourceType::eImage && ( r->data->flags & le_img_resource_usage_flags_t::eIsRoot );

		if ( is_read ) {
			in_ports.push_back( getPortForResource( r, true ) );
		}
		if ( is_implicit_write ) {
			implicit_out_ports.push_back( getPortForResource( r, false ) );
		}
		if ( is_explicit_write ) {
			explicit_out_ports.push_back( getPortForResource( r, false ) );
		}
	}

	this->required_height = offset.y;
}

// ----------------------------------------------------------------------

glm::vec2 RenderPassView::getPortForResource( const le_resource_handle& resource, bool read_or_write ) {
	// todo: fill in --
	auto it = this->ports.find( resource );

	if ( it == this->ports.end() ) {
		return {};
	}

	// ----------| invariant:  port was found

	glm::vec2 ret;

	read_or_write ? ret.x = c_padding_left_right* 0.25 : ret.x = this->right_most_x + c_padding_left_right * 0.5;
	ret.y = it->second;

	return ret;
}

// ----------------------------------------------------------------------

void RenderPassView::draw( le::Encoder2D& encoder, const LeTransform2D& transform ) {

	float card_height = c_line_height + this->required_height;
	float radius      = 10;

	// define a clip shape -- the rounded rect for the card
	encoder
	    .blurred_rounded_rect( transform, le_2d_colour( 0.f, 0.f, 0.f, 0.5 * ( this->is_contributing ? 1.f : 0.2f ) ), this->right_most_x + c_padding_left_right, card_height, radius, 12 )
	    .transform( transform )
	    .begin_clip( le_2d::BlendMode{ .mix = le_2d::BlendMode::Mix::Normal }, this->is_contributing ? 1.0 : 0.2f )
	    // begin card clipping shape:
	    .path_begin( le_2d::FillStyle::NonZero )
	    .rounded_rect( { 0, 0 }, { this->right_most_x + c_padding_left_right, card_height }, radius ) // outside clip shape
	    .path_end();
	{
		// inside the clipping region

		// draw card title + colour background
		encoder
		    .colour_rgba( c_colour_draw )
		    .path_begin( le_2d::FillStyle::EvenOdd )
		    .rect( { 0, 0 }, { this->right_most_x + c_padding_left_right, card_height } ) // background title fill
		    .path_end();
		// draw card light background
		encoder
		    .colour( 200, 200, 200 )
		    .path_begin( le_2d::FillStyle::EvenOdd )
		    .rounded_rect( { 3, c_line_height * 1.2 + 3 - radius }, { this->right_most_x + c_padding_left_right - 3, card_height - 3 }, radius - 3 ) // background fill
		    .path_end();
		// draw title again, so that we don't see the rounded rect of the inner card at the top
		encoder
		    .colour_rgba( c_colour_draw )
		    .path_begin( le_2d::FillStyle::EvenOdd )
		    .rect( { 0, 0 }, { this->right_most_x + c_padding_left_right, c_line_height * 1.2 + radius * 0.25 } ) // background title fill
		    .path_end();

		// Now add contents inside clipping region:
		// That's all typography in one go.
		encoder
		    .append( this->font_cache_encoder, transform )
		    // and end the clip shape
		    .end_clip();
	}

	if ( this->is_contributing ) {

		// only draw ports if this path is contributing

		for ( int i = 0; i != 2; i++ ) {
			encoder
			    .transform( transform );

			if ( i == 0 ) {
				encoder
				    .colour_abgr( 0xff5f711e )
				    .path_begin( le_2d::FillStyle::NonZero );
			} else {
				encoder
				    .colour_abgr( 0xff2d5016 )
				    .path_begin( le_2d::Stroke{ .width = 1.f } );
			}

			// in ports
			for ( auto const& p : this->in_ports ) {
				encoder.get_path().circle( p + glm::vec2{ c_padding_left_right * 0.35f, 0.f }, 4 );
			}

			// implicit out ports
			for ( auto const& p : this->implicit_out_ports ) {
				encoder.get_path().circle( p + glm::vec2{ c_padding_left_right * 0.125f, 0.f }, 2 );
			}

			// explicit out ports
			for ( auto const& p : this->explicit_out_ports ) {
				encoder.get_path().circle( p + glm::vec2{ c_padding_left_right * 0.125f, 0.f }, 4 );
			}
			encoder.path_end();
		}
	}
}
