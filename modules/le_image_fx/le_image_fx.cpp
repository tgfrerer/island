#include "le_image_fx.h"
#include "le_core.h"

#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_log.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <atomic>
#include <unordered_map>

#include "private/le_image_fx/inl/blit_frag.inl"
#include "private/le_image_fx/inl/fullscreen_vert.inl"
#include "private/le_image_fx/inl/blur_frag.inl"
#include "private/le_image_fx/inl/decompression_helpers.inl"

static le_shader_module_handle shader_box_h;
static le_shader_module_handle shader_box_v;

static auto& get_logger() {
	static auto logger = le::Log( "le_image_fx" );
	return logger;
}

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
	        //.setSourceFilePath( "./local_resources/le_image_fx_dev/shaders/glsl/blit.frag" )
	        .setSpirvCode( spv.data(), spv.size() )
	        .setShaderStage( le::ShaderStage::eFragment )
	        .build();

	return s;
}

// ----------------------------------------------------------------------
// BLUR
// ----------------------------------------------------------------------
struct le_image_fx_blur_o {
	// members
	le_renderer_o*           renderer         = nullptr; // non-owning
	le_image_resource_handle image_b          = nullptr; // owning
	le_texture_handle        tex_blur_source;            // owning

	bool                  was_setup            = false;
	le_gpso_handle        pipeline_handle[ 2 ] = { nullptr, nullptr }; // blur_h, blur_v

	le_image_fx_api::blur_preset settings = le_image_fx_api::blur_preset();

	std::atomic<uint32_t> reference_count = 0;
};

// ----------------------------------------------------------------------

