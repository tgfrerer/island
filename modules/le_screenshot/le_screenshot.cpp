#include "le_screenshot.h"
#include "le_core.h"
#include "le_renderer.h"
#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_swapchain_vk.h"
#include "le_swapchain_img.h"
#include "le_png.h"
#include "le_log.h"
#include <filesystem>

static constexpr auto LOGGER_LABEL = "le_screenshot";

namespace {
// we use an anonymous namespace so that our shaders
// don't end up in the global namespace inadvertantly
#include "shaders/blit_frag.h"
#include "shaders/fullscreen_vert.h"
} // namespace

#include <vector>

// ----------------------------------------------------------------------
// Default settings for screenshot image swapchain. Copy and modify to
// customize output.
//
static le_swapchain_img_settings_t get_default_swapchain_img_settings() {
	le_swapchain_img_settings_t settings{
		.width_hint  = 0, // 0 means to take the width of the renderer's first swapchain
		.height_hint = 0, // 0 means to take the height of the renderer's first swapchain

		.format_hint             = le::Format::eR8G8B8A8Unorm,
		.image_encoder_i         = le_png::api->le_png_image_encoder_i,
		.image_filename_template = "./capture/screenshot_%08d.png",
	};
	return settings;
}
// ----------------------------------------------------------------------

struct le_screenshot_o {
	// members
	le_pipeline_manager_o*      pipeline_manager   = nullptr; // non-owning
	le_texture_handle           tex_blit_source    = nullptr; // non-owning
	le_swapchain_handle         swapchain          = nullptr; // opaque handle to a swapchain owned by the renderer
	le_image_resource_handle    fallback_src_image = nullptr; // source image used if no source image was given explicitly (this is resolved to the image of the first available swapchain)
	le_swapchain_img_settings_t swapchain_settings = {};
	le_renderer_o*              renderer           = nullptr; // non-owning
};

// ----------------------------------------------------------------------

static le_screenshot_o* le_screenshot_create( le_renderer_o* renderer ) {
	auto self                = new le_screenshot_o{};
	self->pipeline_manager   = le_renderer_api_i->le_renderer_i.get_pipeline_manager( renderer );
	self->tex_blit_source    = le::Renderer::produceTextureHandle( "fx_blit_source" );
	self->renderer           = renderer;
	self->swapchain_settings = get_default_swapchain_img_settings();
	return self;
}

static bool le_screenshot_init() {
	// request backend capabilities -- we want to be able to use image swapchains
	return le::SwapchainVk::init( le_swapchain_img_settings_t{} );
}

// ----------------------------------------------------------------------

static void le_screenshot_destroy( le_screenshot_o* self ) {
	if ( self->swapchain ) {
		le_renderer_api_i->le_renderer_i.remove_swapchain( self->renderer, self->swapchain );
	}
	delete self;
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_vert( le_pipeline_manager_o* pm ) {
	static le_shader_module_handle s = nullptr;

	if ( s ) {
		return s;
	}

	s = LeShaderModuleBuilder( pm )
			.setSpirvCode( SPIRV_SOURCE_FULLSCREEN_VERT, sizeof( SPIRV_SOURCE_FULLSCREEN_VERT ) / sizeof( uint32_t ) )
			.setShaderStage( le::ShaderStage::eVertex )
			.setHandle( s )
			.build();

	return s;
}

// ----------------------------------------------------------------------

static le_shader_module_handle get_shader_frag_blit( le_pipeline_manager_o* pm ) {
	static le_shader_module_handle s = nullptr;

	if ( s ) {
		return s;
	}

	s = LeShaderModuleBuilder( pm )
			.setSpirvCode( SPIRV_SOURCE_BLIT_FRAG, sizeof( SPIRV_SOURCE_BLIT_FRAG ) / sizeof( uint32_t ) )
			.setShaderStage( le::ShaderStage::eFragment )
			.setHandle( s )
			.build();

	return s;
}

// ----------------------------------------------------------------------
static void le_screenshot_blit_apply( le_screenshot_o* self, le_rendergraph_o* rg, le_image_resource_handle_t* image_src, le_image_resource_handle_t* image_dst ) {

	static auto pipelineBlit =
		LeGraphicsPipelineBuilder( self->pipeline_manager )
			.addShaderStage( get_shader_vert( self->pipeline_manager ) )
			.addShaderStage( get_shader_frag_blit( self->pipeline_manager ) )
			.withAttachmentBlendState()
			.setColorBlendOp( le::BlendOp::eAdd )
			.setSrcColorBlendFactor( le::BlendFactor::eOne )
			.setDstColorBlendFactor( le::BlendFactor::eZero )
			.setAlphaBlendOp( le::BlendOp::eAdd )
			.setSrcAlphaBlendFactor( le::BlendFactor::eOne )
			.setDstAlphaBlendFactor( le::BlendFactor::eZero ) // note we don't want to add alpha - we want to just get the dst alpha
			.end()
			.build();

	auto blit_pass =
		le::RenderPass( "Screenshot BLIT" )
			.addColorAttachment(
				image_dst,
				le::ImageAttachmentInfoBuilder()
					.setLoadOp( le::AttachmentLoadOp::eDontCare )
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
				auto                fx = static_cast<typeof( self )>( user_data );
				le::GraphicsEncoder encoder{ encoder_ };
				encoder
					.bindGraphicsPipeline( pipelineBlit )
					.setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), fx->tex_blit_source )
					.draw( 4 );
			} );

	auto rendergraph = le::RenderGraph( rg );
	rendergraph
		.addRenderPass( blit_pass ) //
		;
}

