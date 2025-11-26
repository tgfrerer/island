#include "le_image_fx.h"
#include "le_core.h"

#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unordered_map>

#include "private/le_image_fx/inl/blit_frag.inl"
#include "private/le_image_fx/inl/fullscreen_vert.inl"
#include "private/le_image_fx/inl/blur_frag.inl"

static le_shader_module_handle shader_box_h;
static le_shader_module_handle shader_box_v;

// ----------------------------------------------------------------------
// Decompression functions - these are used to retrieve shader code from inl strings
static unsigned int stb_decompress( unsigned char* output, const unsigned char* i, unsigned int /*length*/ );
static unsigned int stb_decompress_length( const unsigned char* input );
// note decode85 is taken from ImGui
static unsigned int decode_85_byte( char c ) {
	return c >= '\\' ? c - 36 : c - 35;
}
static void decode_85( const unsigned char* src, unsigned char* dst ) {
	while ( *src ) {
		unsigned int tmp =
		    decode_85_byte( src[ 0 ] ) +
		    85 * ( decode_85_byte( src[ 1 ] ) +
		           85 * ( decode_85_byte( src[ 2 ] ) +
		                  85 * ( decode_85_byte( src[ 3 ] ) +
		                         85 * decode_85_byte( src[ 4 ] ) ) ) );
		dst[ 0 ] = ( ( tmp >> 0 ) & 0xFF );
		dst[ 1 ] = ( ( tmp >> 8 ) & 0xFF );
		dst[ 2 ] = ( ( tmp >> 16 ) & 0xFF );
		dst[ 3 ] = ( ( tmp >> 24 ) & 0xFF ); // We can't assume little-endianness.
		src += 5;
		dst += 4;
	}
}

