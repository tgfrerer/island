#include "le_bloom_pass.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h" // for shader module creation

#include "le_renderer/le_renderer.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <algorithm> // for min/max

static void
le_render_module_add_blit_pass(
    le_render_module_o *        module,
    le_resource_handle_t const &input,
    le_resource_handle_t const &output ) {

	auto pass_blit_exec = []( le_command_buffer_encoder_o *encoder_, void * ) {
		le::Encoder encoder{encoder_};
		auto *      pm = encoder.getPipelineManager();

		static le_shader_module_o *quadVert = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		static le_shader_module_o *blitFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.frag", {le::ShaderStage::eFragment}, "" );

		static auto pipeline = LeGraphicsPipelineBuilder( pm )
		                           .addShaderStage( quadVert )
		                           .addShaderStage( blitFrag )
		                           .withAttachmentBlendState()
		                           .setBlendEnable( false ) // we don't want any blending, just a straight copy.
		                           .end()
		                           .build();

		encoder
		    .bindGraphicsPipeline( pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0" ) )
		    .draw( 4 );
	};

	auto passBlit =
	    le::RenderPass( "blit", LE_RENDER_PASS_TYPE_DRAW )
	        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0" ), le::ImageSamplerInfoBuilder( input ).build() )
	        .addColorAttachment( output )
	        .setExecuteCallback( nullptr, pass_blit_exec ) //
	    ;

	using namespace le_renderer;
	render_module_i.add_renderpass( module, passBlit );
}

static void
le_render_module_add_bloom_pass(
    le_render_module_o *         module,
    le_resource_handle_t const & input,
    le_resource_handle_t const & output,
    uint32_t const &             width,
    uint32_t const &             height,
    le_bloom_pass_api::params_t *params ) {

	// we must introduce all transient resources

	static auto texInput            = LE_IMAGE_SAMPLER_RESOURCE( "input_tex" );
	static auto samplerInfoImgInput = le::ImageSamplerInfoBuilder( input ).build();

	struct RenderTarget {
		le_resource_handle_t image;
		LeImageSamplerInfo   info;
	};

	struct BlurSettings {
		glm::vec2 blur_direction{};
		//		le_resource_handle_t source;
		size_t kernel_define_index;
	};

	static RenderTarget targets_blur_h[] = {
	    {LE_IMG_RESOURCE( "bloom_blur_h_0" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_0" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_1" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_1" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_2" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_2" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_3" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_3" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_4" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_4" ) ).build()},
	};

	static RenderTarget targets_blur_v[] = {
	    {LE_IMG_RESOURCE( "bloom_blur_v_0" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_0" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_1" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_1" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_2" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_2" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_3" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_3" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_4" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_4" ) ).build()},
	};

	static BlurSettings BlurSettingsH[ 5 ] = {
	    {{1.f, 0.f}, 0},
	    {{1.f, 0.f}, 1},
	    {{1.f, 0.f}, 2},
	    {{1.f, 0.f}, 3},
	    {{1.f, 0.f}, 4},
	};

	static BlurSettings BlurSettingsV[ 5 ] = {
	    {{0.f, 1.f}, 0},
	    {{0.f, 1.f}, 1},
	    {{0.f, 1.f}, 2},
	    {{0.f, 1.f}, 3},
	    {{0.f, 1.f}, 4},
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

	auto luminosity_high_pass_fun = []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
		le::Encoder encoder{encoder_};

		static le_bloom_pass_api::params_t fallback_params{};
		le_bloom_pass_api::params_t *      params = &fallback_params;

		if ( user_data ) {
			params = static_cast<le_bloom_pass_api::params_t *>( user_data );
		}

		auto *                     pm           = encoder.getPipelineManager();
		static le_shader_module_o *quadVert     = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		static le_shader_module_o *highPassFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/luminosity_high_pass.frag", {le::ShaderStage::eFragment}, "" );

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
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), texInput )
		    .setArgumentData( LE_ARGUMENT_NAME( "Params" ), &params->luma_threshold, sizeof( le_bloom_pass_api::params_t::luma_threshold_params_t ) )
		    .draw( 4 );
	};

	auto blur_render_fun = []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
		le::Encoder encoder{encoder_};
		auto        settings = static_cast<BlurSettings *>( user_data );

		auto extent = encoder.getRenderpassExtent();

		auto *pm = encoder.getPipelineManager();

		static char const *BLUR_KERNEL_DEFINES[] = {
		    "KERNEL_RADIUS=3",
		    "KERNEL_RADIUS=5",
		    "KERNEL_RADIUS=7",
		    "KERNEL_RADIUS=9",
		    "KERNEL_RADIUS=11",
		};

		static le_shader_module_o *quadVert           = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		static le_shader_module_o *gaussianBlurFrag[] = {
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, BLUR_KERNEL_DEFINES[ 0 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, BLUR_KERNEL_DEFINES[ 1 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, BLUR_KERNEL_DEFINES[ 2 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, BLUR_KERNEL_DEFINES[ 3 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, BLUR_KERNEL_DEFINES[ 4 ] ),
		};

		struct BlurParams {
			glm::vec2 resolution;
			glm::vec2 direction;
		} blur_params = {{float( extent.width ), float( extent.height )}, settings->blur_direction};

		static auto pipeline = LeGraphicsPipelineBuilder( pm )
		                           .addShaderStage( quadVert )
		                           .addShaderStage( gaussianBlurFrag[ settings->kernel_define_index ] )
		                           .withAttachmentBlendState()
		                           .setBlendEnable( false ) // we don't want any blending, just a straight copy.
		                           .end()
		                           .build();

		encoder
		    .bindGraphicsPipeline( pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0" ) )
		    .setArgumentData( LE_ARGUMENT_NAME( "BlurParams" ), &blur_params, sizeof( BlurParams ) )
		    .draw( 4 );
	};

	auto combine_render_fun = []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
		le::Encoder encoder{encoder_};

		static le_bloom_pass_api::params_t fallback_params{};
		le_bloom_pass_api::params_t *      params = &fallback_params;

		if ( user_data ) {
			params = static_cast<le_bloom_pass_api::params_t *>( user_data );
		}

		auto *                     pm              = encoder.getPipelineManager();
		static le_shader_module_o *quadVert        = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		static le_shader_module_o *quadCombineFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/ue_bloom_combine.frag", {le::ShaderStage::eFragment}, "" );

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
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.0" ), 0 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.1" ), 1 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.2" ), 2 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.3" ), 3 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.4" ), 4 )
		    .setArgumentData( LE_ARGUMENT_NAME( "Params" ), &params->bloom, sizeof( le_bloom_pass_api::params_t::bloom_params_t ) )
		    .draw( 4 );
	};

	// first, have a pass which filters out anything which is not bright. (do this at half resolution)

	// then, blur and scale down image 5 times

	// then combine the main image with the blurred image

	auto passHighPass =
	    le::RenderPass( "high_pass", LE_RENDER_PASS_TYPE_DRAW )
	        .sampleTexture( texInput, samplerInfoImgInput )
	        .addColorAttachment( targets_blur_v[ 0 ].image, LOAD_CLEAR )
	        .setWidth( width / 2 )
	        .setHeight( height / 2 )
	        .setExecuteCallback( params, luminosity_high_pass_fun );

	{
		using namespace le_renderer;

		render_module_i.add_renderpass( module, passHighPass );

		uint32_t w = width;
		uint32_t h = height;

		LeImageSamplerInfo source_info = targets_blur_v[ 0 ].info;

		for ( size_t i = 0; i != 5; i++ ) {

			w = std::max<uint32_t>( 1, w / 2 );
			h = std::max<uint32_t>( 1, h / 2 );

			char pass_name[ 32 ];
			snprintf( pass_name, sizeof( pass_name ), "blur_h_%lu", i );

			auto passBlurHorizontal =
			    le::RenderPass( pass_name, LE_RENDER_PASS_TYPE_DRAW )
			        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0" ), source_info ) // read
			        .addColorAttachment( targets_blur_h[ i ].image, LOAD_DONT_CARE )             // write
			        .setWidth( w )
			        .setHeight( h )
			        .setExecuteCallback( &BlurSettingsH[ i ], blur_render_fun );

			snprintf( pass_name, sizeof( pass_name ), "blur_v_%lu", i );

			auto passBlurVertical =
			    le::RenderPass( pass_name, LE_RENDER_PASS_TYPE_DRAW )
			        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0" ), targets_blur_h[ i ].info ) // read
			        .addColorAttachment( targets_blur_v[ i ].image, LOAD_DONT_CARE )                          // write
			        .setWidth( w )
			        .setHeight( h )
			        .setExecuteCallback( &BlurSettingsV[ i ], blur_render_fun );

			source_info = targets_blur_v[ i ].info;

			render_module_i.add_renderpass( module, passBlurHorizontal );
			render_module_i.add_renderpass( module, passBlurVertical );
		}

		auto passCombine =
		    le::RenderPass( "bloom_combine", LE_RENDER_PASS_TYPE_DRAW )
		        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.0" ), targets_blur_v[ 0 ].info )
		        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.1" ), targets_blur_v[ 1 ].info )
		        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.2" ), targets_blur_v[ 2 ].info )
		        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.3" ), targets_blur_v[ 3 ].info )
		        .sampleTexture( LE_IMAGE_SAMPLER_RESOURCE( "src_tex_unit_0.4" ), targets_blur_v[ 4 ].info )
		        .addColorAttachment( output, LOAD_LOAD ) // color attachment
		        .setExecuteCallback( params, combine_render_fun );

		render_module_i.add_renderpass( module, passCombine );
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_bloom_pass_api( void *api ) {
	auto &api_i = static_cast<le_bloom_pass_api *>( api )->le_bloom_pass_i;

	api_i.le_render_module_add_bloom_pass = le_render_module_add_bloom_pass;
	api_i.le_render_module_add_blit_pass  = le_render_module_add_blit_pass;
}
