#include "test_compute_app.h"

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

struct BufferData {
	le_resource_handle_t handle;
	uint32_t             numBytes;
};

struct test_compute_app_o {
	pal::Window  window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	BufferData *particleBuffer = nullptr; // owning

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

static void reset_camera( test_compute_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static test_compute_app_o *test_compute_app_create() {
	auto app = new ( test_compute_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // TestComputeApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window )
	                         .withSwapchain()
	                         .withKhrSwapchain()
	                         .setPresentmode( le::Presentmode::eFifo )
	                         .end()
	                         .end()
	                         .build() );

	// Set up the camera
	reset_camera( app );

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( test_compute_app_o *self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( {0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrixGlm( camMatrix );
}

// ----------------------------------------------------------------------

static bool pass_compute_setup( le_renderpass_o *pRp, void *user_data ) {
	auto           app = static_cast<test_compute_app_o *>( user_data );
	le::RenderPass rp( pRp );
	rp
	    .useResource( app->particleBuffer->handle,
	                  le::BufferInfoBuilder()
	                      .setSize( app->particleBuffer->numBytes )
	                      .addUsageFlags( LE_BUFFER_USAGE_STORAGE_BUFFER_BIT )
	                      .build() );

	return true;
};

// ----------------------------------------------------------------------

static void pass_compute_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_compute_app_o *>( user_data );
	le::Encoder encoder{encoder_};

	// Draw main scene

	static auto shaderCompute = app->renderer.createShaderModule( "./local_resources/shaders/compute.glsl", le::ShaderStage::eCompute );

	static auto psoCompute =
	    LeComputePipelineBuilder( encoder.getPipelineManager() )
	        .setShaderStage( shaderCompute )
	        .build();

	encoder
	    .bindComputePipeline( psoCompute )
	    .bindArgumentBuffer( LE_ARGUMENT_NAME( "ParticleBuf" ), app->particleBuffer->handle )
	    .dispatch( 16, 16, 4 );
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<test_compute_app_o *>( user_data );

	// Attachment resource info may be further specialised using ImageInfoBuilder().
	// Attachment clear color, load and store op may be set via le_image_attachment_info_t.

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .useResource( app->particleBuffer->handle,
	                  le::BufferInfoBuilder()
	                      .addUsageFlags( LE_BUFFER_USAGE_VERTEX_BUFFER_BIT )
	                      .setSize( app->particleBuffer->numBytes )
	                      .build() );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<test_compute_app_o *>( user_data );
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

	static auto shaderVert = app->renderer.createShaderModule( "./local_resources/shaders/default.vert", le::ShaderStage::eVertex );
	static auto shaderFrag = app->renderer.createShaderModule( "./local_resources/shaders/default.frag", le::ShaderStage::eFragment );

	static auto psoDefaultGraphics =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .build();

	MatrixStackUbo_t mvp;
	mvp.model      = glm::mat4( 1.f ); // identity matrix
	mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
	mvp.view       = app->camera.getViewMatrixGlm();
	mvp.projection = app->camera.getProjectionMatrixGlm();

	glm::vec3 trianglePositions[] = {
	    {-150, -50, 0},
	    {50, -50, 0},
	    {0, 50, 0},
	};

	glm::vec4 triangleColors[] = {
	    {1, 0, 0, 1.f},
	    {0, 1, 0, 1.f},
	    {0, 0, 1, 1.f},
	};

	uint64_t bufferOffsets[ 1 ] = {0};

	encoder
	    .bindGraphicsPipeline( psoDefaultGraphics )
	    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
	    //	    .setVertexData( trianglePositions, sizeof( trianglePositions ), 0 )
	    .bindVertexBuffers( 0, 1, &app->particleBuffer->handle, bufferOffsets )
	    .setVertexData( triangleColors, sizeof( triangleColors ), 1 )
	    .draw( 3 );
}

// ----------------------------------------------------------------------

static bool test_compute_app_update( test_compute_app_o *self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	le::RenderModule mainModule{};
	{
		le::RenderPass passInitial( "initial", LE_RENDER_PASS_TYPE_TRANSFER );

		passInitial
		    .setSetupCallback( self, []( le_renderpass_o *pRp, void *user_data ) -> bool {
			    auto app = static_cast<test_compute_app_o *>( user_data );

				if ( app->particleBuffer ) {

					// We don't have to do anything with this pass if the particle buffer already exists.
					// returning false means that this pass will not be added to the frame graph.

					return false;
				} else {

					// ---------| invariant: particle buffer has not been created yet.
					app->particleBuffer = new BufferData{LE_BUF_RESOURCE( "particle_buffer" ), 1024 * 8 * sizeof( float )};
				}

				// --------| invariant: particle buffer handle exists

				le::RenderPass rp( pRp );
				rp
				    .useResource( app->particleBuffer->handle,
				                  le::BufferInfoBuilder()
				                      .setSize( app->particleBuffer->numBytes )
				                      .addUsageFlags( LE_BUFFER_USAGE_TRANSFER_DST_BIT )
				                      .build() ) //
				    ;
				return true;
		    } )

		    .setExecuteCallback( self, []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
			    auto        app = static_cast<test_compute_app_o *>( user_data );
				le::Encoder encoder( encoder_ );

				// todo: add some initial data which we want to upload to the particle buffer.
				glm::vec3 test_computePositions[] = {
				    {-50, -50, 0},
				    {50, -50, 0},
				    {0, 50, 0},
				};

				encoder.writeToBuffer( app->particleBuffer->handle, 0, test_computePositions, sizeof( glm::vec3 ) * 3 );
		    } );

		le::RenderPass passCompute( "compute", LE_RENDER_PASS_TYPE_COMPUTE );

		passCompute
		    .setSetupCallback( self, pass_compute_setup )
		    .setExecuteCallback( self, pass_compute_exec ) //
		    ;

		le::RenderPass passMain( "root", LE_RENDER_PASS_TYPE_DRAW );

		passMain
		    .setSetupCallback( self, pass_main_setup )
		    .setExecuteCallback( self, pass_main_exec )
		    .setIsRoot( true ) //
		    ;

		mainModule.addRenderPass( passInitial );
		mainModule.addRenderPass( passCompute );
		mainModule.addRenderPass( passMain );
	}

	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_compute_app_destroy( test_compute_app_o *self ) {
	if ( self->particleBuffer ) {
		delete self->particleBuffer;
	}
	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_compute_app_api( void *api ) {
	auto  test_compute_app_api_i = static_cast<test_compute_app_api *>( api );
	auto &test_compute_app_i     = test_compute_app_api_i->test_compute_app_i;

	test_compute_app_i.initialize = initialize;
	test_compute_app_i.terminate  = terminate;

	test_compute_app_i.create  = test_compute_app_create;
	test_compute_app_i.destroy = test_compute_app_destroy;
	test_compute_app_i.update  = test_compute_app_update;
}