static std::vector<uint32_t> decode_and_decompress_spv_str( char const* compressed_shader_code ) {
	std::vector<uint32_t> buf_spv_code;
	int                   compressed_size = ( ( ( int )strlen( compressed_shader_code ) + 4 ) / 5 ) * 4;
	auto                  decoded_data    = ( uint8_t* )malloc( compressed_size );
	decode_85( ( unsigned char const* )compressed_shader_code, decoded_data );

	const unsigned int buf_spv_num_bytes = stb_decompress_length( ( const unsigned char* )decoded_data );
	buf_spv_code.resize( buf_spv_num_bytes / 4 );
	stb_decompress( ( uint8_t* )buf_spv_code.data(), ( const unsigned char* )decoded_data, ( unsigned int )compressed_size );

	free( decoded_data );
	return buf_spv_code;
};

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_vert( le_pipeline_manager_o* pm ) {
	static le_shader_module_handle s = nullptr;

	if ( s ) {
		return s;
	}

	auto spv = decode_and_decompress_spv_str( fullscreen_vert_compressed_data_base85 );

	s = LeShaderModuleBuilder( pm )
	        //.setSourceFilePath( "./local_resources/shaders/fullscreen.vert" )
	        .setSpirvCode( spv.data(), spv.size() )
	        .setShaderStage( le::ShaderStage::eVertex )
	        .build();

	return s;
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_frag_blit( le_pipeline_manager_o* pm ) {
	static le_shader_module_handle s = nullptr;

	if ( s ) {
		return s;
	}

	auto spv = decode_and_decompress_spv_str( blit_frag_compressed_data_base85 );

	s = LeShaderModuleBuilder( pm )
	        //.setSourceFilePath( "./local_resources/shaders/blit.frag" )
	        .setSpirvCode( spv.data(), spv.size() )
	        .setShaderStage( le::ShaderStage::eFragment )
	        .build();

	return s;
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_frag_blur_h( le_pipeline_manager_o* pm ) {
	static le_shader_module_handle s = nullptr;

	if ( s ) {
		return s;
	}

	auto spv = decode_and_decompress_spv_str( blur_frag_compressed_data_base85 );

	s = LeShaderModuleBuilder( pm )
	        // .setSourceFilePath( "./local_resources/shaders/blur.frag" )
	        .setSpirvCode( spv.data(), spv.size() )
	        .setShaderStage( le::ShaderStage::eFragment )
	        .setSpecializationConstant( 0, 1.f )
	        .build();

	return s;
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_frag_blur_v( le_pipeline_manager_o* pm ) {
	static le_shader_module_handle s = nullptr;

	if ( s ) {
		return s;
	}

	auto spv = decode_and_decompress_spv_str( blur_frag_compressed_data_base85 );

	s = LeShaderModuleBuilder( pm )
	        // .setSourceFilePath( "./local_resources/shaders/blur.frag" )
	        .setSpirvCode( spv.data(), spv.size() )
	        .setShaderStage( le::ShaderStage::eFragment )
	        .setSpecializationConstant( 1, 1.f )
	        .build();

	return s;
}

// ----------------------------------------------------------------------
// BLUR
// ----------------------------------------------------------------------
struct le_image_fx_blur_o {
	// members
	le_pipeline_manager_o*   pipeline_manager = nullptr; // non-owning
	le_image_resource_handle image_b          = nullptr; // non-owning
	le_texture_handle      tex_blur_source;            // non-owning
};

static le_image_fx_blur_o* le_fx_blur_create( le_renderer_o* renderer ) {
	auto self = new le_image_fx_blur_o{};

	self->pipeline_manager = le_renderer_api_i->le_renderer_i.get_pipeline_manager( renderer );
	self->image_b          = le_renderer_api_i->le_renderer_i.create_img_resource_handle( renderer, nullptr, 0, 0 );
	self->tex_blur_source  = le_renderer_api_i->le_renderer_i.produce_texture_handle( renderer, "fx_blur_source" );

	return self;
}

// ----------------------------------------------------------------------

static void le_fx_blur_destroy( le_image_fx_blur_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_fx_blur_apply( le_image_fx_blur_o* self, le_rendergraph_o* rg, le_image_resource_handle image_a, le_resource_info_t* img_info ) {

	static auto pipelineBlurH =
	    LeGraphicsPipelineBuilder( self->pipeline_manager )
	        .withAttachmentBlendState()
	        .setColorBlendOp( le::BlendOp::eAdd )
	        .setSrcColorBlendFactor( le::BlendFactor::eOne )
	        .setDstColorBlendFactor( le::BlendFactor::eZero )
	        .setAlphaBlendOp( le::BlendOp::eAdd )
	        .setSrcAlphaBlendFactor( le::BlendFactor::eOne )
	        .setDstAlphaBlendFactor( le::BlendFactor::eZero )
	        .end()
	        .addShaderStage( get_shader_vert( self->pipeline_manager ) )
	        .addShaderStage( get_shader_frag_blur_h( self->pipeline_manager ) )

	        .build();

	static auto pipelineBlurV =
	    LeGraphicsPipelineBuilder( self->pipeline_manager )
	        .withAttachmentBlendState()
	        .setColorBlendOp( le::BlendOp::eAdd )
	        .setSrcColorBlendFactor( le::BlendFactor::eOne )
	        .setDstColorBlendFactor( le::BlendFactor::eZero )
	        .setAlphaBlendOp( le::BlendOp::eAdd )
	        .setSrcAlphaBlendFactor( le::BlendFactor::eOne )
	        .setDstAlphaBlendFactor( le::BlendFactor::eZero )
	        .end()
	        .addShaderStage( get_shader_vert( self->pipeline_manager ) )
	        .addShaderStage( get_shader_frag_blur_v( self->pipeline_manager ) )
	        .build();

	auto blur_h =
	    le::RenderPass( "blur_h" )
	        .addColorAttachment(
	            self->image_b,
	            le::ImageAttachmentInfoBuilder()
	                .setLoadOp( le::AttachmentLoadOp::eDontCare )
	                .build() ) // color attachment
	        .sampleTexture(
	            self->tex_blur_source,
	            le::ImageSamplerInfoBuilder()
	                .withImageViewInfo()
	                .setImage( image_a )
	                .end()
	                .withSamplerInfo()
	                .setAddressModeU( le::SamplerAddressMode::eRepeat )
	                .setAddressModeV( le::SamplerAddressMode::eRepeat )
	                .end()
	                .build() )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	            auto                fx = static_cast<le_image_fx_blur_o*>( user_data );
	            le::GraphicsEncoder encoder{ encoder_ };
		        encoder
		            .bindGraphicsPipeline( pipelineBlurH )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), fx->tex_blur_source )
		            .draw( 4 );
            } );

	auto blur_v =
	    le::RenderPass( "blur_v" )
	        .addColorAttachment(
	            image_a,
	            le::ImageAttachmentInfoBuilder()
	                .setLoadOp( le::AttachmentLoadOp::eDontCare )
	                .build() ) // color attachment
	        .sampleTexture(
	            self->tex_blur_source,
	            le::ImageSamplerInfoBuilder()
	                .withImageViewInfo()
	                .setImage( self->image_b )
	                .end()
	                .withSamplerInfo()
	                .setAddressModeU( le::SamplerAddressMode::eRepeat )
	                .setAddressModeV( le::SamplerAddressMode::eRepeat )
	                .end()
	                .build() )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	            auto                fx = static_cast<le_image_fx_blur_o*>( user_data );
	            le::GraphicsEncoder encoder{ encoder_ };
		        encoder

		            .bindGraphicsPipeline( pipelineBlurV )

		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), fx->tex_blur_source )
		            .draw( 4 );
            } );

	auto rendergraph = le::RenderGraph( rg );
	rendergraph
	    .addRenderPass( blur_h ) //
	    .addRenderPass( blur_v ) //
	    .declareResource( self->image_b, *img_info );
};

