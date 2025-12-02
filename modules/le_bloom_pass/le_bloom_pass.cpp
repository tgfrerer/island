#include "le_bloom_pass.h"
#include "le_core.h"

#include "le_renderer.hpp"
#include "le_pipeline_builder.h"

#include "glm/glm.hpp"

#include <algorithm> // for min/max
#include <stdio.h>

static void
le_render_module_add_blit_pass(
    le_renderer_o*                  renderer,
    le_rendergraph_o*               module,
    le_image_resource_handle const& input,
    le_image_resource_handle const& output ) {

	static auto SRC_TEX_UNIT_0 = le_renderer_api_i->le_renderer_i.produce_texture_handle( renderer, "src_tex_unit_0" );

	auto pass_blit_exec = []( le_command_buffer_encoder_o* encoder_, void* ) {
		le::GraphicsEncoder encoder{ encoder_ };
		auto*       pm = encoder.getPipelineManager();

		static auto quadVert =
		    LeShaderModuleBuilder( pm )
		        .setShaderStage( le::ShaderStage::eVertex )
		        .setSourceFilePath( "./resources/shaders/fullscreenQuad.vert" )
		        .build();
		static auto blitFrag =
		    LeShaderModuleBuilder( pm )
		        .setShaderStage( le::ShaderStage::eFragment )
		        .setSourceFilePath( "./resources/shaders/fullscreenQuad.frag" )
		        .build();

		static auto pipeline =
		    LeGraphicsPipelineBuilder( pm )
		        .addShaderStage( quadVert )
		        .addShaderStage( blitFrag )
		        .withAttachmentBlendState()
		        .setBlendEnable( false ) // we don't want any blending, just a straight copy.
		        .end()
		        .build();

		encoder
		    .bindGraphicsPipeline( pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), SRC_TEX_UNIT_0 )
		    .draw( 4 );
	};

	auto passBlit =
	    le::RenderPass( "blit", le::QueueFlagBits::eGraphics )
	        .sampleTexture( SRC_TEX_UNIT_0, le::ImageSamplerInfoBuilder( input ).build() )
	        .addColorAttachment( output )
	        .setExecuteCallback( nullptr, pass_blit_exec ) //
	    ;

	using namespace le_renderer;
	rendergraph_i.add_renderpass( module, passBlit );
}

