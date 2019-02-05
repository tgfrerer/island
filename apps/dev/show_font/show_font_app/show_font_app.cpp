#include "show_font_app.h"

#include "pal_window/pal_window.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include "le_font/le_font.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct show_font_app_o {
	pal::Window  window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	le_glyph_shape_o *glyph_shape = nullptr;
	le_font_o *       font;

	LeCamera camera;
};

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( show_font_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static show_font_app_o *show_font_app_create() {
	auto app = new ( show_font_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // ShowFontApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	// Set up the camera
	reset_camera( app );

	using namespace le_font;
	app->font = le_font_i.create();

	app->glyph_shape = le_font_i.get_shape_for_glyph( app->font, '&', nullptr );

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( show_font_app_o *self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( {0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrixGlm( camMatrix );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o *pRp, void *user_data );

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<show_font_app_o *>( user_data );

	// Attachment resource info may be further specialised using ImageInfoBuilder().
	// Attachment clear color, load and store op may be set via le_image_attachment_info_t.

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<show_font_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	auto extents = encoder.getRenderpassExtent();

	le::Viewport viewports[ 1 ] = {
	    {0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f},
	};

	app->camera.setViewport( viewports[ 0 ] );

	// Data as it is laid out in the shader ubo
	struct MatrixStackUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene

	static auto shaderVert = app->renderer.createShaderModule( "./resources/shaders/default.vert", le::ShaderStage::eVertex );
	static auto shaderFrag = app->renderer.createShaderModule( "./resources/shaders/default.frag", le::ShaderStage::eFragment );

	static auto pipelineShowFont =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .withInputAssemblyState()
	        .setToplogy( le::PrimitiveTopology::eLineStrip )
	        .end()
	        .build();

	MatrixStackUbo_t mvp;

	mvp.model      = glm::mat4( 1.f ); // identity matrix
	mvp.model      = glm::scale( mvp.model, glm::vec3( 1.0 ) );
	mvp.model      = glm::translate( mvp.model, glm::vec3( -200, 300, 0 ) );
	mvp.view       = app->camera.getViewMatrixGlm();
	mvp.projection = app->camera.getProjectionMatrixGlm();

	encoder
	    .setLineWidth( 1.f )
	    .bindGraphicsPipeline( pipelineShowFont )
	    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) ) //
	    ;

	{
		using namespace le_font;

		size_t numContours = le_glyph_shape_i.get_num_contours( app->glyph_shape );

		for ( size_t i = 0; i != numContours; i++ ) {
			size_t                 numV = 0;
			glm::vec2 *            vv   = le_glyph_shape_i.get_vertices_for_shape_contour( app->glyph_shape, i, &numV );
			std::vector<glm::vec3> vertices;
			std::vector<glm::vec4> colors;

			vertices.reserve( numV );
			colors.reserve( numV );

			for ( auto p = vv; p != vv + numV; p++ ) {
				vertices.emplace_back( p->x, -p->y, 0 );
				colors.emplace_back( 1, 1, 1, 1 );
			}

			encoder
			    .setVertexData( vertices.data(), sizeof( glm::vec3 ) * numV, 0 )
			    .setVertexData( colors.data(), sizeof( glm::vec4 ) * numV, 1 )
			    .draw( numV );
		}
	}
}

// ----------------------------------------------------------------------

static bool show_font_app_update( show_font_app_o *self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	le::RenderModule mainModule{};
	{

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );

		renderPassFinal
		    .setSetupCallback( self, pass_main_setup )
		    .setExecuteCallback( self, pass_main_exec ) //
		    ;

		mainModule.addRenderPass( renderPassFinal );
	}

	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void show_font_app_destroy( show_font_app_o *self ) {

	using namespace le_font;
	le_glyph_shape_i.destroy( self->glyph_shape );
	self->glyph_shape = nullptr;

	le_font_i.destroy( self->font );
	self->font = nullptr;

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_show_font_app_api( void *api ) {
	auto  show_font_app_api_i = static_cast<show_font_app_api *>( api );
	auto &show_font_app_i     = show_font_app_api_i->show_font_app_i;

	show_font_app_i.initialize = initialize;
	show_font_app_i.terminate  = terminate;

	show_font_app_i.create  = show_font_app_create;
	show_font_app_i.destroy = show_font_app_destroy;
	show_font_app_i.update  = show_font_app_update;
}