// ----------------------------------------------------------------------

static bool le_screenshot_record( le_screenshot_o* self, le_rendergraph_o* rg, le_image_resource_handle src_image_, uint32_t* num_images, le_swapchain_img_settings_t const* p_img_settings ) {

	static auto logger = LeLog( LOGGER_LABEL );

	// In case we've already recorded all the frames that need recording
	// we can return early

	if ( self->swapchain == nullptr && num_images && *num_images == 0 ) {
		return false;
	}

	if ( self->swapchain && ( ( num_images == nullptr ) || ( *num_images ) == 0 ) ) {
		// if the number of images is 0, and there is a swapchain, we must destroy the swapchain
		le_renderer_api_i->le_renderer_i.remove_swapchain( self->renderer, self->swapchain );
		self->swapchain = nullptr;
		return false;
	}

	if ( num_images == nullptr ) {
		logger.warn( "Missing num_images; nullptr given." );
		return false;
	}

	// ----------| num_images != nullptr

	if ( self->swapchain == nullptr ) {

		// find out how which swapchains are available
		std::vector<le_swapchain_handle> available_swapchains;
		size_t                           num_swapchains  = 0;
		uint32_t                         fallback_width  = 640;
		uint32_t                         fallback_height = 480;

		while ( !le_renderer_api_i->le_renderer_i.get_swapchains( self->renderer, &num_swapchains, available_swapchains.data() ) ) {
			available_swapchains.resize( num_swapchains );
		}

		if ( num_swapchains > 0 ) {
			self->fallback_src_image = le_renderer_api_i->le_renderer_i.get_swapchain_resource( self->renderer, available_swapchains.front() );
			le_renderer_api_i->le_renderer_i.get_swapchain_extent( self->renderer, available_swapchains.front(), &fallback_width, &fallback_height );
		}

		// if the number of images is not null, this means a request to create a new swapchain
		if ( p_img_settings ) {
			self->swapchain_settings = *p_img_settings;
		} else {
			self->swapchain_settings = get_default_swapchain_img_settings();
		}

		if ( self->swapchain_settings.width_hint == 0 ) {
			self->swapchain_settings.width_hint = fallback_width;
		}
		if ( self->swapchain_settings.height_hint == 0 ) {
			self->swapchain_settings.height_hint = fallback_height;
		};

		{

			// We scan the target directory for existing screenshots - if screenshots exist,
			// then we will try to find the one with the highest number matching our labelling scheme
			// and start our new screenshots at this offset.

			auto        target_path    = std::filesystem::path( self->swapchain_settings.image_filename_template ).remove_filename();
			std::string ext            = std::filesystem::path( self->swapchain_settings.image_filename_template ).extension();
			uint32_t    largest_number = 0;

			// iterate over all files in the target path -- this can get slow if there are lots of files in there.
			for ( auto const& f : std::filesystem::directory_iterator{ target_path } ) {
				if ( std::filesystem::is_regular_file( f ) && f.path().extension() == ext ) {

					logger.debug( "Found existing screenshot: %s", f.path().c_str() );

					uint32_t frame_number = 0;
					if ( 1 == sscanf( f.path().c_str(), self->swapchain_settings.image_filename_template, &frame_number ) ) {

						if ( frame_number >= largest_number ) {
							largest_number = frame_number + 1;
						}
					}
				}
			}

			self->swapchain_settings.frame_number_offset = largest_number;

			logger.info( "Starting Screenshot numbering at: %08d", self->swapchain_settings.frame_number_offset );
		}

		self->swapchain = le_renderer_api_i->le_renderer_i.add_swapchain( self->renderer, self->swapchain_settings );
	}

	if ( self->swapchain ) {

		le_image_resource_handle src_image_handle = src_image_;

		if ( src_image_handle == nullptr ) {
			src_image_handle = self->fallback_src_image;
		}

		le_image_resource_handle image_swapchain_image = le_renderer_api_i->le_renderer_i.get_swapchain_resource( self->renderer, self->swapchain );
		le_screenshot_blit_apply( self, rg, src_image_handle, image_swapchain_image );

		--( *num_images );

		return true;
	}

	return false;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_screenshot, api ) {
	auto& le_screenshot_i = static_cast<le_screenshot_api*>( api )->le_screenshot_i;

	le_screenshot_i.create  = le_screenshot_create;
	le_screenshot_i.destroy = le_screenshot_destroy;
	le_screenshot_i.record  = le_screenshot_record;
	le_screenshot_i.init    = le_screenshot_init;
}