static void
le_render_module_add_bloom_pass(
    le_renderer_o*                  renderer,
    le_rendergraph_o*               module,
    le_image_resource_handle const& input,
    le_image_resource_handle const& output,
    uint32_t const&                 width,
    uint32_t const&                 height,
    le_bloom_pass_api::params_t*    params ) {

	// we must introduce all transient resources

	using namespace le_renderer;

	static auto TEX_INPUT        = renderer_i.produce_texture_handle( renderer, "input_tex" );
	static auto SRC_TEX_UNIT_0   = renderer_i.produce_texture_handle( renderer, "src_tex_unit_0" );
	static auto SRC_TEX_UNIT_0_0 = renderer_i.produce_texture_handle( renderer, "src_tex_unit_0.0" );
	static auto SRC_TEX_UNIT_0_1 = renderer_i.produce_texture_handle( renderer, "src_tex_unit_0.1" );
	static auto SRC_TEX_UNIT_0_2 = renderer_i.produce_texture_handle( renderer, "src_tex_unit_0.2" );
	static auto SRC_TEX_UNIT_0_3 = renderer_i.produce_texture_handle( renderer, "src_tex_unit_0.3" );
	static auto SRC_TEX_UNIT_0_4 = renderer_i.produce_texture_handle( renderer, "src_tex_unit_0.4" );

	// Important to create these handles only once

	static le_image_resource_handle bloom_blur_h[ 5 ] = {
	    LE_IMG_RESOURCE( "bloom_blur_h_0" ),
	    LE_IMG_RESOURCE( "bloom_blur_h_1" ),
	    LE_IMG_RESOURCE( "bloom_blur_h_2" ),
	    LE_IMG_RESOURCE( "bloom_blur_h_3" ),
	    LE_IMG_RESOURCE( "bloom_blur_h_4" ),
	};
	static le_image_resource_handle bloom_blur_v[ 5 ] = {
	    LE_IMG_RESOURCE( "bloom_blur_v_0" ),
	    LE_IMG_RESOURCE( "bloom_blur_v_1" ),
	    LE_IMG_RESOURCE( "bloom_blur_v_2" ),
	    LE_IMG_RESOURCE( "bloom_blur_v_3" ),
	    LE_IMG_RESOURCE( "bloom_blur_v_4" ),
	};

	static auto samplerInfoImgInput = le::ImageSamplerInfoBuilder( input ).build();

	struct RenderTarget {
		le_image_resource_handle  image;
		le_image_sampler_info_t info;
	};

	struct BlurSettings {
		glm::vec2 blur_direction{};
		//		le_resource_handle_t source;
		size_t kernel_define_index;
	};

	static RenderTarget targets_blur_h[] = {
	    { bloom_blur_h[ 0 ], le::ImageSamplerInfoBuilder( bloom_blur_h[ 0 ] ).build() },
	    { bloom_blur_h[ 1 ], le::ImageSamplerInfoBuilder( bloom_blur_h[ 1 ] ).build() },
	    { bloom_blur_h[ 2 ], le::ImageSamplerInfoBuilder( bloom_blur_h[ 2 ] ).build() },
	    { bloom_blur_h[ 3 ], le::ImageSamplerInfoBuilder( bloom_blur_h[ 3 ] ).build() },
	    { bloom_blur_h[ 4 ], le::ImageSamplerInfoBuilder( bloom_blur_h[ 4 ] ).build() },
	};

	static RenderTarget targets_blur_v[] = {
	    { bloom_blur_v[ 0 ], le::ImageSamplerInfoBuilder( bloom_blur_v[ 0 ] ).build() },
	    { bloom_blur_v[ 1 ], le::ImageSamplerInfoBuilder( bloom_blur_v[ 1 ] ).build() },
	    { bloom_blur_v[ 2 ], le::ImageSamplerInfoBuilder( bloom_blur_v[ 2 ] ).build() },
	    { bloom_blur_v[ 3 ], le::ImageSamplerInfoBuilder( bloom_blur_v[ 3 ] ).build() },
	    { bloom_blur_v[ 4 ], le::ImageSamplerInfoBuilder( bloom_blur_v[ 4 ] ).build() },
	};

	static BlurSettings BlurSettingsH[ 5 ] = {
	    { { 1.f, 0.f }, 0 },
	    { { 1.f, 0.f }, 1 },
	    { { 1.f, 0.f }, 2 },
	    { { 1.f, 0.f }, 3 },
	    { { 1.f, 0.f }, 4 },
	};

	static BlurSettings BlurSettingsV[ 5 ] = {
	    { { 0.f, 1.f }, 0 },
	    { { 0.f, 1.f }, 1 },
	    { { 0.f, 1.f }, 2 },
	    { { 0.f, 1.f }, 3 },
	    { { 0.f, 1.f }, 4 },
	};

	static auto LOAD_DONT_CARE =
	    le::ImageAttachmentInfoBuilder()
	        .setLoadOp( le::AttachmentLoadOp::eDontCare )
	        .build();

	static auto LOAD_CLEAR =
	    le::ImageAttachmentInfoBuilder()
	        .setLoadOp( le::AttachmentLoadOp::eClear )
	        .build();

	static auto LOAD_LOAD =
	    le::ImageAttachmentInfoBuilder()
	        .setLoadOp( le::AttachmentLoadOp::eLoad )
	        .build();

	auto luminosity_high_pass_fun = []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
		le::GraphicsEncoder encoder{ encoder_ };

		static le_bloom_pass_api::params_t fallback_params{};
		le_bloom_pass_api::params_t*       params = &fallback_params;

		if ( user_data ) {
			params = static_cast<le_bloom_pass_api::params_t*>( user_data );
		}

		auto* pm = encoder.getPipelineManager();

		static auto quadVert     = LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eVertex ).setSourceFilePath( "./resources/shaders/fullscreenQuad.vert" ).build();
		static auto highPassFrag = LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eFragment ).setSourceFilePath( "./resources/shaders/luminosity_high_pass.frag" ).build();

		static auto pipeline =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( quadVert )
		        .addShaderStage( highPassFrag )
		        .withAttachmentBlendState()
		        .setBlendEnable( false ) // we don't want any blending, just a straight copy.
		        .end()
		        .build();

		encoder
		    .bindGraphicsPipeline( pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), TEX_INPUT )
		    .setArgumentData( LE_ARGUMENT_NAME( "Params" ), &params->luma_threshold, sizeof( le_bloom_pass_api::params_t::luma_threshold_params_t ) )
		    .draw( 4 );
	};

	auto blur_render_fun = []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
		le::GraphicsEncoder encoder{ encoder_ };
		auto        settings = static_cast<BlurSettings*>( user_data );

		auto extent = encoder.getRenderpassExtent();

		auto* pm = encoder.getPipelineManager();

		static char const* BLUR_KERNEL_DEFINES[] = {
		    "KERNEL_RADIUS=3",
		    "KERNEL_RADIUS=5",
		    "KERNEL_RADIUS=7",
		    "KERNEL_RADIUS=9",
		    "KERNEL_RADIUS=11",
		};

		static auto quadVert = LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eVertex ).setSourceFilePath( "./resources/shaders/fullscreenQuad.vert" ).build();

		static le_shader_module_handle gaussianBlurFrag[] = {
		    LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eFragment ).setSourceFilePath( "./resources/shaders/blur.frag" ).setSourceDefinesString( BLUR_KERNEL_DEFINES[ 0 ] ).build(),
		    LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eFragment ).setSourceFilePath( "./resources/shaders/blur.frag" ).setSourceDefinesString( BLUR_KERNEL_DEFINES[ 1 ] ).build(),
		    LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eFragment ).setSourceFilePath( "./resources/shaders/blur.frag" ).setSourceDefinesString( BLUR_KERNEL_DEFINES[ 2 ] ).build(),
		    LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eFragment ).setSourceFilePath( "./resources/shaders/blur.frag" ).setSourceDefinesString( BLUR_KERNEL_DEFINES[ 3 ] ).build(),
		    LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eFragment ).setSourceFilePath( "./resources/shaders/blur.frag" ).setSourceDefinesString( BLUR_KERNEL_DEFINES[ 4 ] ).build(),
		};

		struct BlurParams {
			glm::vec2 resolution;
			glm::vec2 direction;
		} blur_params = { { float( extent.width ), float( extent.height ) }, settings->blur_direction };

		static auto pipeline = LeGraphicsPipelineBuilder( pm )
		                           .addShaderStage( quadVert )
		                           .addShaderStage( gaussianBlurFrag[ settings->kernel_define_index ] )
		                           .withAttachmentBlendState()
		                           .setBlendEnable( false ) // we don't want any blending, just a straight copy.
		                           .end()
		                           .build();

		encoder
		    .bindGraphicsPipeline( pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), SRC_TEX_UNIT_0 )
		    .setArgumentData( LE_ARGUMENT_NAME( "params" ), &blur_params, sizeof( BlurParams ) )
		    .draw( 4 );
	};

	auto combine_render_fun = []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
		le::GraphicsEncoder encoder{ encoder_ };

		static le_bloom_pass_api::params_t fallback_params{};
		le_bloom_pass_api::params_t*       params = &fallback_params;

		if ( user_data ) {
			params = static_cast<le_bloom_pass_api::params_t*>( user_data );
		}

		auto*       pm              = encoder.getPipelineManager();
		static auto quadVert        = LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eVertex ).setSourceFilePath( "./resources/shaders/fullscreenQuad.vert" ).build();
		static auto quadCombineFrag = LeShaderModuleBuilder( pm ).setShaderStage( le::ShaderStage::eFragment ).setSourceFilePath( "./resources/shaders/ue_bloom_combine.frag" ).build();

		static auto pipeline =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( quadVert )
		        .addShaderStage( quadCombineFrag )
		        .withAttachmentBlendState()
		        .usePreset( le::AttachmentBlendPreset::eAdd ) // we want this screened on top
		        .end()
		        .build();

		encoder
		    .bindGraphicsPipeline( pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), SRC_TEX_UNIT_0_0, 0 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), SRC_TEX_UNIT_0_1, 1 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), SRC_TEX_UNIT_0_2, 2 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), SRC_TEX_UNIT_0_3, 3 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), SRC_TEX_UNIT_0_4, 4 )
		    .setArgumentData( LE_ARGUMENT_NAME( "Params" ), &params->bloom, sizeof( le_bloom_pass_api::params_t::bloom_params_t ) )
		    .draw( 4 );
	};

	// -- First, have a pass which filters out anything which is not bright. (do this at half resolution)
	// -- Then, blur and scale down image 5 times
	// -- Finally, combine the main image with the blurred image

	auto passHighPass =
	    le::RenderPass( "high_pass", le::QueueFlagBits::eGraphics )
	        .sampleTexture( TEX_INPUT, samplerInfoImgInput )
	        .addColorAttachment( targets_blur_v[ 0 ].image, LOAD_CLEAR )
	        .setWidth( width / 2 )
	        .setHeight( height / 2 )
	        .setExecuteCallback( params, luminosity_high_pass_fun );

	{
		using namespace le_renderer;

		rendergraph_i.add_renderpass( module, passHighPass );

		uint32_t w = width;
		uint32_t h = height;

		le_image_sampler_info_t source_info = targets_blur_v[ 0 ].info;

		for ( size_t i = 0; i != 5; i++ ) {

			w = std::max<uint32_t>( 1, w / 2 );
			h = std::max<uint32_t>( 1, h / 2 );

			char pass_name[ 32 ];
			snprintf( pass_name, sizeof( pass_name ), "blur_h_%zu", i );

			auto passBlurHorizontal =
			    le::RenderPass( pass_name, le::QueueFlagBits::eGraphics )
			        .sampleTexture( SRC_TEX_UNIT_0, source_info )                    // read
			        .addColorAttachment( targets_blur_h[ i ].image, LOAD_DONT_CARE ) // write
			        .setWidth( w )
			        .setHeight( h )
			        .setExecuteCallback( &BlurSettingsH[ i ], blur_render_fun );

			snprintf( pass_name, sizeof( pass_name ), "blur_v_%zu", i );

			auto passBlurVertical =
			    le::RenderPass( pass_name, le::QueueFlagBits::eGraphics )
			        .sampleTexture( SRC_TEX_UNIT_0, targets_blur_h[ i ].info )       // read
			        .addColorAttachment( targets_blur_v[ i ].image, LOAD_DONT_CARE ) // write
			        .setWidth( w )
			        .setHeight( h )
			        .setExecuteCallback( &BlurSettingsV[ i ], blur_render_fun );

			source_info = targets_blur_v[ i ].info;

			rendergraph_i.add_renderpass( module, passBlurHorizontal );
			rendergraph_i.add_renderpass( module, passBlurVertical );
		}

		auto passCombine =
		    le::RenderPass( "bloom_combine", le::QueueFlagBits::eGraphics )
		        .sampleTexture( SRC_TEX_UNIT_0_0, targets_blur_v[ 0 ].info )
		        .sampleTexture( SRC_TEX_UNIT_0_1, targets_blur_v[ 1 ].info )
		        .sampleTexture( SRC_TEX_UNIT_0_2, targets_blur_v[ 2 ].info )
		        .sampleTexture( SRC_TEX_UNIT_0_3, targets_blur_v[ 3 ].info )
		        .sampleTexture( SRC_TEX_UNIT_0_4, targets_blur_v[ 4 ].info )
		        .addColorAttachment( output, LOAD_LOAD ) // color attachment
		        .setExecuteCallback( params, combine_render_fun );

		rendergraph_i.add_renderpass( module, passCombine );
		rendergraph_i.declare_resource( module, bloom_blur_v[ 4 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_v[ 3 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_v[ 2 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_v[ 1 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_v[ 0 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_h[ 4 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_h[ 3 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_h[ 2 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_h[ 1 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
		rendergraph_i.declare_resource( module, bloom_blur_h[ 0 ], le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlagBits::eColorAttachment | le::ImageUsageFlagBits::eSampled ).build() );
	}
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_bloom_pass, api ) {
	auto& api_i = static_cast<le_bloom_pass_api*>( api )->le_bloom_pass_i;

	api_i.le_render_module_add_bloom_pass = le_render_module_add_bloom_pass;
	api_i.le_render_module_add_blit_pass  = le_render_module_add_blit_pass;
}
