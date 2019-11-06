#include "le_bloom_pass.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h" // for shader module creation

#include "le_renderer/le_renderer.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

struct shader_module_store_t {
	le_shader_module_o *quadVert         = nullptr;
	le_shader_module_o *quadFrag         = nullptr;
	le_shader_module_o *gaussianBlurFrag = nullptr;
	le_shader_module_o *quadCombineFrag  = nullptr;
};

static void le_render_module_add_bloom_pass( le_render_module_o *module, le_resource_handle_t const &input, le_resource_handle_t const &output, uint32_t const &width, uint32_t const &height ) {

	// we must introduce all transient resources

	static auto texMain = LE_IMAGE_SAMPLER_RESOURCE( "input_tex" );

	static auto samplerInfoImgMain = le::ImageSamplerInfoBuilder()
	                                     .withImageViewInfo()
	                                     .setImage( input )
	                                     .end()
	                                     .build();

	static auto imgQuarterPing     = LE_IMG_RESOURCE( "quarter_image_ping" );
	static auto texQuarterPing     = LE_IMAGE_SAMPLER_RESOURCE( "quarter_tex_ping" );
	static auto samplerInfoImgPing = le::ImageSamplerInfoBuilder()
	                                     .withImageViewInfo()
	                                     .setImage( imgQuarterPing )
	                                     .end()
	                                     .build();
	static auto imgQuarterPong     = LE_IMG_RESOURCE( "quarter_image_pong" );
	static auto texQuarterPong     = LE_IMAGE_SAMPLER_RESOURCE( "quarter_tex_pong" );
	static auto samplerInfoImgPong = le::ImageSamplerInfoBuilder()
	                                     .withImageViewInfo()
	                                     .setImage( imgQuarterPong )
	                                     .end()
	                                     .build();

	static auto LOAD_DONT_CARE =
	    le::ImageAttachmentInfoBuilder()
	        .setLoadOp( le::AttachmentLoadOp::eDontCare )
	        .build();

	static size_t imgScale = 4;
	static float  dirScale = 1;

	auto passQuarterPing =
	    le::RenderPass( "quarter", LE_RENDER_PASS_TYPE_DRAW )
	        .sampleTexture( texMain, samplerInfoImgMain )
	        .addColorAttachment( imgQuarterPing, LOAD_DONT_CARE )
	        .setWidth( width / imgScale )
	        .setHeight( height / imgScale )
	        .setExecuteCallback( nullptr, []( le_command_buffer_encoder_o *encoder_, void * ) {
		        le::Encoder encoder{encoder_};

		        auto extent = encoder.getRenderpassExtent();

		        auto *pm = encoder.getPipelineManager();

		        static le_shader_module_o *quadVert         = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		        static le_shader_module_o *gaussianBlurFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/gaussian_blur.frag", {le::ShaderStage::eFragment}, "" );

		        struct BlurParams {
			        glm::vec2 resolution;
			        glm::vec2 direction;
		        } params = {{float( extent.width ), float( extent.height )}, {0, dirScale}};

		        static auto pipelineQuad =
		            LeGraphicsPipelineBuilder( pm )
		                .addShaderStage( quadVert )
		                .addShaderStage( gaussianBlurFrag )
		                .build();

		        encoder
		            .bindGraphicsPipeline( pipelineQuad )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), texMain )
		            .setArgumentData( LE_ARGUMENT_NAME( "BlurParams" ), &params, sizeof( BlurParams ) )
		            .draw( 4 );
	        } );

	auto passQuarterPong =
	    le::RenderPass( "quarter", LE_RENDER_PASS_TYPE_DRAW )
	        .sampleTexture( texQuarterPing, samplerInfoImgPing )
	        .addColorAttachment( imgQuarterPong, LOAD_DONT_CARE )
	        .setWidth( width / imgScale )
	        .setHeight( height / imgScale )
	        .setExecuteCallback( nullptr, []( le_command_buffer_encoder_o *encoder_, void * ) {
		        le::Encoder encoder{encoder_};

		        auto extent = encoder.getRenderpassExtent();

		        struct BlurParams {
			        glm::vec2 resolution;
			        glm::vec2 direction;
		        } params = {{float( extent.width ), float( extent.height )}, {dirScale, 0}};

		        auto *pm = encoder.getPipelineManager();

		        static le_shader_module_o *quadVert         = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		        static le_shader_module_o *gaussianBlurFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/gaussian_blur.frag", {le::ShaderStage::eFragment}, "" );

		        static auto pipelineQuad =
		            LeGraphicsPipelineBuilder( pm )
		                .addShaderStage( quadVert )
		                .addShaderStage( gaussianBlurFrag )
		                .build();

		        encoder
		            .bindGraphicsPipeline( pipelineQuad )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), texQuarterPing )
		            .setArgumentData( LE_ARGUMENT_NAME( "BlurParams" ), &params, sizeof( BlurParams ) )
		            .draw( 4 );
	        } );

	auto passCombine =
	    le::RenderPass( "root", LE_RENDER_PASS_TYPE_DRAW )
	        .setIsRoot( true )
	        .sampleTexture( texMain, samplerInfoImgMain )
	        .sampleTexture( texQuarterPong, samplerInfoImgPong )
	        .addColorAttachment( output, LOAD_DONT_CARE ) // color attachment
	        .setExecuteCallback( nullptr, []( le_command_buffer_encoder_o *encoder_, void * ) {
		        le::Encoder encoder{encoder_};

		        auto *                     pm              = encoder.getPipelineManager();
		        static le_shader_module_o *quadVert        = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/fullscreenQuad.vert", {le::ShaderStage::eVertex}, "" );
		        static le_shader_module_o *quadCombineFrag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/rgb_blit_combine.frag", {le::ShaderStage::eFragment}, "" );

		        static auto pipelineQuad =
		            LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		                .addShaderStage( quadVert )
		                .addShaderStage( quadCombineFrag )
		                .build();
		        encoder
		            .bindGraphicsPipeline( pipelineQuad )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), texMain )
		            .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_1" ), texQuarterPong )
		            .draw( 4 );
	        } );

	{
		using namespace le_renderer;
		render_module_i.add_renderpass( module, passQuarterPing );
		render_module_i.add_renderpass( module, passQuarterPong );
		render_module_i.add_renderpass( module, passCombine );
	}

	// TODO: add passes to module.
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_bloom_pass_api( void *api ) {
	auto &api_i = static_cast<le_bloom_pass_api *>( api )->le_bloom_pass_i;

	api_i.le_render_module_add_bloom_pass = le_render_module_add_bloom_pass;
}
