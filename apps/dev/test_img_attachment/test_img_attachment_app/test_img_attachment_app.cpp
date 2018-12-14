#include "test_img_attachment_app.h"

#include "pal_window/pal_window.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>

struct test_img_attachment_app_o {
	pal::Window  window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

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

static void reset_camera( test_img_attachment_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static test_img_attachment_app_o *test_img_attachment_app_create() {
	auto app = new ( test_img_attachment_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // TestImgAttachmentApp" );

	// create a new window
	app->window.setup( settings );

	auto info = le::RendererInfoBuilder()
	                .setWindow( app->window )
	                .withSwapchain()
	                .setWidthHint( 500 )
	                .setHeightHint( 200 )
	                .setImagecountHint( 2 )
	                .withKhrSwapchain()
	                .setPresentmode( le::Presentmode::eImmediate )
	                .end()
	                .end()
	                .build();

	app->renderer.setup( info );

	// Set up the camera
	reset_camera( app );

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( test_img_attachment_app_o *self ) {
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
	auto app = static_cast<test_img_attachment_app_o *>( user_data );

	// Attachment resource info may be further specialised using ImageInfoBuilder().
	// Attachment clear color, load and store op may be set via le_image_attachment_info_t.

	le_image_attachment_info_t imageAttachmentInfo;
	imageAttachmentInfo.clearValue.color = {1, 1, 0, 1};

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource(), imageAttachmentInfo ) // color attachment
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_img_attachment_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	// Data as it is laid out in the shader ubo
	struct MatrixStackUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene

	static auto shaderVert = app->renderer.createShaderModule( "./resources/shaders/default.vert", le::ShaderStage::eVertex );
	static auto shaderFrag = app->renderer.createShaderModule( "./resources/shaders/default.frag", le::ShaderStage::eFragment );

	static auto pipelineTestImgAttachment =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .build();

	MatrixStackUbo_t mvp;
	mvp.model      = glm::mat4( 1.f ); // identity matrix
	mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
	mvp.view       = app->camera.getViewMatrixGlm();
	mvp.projection = app->camera.getProjectionMatrixGlm();

	glm::vec3 test_img_attachmentPositions[] = {
	    {-50, -50, 0},
	    {50, -50, 0},
	    {0, 50, 0},
	};

	glm::vec4 test_img_attachmentColors[] = {
	    {1, 0, 0, 1.f},
	    {0, 1, 0, 1.f},
	    {0, 0, 1, 1.f},
	};

	encoder
	    .bindGraphicsPipeline( pipelineTestImgAttachment )
	    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
	    .setVertexData( test_img_attachmentPositions, sizeof( test_img_attachmentPositions ), 0 )
	    .setVertexData( test_img_attachmentColors, sizeof( test_img_attachmentColors ), 1 )
	    .draw( 3 );
}

// ----------------------------------------------------------------------

static bool test_img_attachment_app_update( test_img_attachment_app_o *self ) {

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

static void test_img_attachment_app_destroy( test_img_attachment_app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_img_attachment_app_api( void *api ) {
	auto  test_img_attachment_app_api_i = static_cast<test_img_attachment_app_api *>( api );
	auto &test_img_attachment_app_i     = test_img_attachment_app_api_i->test_img_attachment_app_i;

	test_img_attachment_app_i.initialize = initialize;
	test_img_attachment_app_i.terminate  = terminate;

	test_img_attachment_app_i.create  = test_img_attachment_app_create;
	test_img_attachment_app_i.destroy = test_img_attachment_app_destroy;
	test_img_attachment_app_i.update  = test_img_attachment_app_update;
}