// ----------------------------------------------------------------------
// BLIT
// ----------------------------------------------------------------------

struct le_image_fx_blit_o {
	// members
	le_renderer_o* const             renderer;
	le_pipeline_manager_o*           pipeline_manager = nullptr; // non-owning
	le_texture_handle                tex_blit_source;            // owning
	le_gpso_handle                   pipeline_handle;            //
	le_image_fx_api::BlitBlendPreset blend_preset;
};

static le_image_fx_blit_o* le_fx_blit_create( le_renderer_o* renderer, le_image_fx_api::BlitBlendPreset blend_preset ) {
	auto self = new le_image_fx_blit_o{ renderer };

	self->tex_blit_source  = le_renderer_api_i->le_renderer_i.produce_texture_handle( renderer, "image_fx_blit_src" );
	self->pipeline_manager = le_renderer_api_i->le_renderer_i.get_pipeline_manager( renderer );
	self->blend_preset     = blend_preset;

	le::AttachmentBlendPreset selected_preset{};

	switch ( blend_preset ) {
	case le_image_fx_api::BLIT_BLEND_COPY:
		selected_preset = le::AttachmentBlendPreset::eCopy;
		break;
	case le_image_fx_api::BLIT_BLEND_ALPHA_PREMUL:
		selected_preset = le::AttachmentBlendPreset::ePremultipliedAlpha;
		break;
	case le_image_fx_api::BLIT_BLEND_ADD:
		selected_preset = le::AttachmentBlendPreset::eAdd;
		break;
	case le_image_fx_api::BLIT_BLEND_MULTIPLY:
		selected_preset = le::AttachmentBlendPreset::eMultiply;
		break;
	default:
		assert( false ); // unreachable
	}

	self->pipeline_handle =
	    LeGraphicsPipelineBuilder( self->pipeline_manager )
	        .addShaderStage( get_shader_vert( self->pipeline_manager ) )
	        .addShaderStage( get_shader_frag_blit( self->pipeline_manager ) )
	        .withAttachmentBlendState()
	        .usePreset( selected_preset )
	        .end()
	        .build();

	return self;
}

// ----------------------------------------------------------------------