static le_image_fx_blur_o* le_fx_blur_create( le_renderer_o* renderer, le_image_fx_api::blur_preset preset ) {
	auto self = new le_image_fx_blur_o{
	    .renderer        = renderer,
	    .image_b         = le_renderer_api_i->le_renderer_i.create_img_resource_handle( renderer, nullptr, 0, 0 ),
	    .tex_blur_source = le_renderer_api_i->le_renderer_i.produce_texture_handle( renderer, "fx_blur_source" ),
	    .was_setup       = false,
	    .pipeline_handle = { nullptr, nullptr },
	    .settings        = preset,
	    .reference_count = 1,
	};
	return self;
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_frag_blur( le_image_fx_blur_o* self, le_pipeline_manager_o* pm, uint32_t direction ) {
	auto builder = LeShaderModuleBuilder( pm );

	builder
	    .setShaderStage( le::ShaderStage::eFragment )
	    .setSpecializationConstant( direction, 1.f )
	    .setSpecializationConstant( 2, self->settings.kernel_radius )
	    .setSpecializationConstant( 3, self->settings.sigma ) //
	    ;

#if LE_IMAGE_FX_SHADERS_FROM_SOURCE
	builder.setSourceFilePath( "./local_resources/le_image_fx/shaders/glsl/blur.frag" )
#else
	static auto spv = decode_and_decompress_spv_str( blur_frag_compressed_data_base85 );
	builder.setSpirvCode( spv.data(), spv.size() );
#endif
	    return builder.build();
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_frag_blur_h( le_image_fx_blur_o* self, le_pipeline_manager_o* pm ) {
	return get_shader_frag_blur( self, pm, 0 );
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_frag_blur_v( le_image_fx_blur_o* self, le_pipeline_manager_o* pm ) {
	return get_shader_frag_blur( self, pm, 1 );
}

// ----------------------------------------------------------------------

static void le_fx_blur_dec_ref_count( le_image_fx_blur_o* self ) {
	if ( --self->reference_count == 0 ) {
		delete self;
	}
}

// ----------------------------------------------------------------------

static void le_fx_blur_destroy( le_image_fx_blur_o* self ) {
	le_fx_blur_dec_ref_count( self );
}

// ----------------------------------------------------------------------

static void le_fx_blur_apply( le_image_fx_blur_o* self, le_rendergraph_o* rg, le_image_resource_handle image_a, le_resource_info_t const* img_info, le_image_fx_api::blur_preset const* preset ) {

	[[unlikely]] if ( false == self->was_setup ) {

		le_pipeline_manager_o* pipeline_manager = le_renderer_api_i->le_renderer_i.get_pipeline_manager( self->renderer );

		self->pipeline_handle[ 0 ] =
		    LeGraphicsPipelineBuilder( pipeline_manager )
		        .withAttachmentBlendState()
		        .setColorBlendOp( le::BlendOp::eAdd )
		        .setSrcColorBlendFactor( le::BlendFactor::eOne )
		        .setDstColorBlendFactor( le::BlendFactor::eZero )
		        .setAlphaBlendOp( le::BlendOp::eAdd )
		        .setSrcAlphaBlendFactor( le::BlendFactor::eOne )
		        .setDstAlphaBlendFactor( le::BlendFactor::eZero )
		        .end()
		        .addShaderStage( get_shader_vert( pipeline_manager ) )
		        .addShaderStage( get_shader_frag_blur_h( self, pipeline_manager ) )

		        .build();

		self->pipeline_handle[ 1 ] =
		    LeGraphicsPipelineBuilder( pipeline_manager )
		        .withAttachmentBlendState()
		        .setColorBlendOp( le::BlendOp::eAdd )
		        .setSrcColorBlendFactor( le::BlendFactor::eOne )
		        .setDstColorBlendFactor( le::BlendFactor::eZero )
		        .setAlphaBlendOp( le::BlendOp::eAdd )
		        .setSrcAlphaBlendFactor( le::BlendFactor::eOne )
		        .setDstAlphaBlendFactor( le::BlendFactor::eZero )
		        .end()
		        .addShaderStage( get_shader_vert( pipeline_manager ) )
		        .addShaderStage( get_shader_frag_blur_v( self, pipeline_manager ) )
		        .build();
		self->was_setup = true;
	}

	// We store user data with the callback, so that we can pass
	// all relevant state of the current object as lambda data to
	// the callback, and the callback does not have to recur to the
	// actual object anymore.

	struct user_data_t {
		le_image_fx_api::blur_preset preset;
		le_gpso_handle               pipeline;
		le_texture_handle            src_tex;
	};

	user_data_t user_data{
	    .preset   = preset ? *preset : self->settings,
	    .pipeline = self->pipeline_handle[ 0 ],
	    .src_tex  = self->tex_blur_source,
	};

	auto blur_exec_cb = []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
		auto data = static_cast<user_data_t*>( user_data );

		le::GraphicsEncoder encoder{ encoder_ };
		// note that this will upload settings at the time the blur pass executes,
		// and not at the time the command is recorded - you might want to
		encoder
		    .bindGraphicsPipeline( data->pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), data->src_tex )
		    .setPushConstantData( &data->preset, sizeof( self->settings ) )
		    .draw( 4 );
	};

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
	        .setExecuteCallbackWithLocalUserData( &user_data, sizeof( user_data_t ), blur_exec_cb );

	// update pipeline contained in user data for pass 2
	user_data.pipeline = self->pipeline_handle[ 1 ];

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
	        .setExecuteCallbackWithLocalUserData( &user_data, sizeof( user_data_t ), blur_exec_cb )
	        .setCleanupCallback( self, []( void* user_data ) {
		        auto fx = static_cast<le_image_fx_blur_o*>( user_data );
		        // Decrement the reference count to the object as this callback
		        // has finished and therefore releases its reference to the object.
		        // this gets executed regardless of whether `blur_v` is executed or not.
		        le_fx_blur_dec_ref_count( fx );
	        } );
	;

	// increase reference count for this object
	// as it is enqueued with the current frame
	self->reference_count++;

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
	le_renderer_o* const             renderer         = nullptr;
	le_pipeline_manager_o*           pipeline_manager = nullptr; // non-owning
	le_texture_handle                tex_blit_source  = nullptr; // owning
	le_gpso_handle                   pipeline_handle  = nullptr; //
	le_image_fx_api::BlitBlendPreset blend_preset     = {};
	bool                             was_setup        = false;
	std::atomic<uint32_t>            reference_count  = 0;
};

// ----------------------------------------------------------------------

static bool le_fx_setup_blit( le_image_fx_blit_o* self ) {

	if ( self->renderer == nullptr ) {
		get_logger().error( "Cannot initialize blit without valid renderer -- Was renderer set up?" );
		return false;
	}

	self->tex_blit_source  = le_renderer_api_i->le_renderer_i.produce_texture_handle( self->renderer, "image_fx_blit_src" );
	self->pipeline_manager = le_renderer_api_i->le_renderer_i.get_pipeline_manager( self->renderer );

	if ( self->pipeline_manager == nullptr ) {
		get_logger().error( "Cannot initialize blit without valid pipeline manager -- Was renderer set up?" );
		return false;
	}

	le::AttachmentBlendPreset selected_blend_preset{};

	switch ( self->blend_preset ) {
	case le_image_fx_api::BLIT_BLEND_COPY:
		selected_blend_preset = le::AttachmentBlendPreset::eCopy;
		break;
	case le_image_fx_api::BLIT_BLEND_ALPHA_PREMUL:
		selected_blend_preset = le::AttachmentBlendPreset::ePremultipliedAlpha;
		break;
	case le_image_fx_api::BLIT_BLEND_ADD:
		selected_blend_preset = le::AttachmentBlendPreset::eAdd;
		break;
	case le_image_fx_api::BLIT_BLEND_MULTIPLY:
		selected_blend_preset = le::AttachmentBlendPreset::eMultiply;
		break;
	default:
		assert( false ); // unreachable
	}

	self->pipeline_handle =
	    LeGraphicsPipelineBuilder( self->pipeline_manager )
	        .addShaderStage( get_shader_vert( self->pipeline_manager ) )
	        .addShaderStage( get_shader_frag_blit( self->pipeline_manager ) )
	        .withAttachmentBlendState()
	        .usePreset( selected_blend_preset )
	        .end()
	        .build();

	self->was_setup = true;

	return true;
}

// ----------------------------------------------------------------------

static le_image_fx_blit_o* le_fx_blit_create( le_renderer_o* renderer, le_image_fx_api::BlitBlendPreset blend_preset ) {
	auto self = new le_image_fx_blit_o{
	    .renderer        = renderer,
	    .blend_preset    = blend_preset,
	    .reference_count = 1,
	};

	return self;
}

// ----------------------------------------------------------------------

static void le_fx_blit_dec_ref_count( le_image_fx_blit_o* self ) {
	if ( --self->reference_count == 0 ) {
		delete self;
	}
}

// ----------------------------------------------------------------------

static void le_fx_blit_destroy( le_image_fx_blit_o* self ) {
	// Decrement reference count. In case there is no callback
	// in flight, this will trigger deleting the object as there
	// will be no more owners of the object.
	le_fx_blit_dec_ref_count( self );
}

// ----------------------------------------------------------------------
static void le_fx_blit_apply( le_image_fx_blit_o* self, le_rendergraph_o* rg, le_image_resource_handle_t* image_src, le_image_resource_handle_t* image_dst ) {

	[[unlikely]] if ( false == self->was_setup ) {
		// setup on first use
		le_fx_setup_blit( self );
	}

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

		        // we want to make sure that the current attachment blend preset exists
		        // - if it does not exist in our cache, then we must create a new one
		        // and add it to the cache.

		        encoder
		            .bindGraphicsPipeline( fx->pipeline_handle )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), fx->tex_blit_source )
		            .draw( 4 );
	        } )
	        .setCleanupCallback( self, []( void* user_data ) {
		        auto fx = static_cast<le_image_fx_blit_o*>( user_data );
		        // Decrement the reference count to the object as this callback
		        // has finished and therefore releases its reference to the object.
		        // this gets executed regardless of whether the `blit_pass`
		        // renderpass is executed or not.
		        le_fx_blit_dec_ref_count( fx );
	        } );

	// Increase the reference count since we add a callback that refers to
	// the object for the duration the callback's lifetime.
	self->reference_count++;

	// Add the renderpass to the rendergraph.
	//
	// The rendergraph will `on_cleanup` of this renderpass decrement the reference count
	// to the blit object contained in `self`; This guarantees that the object stays alive
	// while it is in-flight.

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