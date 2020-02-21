#include "le_font_renderer.h"
#include "le_core/le_core.h"

#include "modules/le_renderer/le_renderer.h"
#include "modules/le_font/le_font.h"
#include "modules/le_pipeline_builder/le_pipeline_builder.h"

#include <forward_list>
#include <cstdio>
#include <atomic>
#include <algorithm>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/vec4.hpp"

struct font_info_t {
	le_font_o *          font; // non-owning
	le_resource_handle_t font_image;
	le_resource_info_t   font_atlas_info;
	le_resource_handle_t font_image_sampler;
	bool                 atlas_uploaded;
	bool                 sampler_created;
};

struct le_font_renderer_o {
	std::forward_list<font_info_t> fonts_info;
	std::atomic<size_t>            counter          = {};
	le_shader_module_o *           shader_font_vert = nullptr;
	le_shader_module_o *           shader_font_frag = nullptr;
};

using draw_string_info_t = le_font_renderer_api::draw_string_info_t;

// ----------------------------------------------------------------------

le_font_renderer_o *le_font_renderer_create( le_renderer_o *renderer ) {
	auto self = new le_font_renderer_o();

	using namespace le_renderer;

	self->shader_font_vert = renderer_i.create_shader_module( renderer, "./resources/shaders/le_font.vert", {le::ShaderStage::eVertex}, "NO_MVP" );
	self->shader_font_frag = renderer_i.create_shader_module( renderer, "./resources/shaders/le_font.frag", {le::ShaderStage::eFragment}, "" );

	return self;
}

// ----------------------------------------------------------------------

void le_font_renderer_destroy( le_font_renderer_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------
void le_font_renderer_add_font( le_font_renderer_o *self, le_font_o *font ) {

	char img_sampler_name[ 32 ] = "";
	char img_atlas_name[ 32 ]   = "";

	size_t number = self->counter++;

	snprintf( img_atlas_name, sizeof( img_atlas_name ), "fr_a_%08zu", number );
	snprintf( img_sampler_name, sizeof( img_sampler_name ), "fr_s_%08zu", number );

	using namespace le_font;
	uint8_t const *pixels_data;
	uint32_t       atlas_width, atlas_height, atlas_stride;

	le_font_i.create_atlas( font );

	le_font_i.get_atlas( font, &pixels_data, &atlas_width, &atlas_height, &atlas_stride );

	le_resource_info_t font_atlas_info =
	    le::ImageInfoBuilder()
	        .setExtent( atlas_width, atlas_height )
	        .setFormat( le::Format::eR8Unorm )
	        .build();

	auto info = font_info_t( {font,
	                          LE_IMG_RESOURCE( img_atlas_name ),
	                          font_atlas_info,
	                          LE_IMAGE_SAMPLER_RESOURCE( img_sampler_name ),
	                          false,
	                          false} );

	self->fonts_info.push_front( info );
}

// ----------------------------------------------------------------------
le_resource_handle_t *le_font_renderer_get_font_image_sampler( le_font_renderer_o *self, le_font_o *font ) {

	for ( auto &f : self->fonts_info ) {
		if ( f.font == font ) {
			return &f.font_image_sampler;
		}
	}

	// ----------| invariant: Image sampler has not been found, otherwise we'd have returned early.

	return nullptr;
}

// ----------------------------------------------------------------------
le_resource_handle_t *le_font_renderer_get_font_image( le_font_renderer_o *self, le_font_o *font ) {

	for ( auto &f : self->fonts_info ) {
		if ( f.font == font ) {
			return &f.font_image;
		}
	}

	// ----------| invariant: Image sampler has not been found, otherwise we'd have returned early.

	return nullptr;
}

// ----------------------------------------------------------------------

bool le_font_renderer_setup_resources( le_font_renderer_o *self, le_render_module_o *module ) {

	auto resource_upload_pass =
	    le::RenderPass( "uploadImage", LE_RENDER_PASS_TYPE_TRANSFER )
	        .setSetupCallback( self, []( le_renderpass_o *rp_, void *user_data ) -> bool {
		        le::RenderPass rp{rp_};

		        auto self         = static_cast<le_font_renderer_o *>( user_data );
		        bool needs_upload = false; // If any atlasses need upload this must flip to true.

		        for ( auto &fnt : self->fonts_info ) {
			        rp.useImageResource( fnt.font_image, {LE_IMAGE_USAGE_TRANSFER_DST_BIT} );
			        needs_upload |= !fnt.atlas_uploaded;
		        }

		        return needs_upload;
	        } )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
		        auto self = static_cast<le_font_renderer_o *>( user_data );

		        le::Encoder encoder{encoder_};

		        for ( auto &fnt : self->fonts_info ) {

			        if ( fnt.atlas_uploaded ) {
				        continue;
			        }

			        auto write_settings = le::WriteToImageSettingsBuilder().build();

			        using namespace le_font;

			        uint8_t const *pixels_data;
			        uint32_t       pix_stride;
			        le_font_i.get_atlas( fnt.font, &pixels_data, &write_settings.image_w, &write_settings.image_h, &pix_stride );

			        encoder.writeToImage( fnt.font_image, write_settings, pixels_data, pix_stride * write_settings.image_w * write_settings.image_h );

			        fnt.atlas_uploaded = true;
		        }
	        } );

	using namespace le_renderer;

	// -- upload resources if needed
	render_module_i.add_renderpass( module, resource_upload_pass );

	// -- make resource names visible to rendergraph
	for ( auto &fnt : self->fonts_info ) {
		render_module_i.declare_resource( module, fnt.font_image, fnt.font_atlas_info );
	}

	return true;
};

