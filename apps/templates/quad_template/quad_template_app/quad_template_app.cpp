#include "quad_template_app.h"

#include "le_window/le_window.h"
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

struct quad_template_app_o {
	le::Window  window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCamera camera;
};

// ----------------------------------------------------------------------

static void initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	le::Window::terminate();
};

// ----------------------------------------------------------------------

static quad_template_app_o *quad_template_app_create() {
	auto app = new ( quad_template_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "IslΛnd // QuadTemplateApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	return app;
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<quad_template_app_o *>( user_data );

	// Attachment resource info may be further specialised using ImageInfoBuilder().
	// Attachment clear color, load and store op may be set via le_image_attachment_info_t.

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<quad_template_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	// Draw main scene

	static auto shaderVert = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.vert", le::ShaderStage::eVertex );
	static auto shaderFrag = app->renderer.createShaderModule( "./local_resources/shaders/fullscreen.frag", le::ShaderStage::eFragment );

	static auto pipelineQuadTemplate =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .build();

	encoder
	    .bindGraphicsPipeline( pipelineQuadTemplate )
	    .draw( 4 );
}

// ----------------------------------------------------------------------

static bool quad_template_app_update( quad_template_app_o *self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

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

static void quad_template_app_destroy( quad_template_app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( quad_template_app, api ){

	auto  quad_template_app_api_i = static_cast<quad_template_app_api *>( api );
	auto &quad_template_app_i     = quad_template_app_api_i->quad_template_app_i;

	quad_template_app_i.initialize = initialize;
	quad_template_app_i.terminate  = terminate;

	quad_template_app_i.create  = quad_template_app_create;
	quad_template_app_i.destroy = quad_template_app_destroy;
	quad_template_app_i.update  = quad_template_app_update;
}
