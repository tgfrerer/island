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
le_render_module_add_bloom_pass(
    le_render_module_o *        module,
    le_resource_handle_t const &input,
    le_resource_handle_t const &output,
    uint32_t const &            width,
    uint32_t const &            height ) {

	// we must introduce all transient resources

	static auto texInput            = LE_IMAGE_SAMPLER_RESOURCE( "input_tex" );
	static auto samplerInfoImgInput = le::ImageSamplerInfoBuilder( input ).build();

	struct RenderTarget {
		le_resource_handle_t image;
		le_resource_handle_t imageSampler;
		LeImageSamplerInfo   info;
	};

	struct BlurSettings {
		glm::vec2            blur_direction{};
		le_resource_handle_t source;
		size_t               kernel_define_index;
	};

	static RenderTarget targets_blur_h[] = {
	    {LE_IMG_RESOURCE( "bloom_blur_h_0" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_h_0" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_0" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_1" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_h_1" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_1" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_2" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_h_2" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_2" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_3" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_h_3" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_3" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_h_4" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_h_4" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_h_4" ) ).build()},
	};

	static RenderTarget targets_blur_v[] = {
	    {LE_IMG_RESOURCE( "bloom_blur_v_0" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_v_0" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_0" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_1" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_v_1" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_1" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_2" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_v_2" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_2" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_3" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_v_3" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_3" ) ).build()},
	    {LE_IMG_RESOURCE( "bloom_blur_v_4" ), LE_IMAGE_SAMPLER_RESOURCE( "bloom_tex_v_4" ), le::ImageSamplerInfoBuilder( LE_IMG_RESOURCE( "bloom_blur_v_4" ) ).build()},
	};

	static BlurSettings BlurSettingsH[ 5 ] = {
	    {{1.f, 0.f}, targets_blur_v[ 0 ].imageSampler, 0},
	    {{1.f, 0.f}, targets_blur_v[ 1 ].imageSampler, 1},
	    {{1.f, 0.f}, targets_blur_v[ 2 ].imageSampler, 2},
	    {{1.f, 0.f}, targets_blur_v[ 3 ].imageSampler, 3},
	    {{1.f, 0.f}, targets_blur_v[ 4 ].imageSampler, 4},
	};

	static BlurSettings BlurSettingsV[ 5 ] = {
	    {{0.f, 1.f}, targets_blur_h[ 0 ].imageSampler, 0},
	    {{0.f, 1.f}, targets_blur_h[ 1 ].imageSampler, 1},
	    {{0.f, 1.f}, targets_blur_h[ 2 ].imageSampler, 2},
	    {{0.f, 1.f}, targets_blur_h[ 3 ].imageSampler, 3},
	    {{0.f, 1.f}, targets_blur_h[ 4 ].imageSampler, 4},
	};

	static auto LOAD_DONT_CARE =
	    le::ImageAttachmentInfoBuilder()
	        .setLoadOp( le::AttachmentLoadOp::eDontCare )
	        .build();

	static auto LOAD_CLEAR =
	    le::ImageAttachmentInfoBuilder()
	        .setLoadOp( le::AttachmentLoadOp::eClear )
	        .build();

	auto luminosity_high_pass_fun = []( le_command_buffer_encoder_o *encoder_, void * ) {
		le::Encoder encoder{encoder_};

		auto *                     pm           = encoder.getPipelineManager();
		static le_shader_module_o *quadVert     = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		static le_shader_module_o *highPassFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/luminosity_high_pass.frag", {le::ShaderStage::eFragment}, "" );

		struct Params {
			glm::vec3 defaultColor{0};            // vec3(0)
			float     defaultOpacity{0};          // 0
			float     luminosityThreshold{0.75f}; // 1.f
			float     smoothWidth{0.01f};         // 1.0
		};

		Params params{};

		static auto pipeline =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( quadVert )
		        .addShaderStage( highPassFrag )
		        .build();

		encoder
		    .bindGraphicsPipeline( pipeline )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), texInput )
		    .setArgumentData( LE_ARGUMENT_NAME( "Params" ), &params, sizeof( Params ) )
		    .draw( 4 );
	};

	auto blur_render_fun = []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
		le::Encoder encoder{encoder_};
		auto        settings = static_cast<BlurSettings *>( user_data );

		auto extent = encoder.getRenderpassExtent();

		auto *pm = encoder.getPipelineManager();

		static char const *KERNEL_DEFINES[] = {
		    "KERNEL_RADIUS=3",
		    "KERNEL_RADIUS=5",
		    "KERNEL_RADIUS=7",
		    "KERNEL_RADIUS=9",
		    "KERNEL_RADIUS=11",
		};

		static le_shader_module_o *quadVert           = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		static le_shader_module_o *gaussianBlurFrag[] = {
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, KERNEL_DEFINES[ 0 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, KERNEL_DEFINES[ 1 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, KERNEL_DEFINES[ 2 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, KERNEL_DEFINES[ 3 ] ),
		    le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/blur.frag", {le::ShaderStage::eFragment}, KERNEL_DEFINES[ 4 ] ),
		};

		struct BlurParams {
			glm::vec2 resolution;
			glm::vec2 direction;
		} params = {{float( extent.width ), float( extent.height )}, settings->blur_direction};

		static auto pipelineQuad = LeGraphicsPipelineBuilder( pm ).addShaderStage( quadVert ).addShaderStage( gaussianBlurFrag[ settings->kernel_define_index ] ).build();

		encoder
		    .bindGraphicsPipeline( pipelineQuad )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), settings->source )
		    .setArgumentData( LE_ARGUMENT_NAME( "BlurParams" ), &params, sizeof( BlurParams ) )
		    .draw( 4 );
	};

	auto combine_render_fun = []( le_command_buffer_encoder_o *encoder_, void * ) {
		le::Encoder encoder{encoder_};

		auto *                     pm              = encoder.getPipelineManager();
		static le_shader_module_o *quadVert        = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		static le_shader_module_o *quadCombineFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/ue_bloom_combine.frag", {le::ShaderStage::eFragment}, "" );

		static auto pipelineQuad =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage( quadVert )
		        .addShaderStage( quadCombineFrag )
		        .build();

		encoder
		    .bindGraphicsPipeline( pipelineQuad )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), texInput )

		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), targets_blur_v[ 0 ].imageSampler, 0 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), targets_blur_v[ 1 ].imageSampler, 1 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), targets_blur_v[ 2 ].imageSampler, 2 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), targets_blur_v[ 3 ].imageSampler, 3 )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), targets_blur_v[ 4 ].imageSampler, 4 )

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
	        .setExecuteCallback( nullptr, luminosity_high_pass_fun );

	{
		using namespace le_renderer;

		render_module_i.add_renderpass( module, passHighPass );

		uint32_t w = width;
		uint32_t h = height;

		for ( size_t i = 0; i != 5; i++ ) {

			w = std::max<uint32_t>( 1, w / 2 );
			h = std::max<uint32_t>( 1, h / 2 );

			char pass_name[ 32 ];
			snprintf( pass_name, sizeof( pass_name ), "blur_h_%lu", i );

			auto passBlurHorizontal =
			    le::RenderPass( pass_name, LE_RENDER_PASS_TYPE_DRAW )
			        .sampleTexture( targets_blur_v[ i ].imageSampler, targets_blur_v[ i ].info ) // read
			        .addColorAttachment( targets_blur_h[ i ].image, LOAD_DONT_CARE )             // write
			        .setWidth( w )
			        .setHeight( h )
			        .setExecuteCallback( &BlurSettingsH[ i ], blur_render_fun );

			snprintf( pass_name, sizeof( pass_name ), "blur_v_%lu", i );

			auto passBlurVertical =
			    le::RenderPass( pass_name, LE_RENDER_PASS_TYPE_DRAW )
			        .sampleTexture( targets_blur_h[ i ].imageSampler, targets_blur_h[ i ].info ) // read
			        .addColorAttachment( targets_blur_v[ i ].image, LOAD_DONT_CARE )             // write
			        .setWidth( w )
			        .setHeight( h )
			        .setExecuteCallback( &BlurSettingsV[ i ], blur_render_fun );

			render_module_i.add_renderpass( module, passBlurHorizontal );
			render_module_i.add_renderpass( module, passBlurVertical );
		}

		auto passCombine =
		    le::RenderPass( "bloom_combine", LE_RENDER_PASS_TYPE_DRAW )
		        .setIsRoot( true )
		        .sampleTexture( texInput, samplerInfoImgInput )
		        .sampleTexture( targets_blur_v[ 0 ].imageSampler, targets_blur_v[ 0 ].info )
		        .sampleTexture( targets_blur_v[ 1 ].imageSampler, targets_blur_v[ 1 ].info )
		        .sampleTexture( targets_blur_v[ 2 ].imageSampler, targets_blur_v[ 2 ].info )
		        .sampleTexture( targets_blur_v[ 3 ].imageSampler, targets_blur_v[ 3 ].info )
		        .sampleTexture( targets_blur_v[ 4 ].imageSampler, targets_blur_v[ 4 ].info )
		        .addColorAttachment( output, LOAD_DONT_CARE ) // color attachment
		        .setExecuteCallback( nullptr, combine_render_fun );

		render_module_i.add_renderpass( module, passCombine );
	}

	// TODO: add passes to module.
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_bloom_pass_api( void *api ) {
	auto &api_i = static_cast<le_bloom_pass_api *>( api )->le_bloom_pass_i;

	api_i.le_render_module_add_bloom_pass = le_render_module_add_bloom_pass;
}