// ----------------------------------------------------------------------

bool le_font_renderer_use_fonts( le_font_renderer_o *self, le_font_o **fonts, size_t num_fonts, le_renderpass_o *pass ) {

	for ( size_t i = 0; i != num_fonts; ++i ) {

		le_font_o const *f          = fonts[ i ];
		font_info_t *    found_info = nullptr;

		// -- Find info entry for this font

		for ( auto &info : self->fonts_info ) {
			if ( info.font == f ) {
				found_info = &info;
				break;
			}
		}

		if ( nullptr == found_info ) {
			// we should print out a warning that the font in question hasn't been found.
			assert( false && "font was not found in font_renderer" );
			continue;
		}

		LeImageSamplerInfo font_sampler_info =
		    le::ImageSamplerInfoBuilder()
		        .withImageViewInfo()
		        .setImage( found_info->font_image )
		        .end()
		        .build();

		using namespace le_renderer;

		renderpass_i.sample_texture( pass, found_info->font_image_sampler, &font_sampler_info );
	}

	return true;
}

// ----------------------------------------------------------------------

bool le_font_renderer_draw_string( le_font_renderer_o *self, le_font_o *font, le_command_buffer_encoder_o *encoder_, draw_string_info_t &info ) {

	le::Encoder encoder{encoder_};

	auto extents = encoder.getRenderpassExtent();

	static auto pipeline =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( self->shader_font_vert )
	        .addShaderStage( self->shader_font_frag )
	        .build();

	using namespace le_font;

	size_t                 num_vertices = le_font_i.draw_utf8_string( font, info.str, nullptr, nullptr, nullptr, 0, 0 );
	std::vector<glm::vec4> vertices;

	vertices.resize( num_vertices );
	le_font_i.draw_utf8_string( font, info.str, &info.x, &info.y, vertices.data(), num_vertices, 0 );

	struct NoMvpUbo {
		glm::vec4 screen_extents;
	} no_mvp_ubo;

	no_mvp_ubo.screen_extents = {0, 0, float( extents.width ), float( extents.height )};

	encoder
	    .bindGraphicsPipeline( pipeline )
	    .setArgumentData( LE_ARGUMENT_NAME( "Extents" ), &no_mvp_ubo, sizeof( NoMvpUbo ) ) //
	    .setVertexData( vertices.data(), sizeof( glm::vec4 ) * vertices.size(), 0 )
	    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), *le_font_renderer_get_font_image_sampler( self, font ) )
	    .setArgumentData( LE_ARGUMENT_NAME( "VertexColor" ), &info.color, sizeof( info.color ) )
	    .draw( uint32_t( vertices.size() ) ) //
	    ;

	return true;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_font_renderer_api( void *api ) {
	auto &i = static_cast<le_font_renderer_api *>( api )->le_font_renderer_i;

	i.create  = le_font_renderer_create;
	i.destroy = le_font_renderer_destroy;

	i.add_font               = le_font_renderer_add_font;
	i.setup_resources        = le_font_renderer_setup_resources;
	i.use_fonts              = le_font_renderer_use_fonts;
	i.get_font_image         = le_font_renderer_get_font_image;
	i.get_font_image_sampler = le_font_renderer_get_font_image_sampler;
	i.draw_string            = le_font_renderer_draw_string;
}
