#include "test_dependency.h"

#include "pal_window/pal_window.h"
#include "le_renderer/le_renderer.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <iostream>
#include <iomanip>

struct test_dependency_o {
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

static void reset_camera( test_dependency_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static test_dependency_o *test_dependency_create() {
	auto app = new ( test_dependency_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window )
	                         .withSwapchain()
	                         .withKhrSwapchain()
	                         .setPresentmode( le::Presentmode::eFifo )
	                         .end()
	                         .end()
	                         .build() );

	// -- Declare graphics pipeline state objects

	{
		// set up the camera
		reset_camera( app );
	}

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( test_dependency_o *self ) {
	auto swapchainExtent = self->renderer.getSwapchainExtent();
	self->camera.setViewport( {0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

static void pass_one_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	std::cout << "one exec" << std::endl
	          << std::flush;
}

static void pass_two_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	std::cout << "two exec" << std::endl
	          << std::flush;
}

static void pass_three_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	std::cout << "three exec" << std::endl
	          << std::flush;
}

static void pass_comp_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	std::cout << "comp exec" << std::endl
	          << std::flush;
}

static void pass_transf_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	std::cout << "transf exec" << std::endl
	          << std::flush;
}
// ----------------------------------------------------------------------

static bool test_dependency_update( test_dependency_o *self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	std::cout << "Frame update: " << std::dec << self->frame_counter << std::endl
	          << std::flush;

	le::RenderModule mainModule{};
	{

		le_image_attachment_info_t defaultClearInfo{};
		defaultClearInfo.clearValue.color = {{{1.f, 0.f, 0.f, 1.f}}};

		mainModule
		    .addRenderPass( le::RenderPass( "transf_one", LE_RENDER_PASS_TYPE_TRANSFER )
		                        .useResource( LE_BUF_RESOURCE( "particle_buffer" ), le::BufferInfoBuilder().setSize( 1024 ).setUsageFlags( LE_BUFFER_USAGE_TRANSFER_DST_BIT ).build() )
		                        .setExecuteCallback( self, pass_transf_exec ) )
		    .addRenderPass( le::RenderPass( "comp_one", LE_RENDER_PASS_TYPE_COMPUTE )
		                        .useResource( LE_BUF_RESOURCE( "particle_buffer" ), le::BufferInfoBuilder().setSize( 1024 ).setUsageFlags( LE_BUFFER_USAGE_STORAGE_BUFFER_BIT ).build() )
		                        .setExecuteCallback( self, pass_comp_exec ) )
		    .addRenderPass( le::RenderPass( "one", LE_RENDER_PASS_TYPE_DRAW )
		                        .useResource( LE_BUF_RESOURCE( "particle_buffer" ), le::BufferInfoBuilder().setSize( 1024 ).setUsageFlags( LE_BUFFER_USAGE_VERTEX_BUFFER_BIT ).build() )
		                        .addDepthStencilAttachment( LE_IMG_RESOURCE( "one_depth" ) )                                                                                                  // depth attachment
		                        .addColorAttachment( LE_IMG_RESOURCE( "one_output" ), defaultClearInfo, le::ImageInfoBuilder().setFormat( le::Format::eR32G32B32A32Sfloat ).build() )         // color attachment 0
		                        .sampleTexture( LE_TEX_RESOURCE( "dummy_texture" ), le::TextureInfoBuilder().withImageViewInfo().setImage( LE_IMG_RESOURCE( "dummy_image" ) ).end().build() ) //
		                        .setExecuteCallback( self, pass_one_exec )                                                                                                                    //
		                    )

		    .addRenderPass( le::RenderPass( "two", LE_RENDER_PASS_TYPE_DRAW )
		                        .addColorAttachment( LE_IMG_RESOURCE( "two_output" ), defaultClearInfo )                                                                                     // color attachment 0
		                        .sampleTexture( LE_TEX_RESOURCE( "dummy_texture" ), le::TextureInfoBuilder().withImageViewInfo().setImage( LE_IMG_RESOURCE( "one_output" ) ).end().build() ) //
		                        .addDepthStencilAttachment( LE_IMG_RESOURCE( "depthStencil" ) )                                                                                              //
		                        .setExecuteCallback( self, pass_two_exec )                                                                                                                   //
		                    )

		    .addRenderPass( le::RenderPass( "three", LE_RENDER_PASS_TYPE_DRAW )
		                        .addColorAttachment( self->renderer.getSwapchainResource(), defaultClearInfo )                                                                                   // color attachment
		                        .sampleTexture( LE_TEX_RESOURCE( "dummy_texture_two" ), le::TextureInfoBuilder().withImageViewInfo().setImage( LE_IMG_RESOURCE( "two_output" ) ).end().build() ) //
		                        .setExecuteCallback( self, pass_three_exec )                                                                                                                     //
		                        .setIsRoot( true )                                                                                                                                               //
		                    )
		    //
		    ;
	}

	// Update will first call setup callbacks, then render callbacks in this module.
	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_dependency_destroy( test_dependency_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_dependency_api( void *api ) {
	auto  test_dependency_api_i = static_cast<test_dependency_api *>( api );
	auto &test_dependency_i     = test_dependency_api_i->test_dependency_i;

	test_dependency_i.initialize = initialize;
	test_dependency_i.terminate  = terminate;

	test_dependency_i.create  = test_dependency_create;
	test_dependency_i.destroy = test_dependency_destroy;
	test_dependency_i.update  = test_dependency_update;
}