static void le_fx_blit_destroy( le_image_fx_blit_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------
static void le_fx_blit_apply( le_image_fx_blit_o* self, le_rendergraph_o* rg, le_image_resource_handle_t* image_src, le_image_resource_handle_t* image_dst ) {

	auto blit_pass =
	    le::RenderPass( "blit" )
	        .addColorAttachment(
	            image_dst,
	            le::ImageAttachmentInfoBuilder()
	                .setLoadOp( self->blend_preset == le_image_fx::BlitBlendPreset::BLIT_BLEND_COPY ? le::AttachmentLoadOp::eDontCare : le::AttachmentLoadOp::eLoad )
	                .build() ) // color attachment
	        .sampleTexture(
	            self->tex_blit_source,
	            le::ImageSamplerInfoBuilder()
	                .withImageViewInfo()
	                .setImage( image_src )
	                .end()
	                .withSamplerInfo()
	                .setAddressModeU( le::SamplerAddressMode::eRepeat )
	                .setAddressModeV( le::SamplerAddressMode::eRepeat )
	                .end()
	                .build() )
	        .setExecuteCallback( self, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	            auto                fx = static_cast<le_image_fx_blit_o*>( user_data );
	            le::GraphicsEncoder encoder{ encoder_ };

		        // we want to make sure that the current attachment blend preset exists - if id does not exist in our cache, then
		        // we must create a new one and add it to the cache.

		        encoder
		            .bindGraphicsPipeline( fx->pipeline_handle )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), fx->tex_blit_source )
		            .draw( 4 );
	        } );

	auto rendergraph = le::RenderGraph( rg );
	rendergraph
	    .addRenderPass( blit_pass ) //
	    ;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_image_fx, api ) {
	auto& le_fx_i = static_cast<le_image_fx_api*>( api )->le_image_fx_i;

	le_fx_i.create_blur  = le_fx_blur_create;
	le_fx_i.destroy_blur = le_fx_blur_destroy;
	le_fx_i.blur_apply   = le_fx_blur_apply;

	le_fx_i.create_blit  = le_fx_blit_create;
	le_fx_i.destroy_blit = le_fx_blit_destroy;
	le_fx_i.blit_apply   = le_fx_blit_apply;
}

//-----------------------------------------------------------------------------
// [SECTION] Decompression code
//-----------------------------------------------------------------------------
// Compressed with stb_compress() then converted to a C array and encoded as base85.
// Use the program in misc/fonts/binary_to_compressed_c.cpp to create the array from a TTF file.
// The purpose of encoding as base85 instead of "0x00,0x01,..." style is only save on _source code_ size.
// Decompression from stb.h (public domain) by Sean Barrett https://github.com/nothings/stb/blob/master/stb.h
//-----------------------------------------------------------------------------

static unsigned int stb_decompress_length( const unsigned char* input ) {
	return ( input[ 8 ] << 24 ) + ( input[ 9 ] << 16 ) + ( input[ 10 ] << 8 ) + input[ 11 ];
}

static unsigned char *      stb__barrier_out_e, *stb__barrier_out_b;
static const unsigned char* stb__barrier_in_b;
static unsigned char*       stb__dout;
static void                 stb__match( const unsigned char* data, unsigned int length ) {
    // INVERSE of memmove... write each byte before copying the next...
    assert( stb__dout + length <= stb__barrier_out_e );
    if ( stb__dout + length > stb__barrier_out_e ) {
        stb__dout += length;
        return;
    }
    if ( data < stb__barrier_out_b ) {
        stb__dout = stb__barrier_out_e + 1;
        return;
    }
    while ( length-- )
        *stb__dout++ = *data++;
}

static void stb__lit( const unsigned char* data, unsigned int length ) {
	assert( stb__dout + length <= stb__barrier_out_e );
	if ( stb__dout + length > stb__barrier_out_e ) {
		stb__dout += length;
		return;
	}
	if ( data < stb__barrier_in_b ) {
		stb__dout = stb__barrier_out_e + 1;
		return;
	}
	memcpy( stb__dout, data, length );
	stb__dout += length;
}

#define stb__in2( x ) ( ( i[ x ] << 8 ) + i[ ( x ) + 1 ] )
#define stb__in3( x ) ( ( i[ x ] << 16 ) + stb__in2( ( x ) + 1 ) )
#define stb__in4( x ) ( ( i[ x ] << 24 ) + stb__in3( ( x ) + 1 ) )

static const unsigned char* stb_decompress_token( const unsigned char* i ) {
	if ( *i >= 0x20 ) { // use fewer if's for cases that expand small
		if ( *i >= 0x80 )
			stb__match( stb__dout - i[ 1 ] - 1, i[ 0 ] - 0x80 + 1 ), i += 2;
		else if ( *i >= 0x40 )
			stb__match( stb__dout - ( stb__in2( 0 ) - 0x4000 + 1 ), i[ 2 ] + 1 ), i += 3;
		else /* *i >= 0x20 */
			stb__lit( i + 1, i[ 0 ] - 0x20 + 1 ), i += 1 + ( i[ 0 ] - 0x20 + 1 );
	} else { // more ifs for cases that expand large, since overhead is amortized
		if ( *i >= 0x18 )
			stb__match( stb__dout - ( stb__in3( 0 ) - 0x180000 + 1 ), i[ 3 ] + 1 ), i += 4;
		else if ( *i >= 0x10 )
			stb__match( stb__dout - ( stb__in3( 0 ) - 0x100000 + 1 ), stb__in2( 3 ) + 1 ), i += 5;
		else if ( *i >= 0x08 )
			stb__lit( i + 2, stb__in2( 0 ) - 0x0800 + 1 ), i += 2 + ( stb__in2( 0 ) - 0x0800 + 1 );
		else if ( *i == 0x07 )
			stb__lit( i + 3, stb__in2( 1 ) + 1 ), i += 3 + ( stb__in2( 1 ) + 1 );
		else if ( *i == 0x06 )
			stb__match( stb__dout - ( stb__in3( 1 ) + 1 ), i[ 4 ] + 1 ), i += 5;
		else if ( *i == 0x04 )
			stb__match( stb__dout - ( stb__in3( 1 ) + 1 ), stb__in2( 4 ) + 1 ), i += 6;
	}
	return i;
}

static unsigned int stb_adler32( unsigned int adler32, unsigned char* buffer, unsigned int buflen ) {
	const unsigned long ADLER_MOD = 65521;
	unsigned long       s1 = adler32 & 0xffff, s2 = adler32 >> 16;
	unsigned long       blocklen = buflen % 5552;

	unsigned long i;
	while ( buflen ) {
		for ( i = 0; i + 7 < blocklen; i += 8 ) {
			s1 += buffer[ 0 ], s2 += s1;
			s1 += buffer[ 1 ], s2 += s1;
			s1 += buffer[ 2 ], s2 += s1;
			s1 += buffer[ 3 ], s2 += s1;
			s1 += buffer[ 4 ], s2 += s1;
			s1 += buffer[ 5 ], s2 += s1;
			s1 += buffer[ 6 ], s2 += s1;
			s1 += buffer[ 7 ], s2 += s1;

			buffer += 8;
		}

		for ( ; i < blocklen; ++i )
			s1 += *buffer++, s2 += s1;

		s1 %= ADLER_MOD, s2 %= ADLER_MOD;
		buflen -= blocklen;
		blocklen = 5552;
	}
	return ( unsigned int )( s2 << 16 ) + ( unsigned int )s1;
}

static unsigned int stb_decompress( unsigned char* output, const unsigned char* i, unsigned int /*length*/ ) {
	if ( stb__in4( 0 ) != 0x57bC0000 )
		return 0;
	if ( stb__in4( 4 ) != 0 )
		return 0; // error! stream is > 4GB
	const unsigned int olen = stb_decompress_length( i );
	stb__barrier_in_b       = i;
	stb__barrier_out_e      = output + olen;
	stb__barrier_out_b      = output;
	i += 16;

	stb__dout = output;
	for ( ;; ) {
		const unsigned char* old_i = i;
		i                          = stb_decompress_token( i );
		if ( i == old_i ) {
			if ( *i == 0x05 && i[ 1 ] == 0xfa ) {
				assert( stb__dout == output + olen );
				if ( stb__dout != output + olen )
					return 0;
				if ( stb_adler32( 1, output, olen ) != ( unsigned int )stb__in4( 2 ) )
					return 0;
				return olen;
			} else {
				assert( 0 ); /* NOTREACHED */
				return 0;
			}
		}
		assert( stb__dout <= output + olen );
		if ( stb__dout > output + olen )
			return 0;
	}
}
