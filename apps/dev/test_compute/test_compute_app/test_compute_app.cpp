#include "test_compute_app.h"

#include "pal_window/pal_window.h"
#include "le_renderer/le_renderer.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"

#include "le_mesh_generator/le_mesh_generator.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "le_ui_event/le_ui_event.h"

constexpr size_t cNumDataElements = 32;

struct MeshData {
	le_resource_handle_t vertex_handle;
	le_resource_handle_t index_handle;
	uint32_t             vertex_num_bytes;
	uint32_t             index_num_bytes;
};

struct test_compute_app_o {
	pal::Window  window;
	le::Renderer renderer;

	uint64_t frame_counter = 0;
	uint32_t anim_frame    = 0;
	int32_t  anim_speed    = 1;

	MeshData *mesh = nullptr; // owning

	LeCamera           camera;
	LeCameraController cameraController;
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
	                                                    //	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	glm::mat4 camMatrix =
	    {{0.937339, -0.235563, -0.256721, -0.000000}, {-0.000000, 0.736816, -0.676093, 0.000000}, {0.348419, 0.633728, 0.690647, -0.000000}, {-79.101540, -152.343918, -1253.020996, 1.000000}}

	;
	self->camera.setViewMatrixGlm( camMatrix );
}

// ----------------------------------------------------------------------

static bool pass_initial_setup( le_renderpass_o *pRp, void *user_data ) {

	auto app = static_cast<test_compute_app_o *>( user_data );

	if ( app->mesh ) {

		// We don't have to do anything with this pass if the particle buffer already exists.
		// returning false means that this pass will not be added to the frame graph.

		return false;
	} else {

		// ---------| invariant: particle buffer has not been created yet.
		app->mesh = new MeshData{
		    LE_BUF_RESOURCE( "vertex_buffer" ),
		    LE_BUF_RESOURCE( "index_buffer" ),
		    ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ) * sizeof( glm::vec4 ),    // vertex_num_bytes
		    ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ) * 6 * sizeof( uint16_t ), // indices_num_bytes
	    };
	}

	// --------| invariant: particle buffer handle exists

	le::RenderPass rp( pRp );
	rp
	    .useResource( app->mesh->vertex_handle,
	                  le::BufferInfoBuilder()
	                      .setSize( app->mesh->vertex_num_bytes )
	                      .addUsageFlags( LE_BUFFER_USAGE_TRANSFER_DST_BIT )
	                      .build() ) //
	    .useResource( app->mesh->index_handle,
	                  le::BufferInfoBuilder()
	                      .setSize( app->mesh->index_num_bytes )
	                      .addUsageFlags( LE_BUFFER_USAGE_TRANSFER_DST_BIT )
	                      .build() ) //
	    ;
	return true;
}

// ----------------------------------------------------------------------

static void pass_initial_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {

	auto        app = static_cast<test_compute_app_o *>( user_data );
	le::Encoder encoder( encoder_ );

	using namespace le_mesh_generator;
	le_mesh_generator_o *meshGenerator = le_mesh_generator_i.create();

	le_mesh_generator_i.generate_plane( meshGenerator, 1024, 1024, cNumDataElements, cNumDataElements );

	{
		// This is really annoying - we must use vec4 instead of vec3 for vertex position
		// as ssbo alignment only allows us vec4 - we can't have that packed tightly

		float *vertData = nullptr;
		size_t numVerts = 0;

		std::vector<float> tmp_vertices;
		tmp_vertices.reserve( ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ) );

		le_mesh_generator_i.get_vertices( meshGenerator, numVerts, &vertData );
		float *      v     = vertData;
		float *const end_v = vertData + ( numVerts * 3 );

		for ( ; v != end_v; ) {
			tmp_vertices.emplace_back( *v++ );
			tmp_vertices.emplace_back( *v++ );
			tmp_vertices.emplace_back( *v++ );
			tmp_vertices.emplace_back( 1 );
		}

		encoder.writeToBuffer( app->mesh->vertex_handle, 0, tmp_vertices.data(), tmp_vertices.size() * sizeof( float ) );
	}
	{
		uint16_t *indexData  = nullptr;
		size_t    numIndices = 0;

		le_mesh_generator_i.get_indices( meshGenerator, numIndices, &indexData );
		encoder.writeToBuffer( app->mesh->index_handle, 0, indexData, numIndices * sizeof( uint16_t ) );
	}

	le_mesh_generator_i.destroy( meshGenerator );
}

// ----------------------------------------------------------------------

static bool pass_compute_setup( le_renderpass_o *pRp, void *user_data ) {
	auto           app = static_cast<test_compute_app_o *>( user_data );
	le::RenderPass rp( pRp );
	rp
	    .useResource( app->mesh->vertex_handle,
	                  le::BufferInfoBuilder()
	                      .setSize( app->mesh->vertex_num_bytes )
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

	float t_val = ( app->anim_frame % ( 240 * 10 ) ) / 240.f;

	encoder
	    .bindComputePipeline( psoCompute )
	    .bindArgumentBuffer( LE_ARGUMENT_NAME( "ParticleBuf" ), app->mesh->vertex_handle )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniforms" ), &t_val, sizeof( float ) )
	    .dispatch( ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ), 1, 1 );
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o *pRp, void *user_data ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<test_compute_app_o *>( user_data );

	// Attachment resource info may be further specialised using ImageInfoBuilder().
	// Attachment clear color, load and store op may be set via le_image_attachment_info_t.

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .useResource( app->mesh->vertex_handle,
	                  le::BufferInfoBuilder()
	                      .addUsageFlags( LE_BUFFER_USAGE_VERTEX_BUFFER_BIT )
	                      .setSize( app->mesh->vertex_num_bytes )
	                      .build() )
	    .useResource( app->mesh->index_handle,
	                  le::BufferInfoBuilder()
	                      .addUsageFlags( LE_BUFFER_USAGE_INDEX_BUFFER_BIT )
	                      .setSize( app->mesh->index_num_bytes )
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
	        .withInputAssemblyState()
	        .setToplogy( le::PrimitiveTopology::eTriangleList )
	        .end()
	        .withRasterizationState()
	        .setPolygonMode( le::PolygonMode::eLine )
	        .end()
	        .build();

	MatrixStackUbo_t mvp;
	mvp.model      = glm::mat4( 1.f ); // identity matrix
	mvp.view       = app->camera.getViewMatrixGlm();
	mvp.projection = app->camera.getProjectionMatrixGlm();

	uint64_t bufferOffsets[ 1 ] = {0};

	encoder
	    .setLineWidth( 1 )
	    .bindGraphicsPipeline( psoDefaultGraphics )
	    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &mvp, sizeof( MatrixStackUbo_t ) )
	    .bindVertexBuffers( 0, 1, &app->mesh->vertex_handle, bufferOffsets )
	    .bindIndexBuffer( app->mesh->index_handle, 0 )
	    .drawIndexed( 6 * ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ) );
}

// ----------------------------------------------------------------------
static void test_compute_app_process_ui_events( test_compute_app_o *self ) {
	using namespace pal_window;
	uint32_t         numEvents;
	LeUiEvent const *pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, numEvents );

	std::vector<LeUiEvent> events{pEvents, pEvents + numEvents};

	bool wantsToggle = false;

	for ( auto &event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto &e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					reset_camera( self );
					float distance_to_origin = glm::distance( glm::vec4{0, 0, 0, 1}, glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					float distance_to_origin = glm::distance( glm::vec4{0, 0, 0, 1}, glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eP ) {
					// print out current camera view matrix
					std::cout << "View matrix:" << glm::to_string( self->camera.getViewMatrixGlm() ) << std::endl
					          << std::flush;
					std::cout << "camera node matrix:" << glm::to_string( glm::inverse( self->camera.getViewMatrixGlm() ) ) << std::endl
					          << std::flush;
				} else if ( e.key == LeUiEvent::NamedKey::eA ) {
					if ( self->anim_speed != 0 ) {
						self->anim_speed = 0;
					}
				} else if ( e.key == LeUiEvent::NamedKey::ePageUp ) {
					self->anim_speed++;
				} else if ( e.key == LeUiEvent::NamedKey::ePageDown ) {
					self->anim_speed--;
				}

			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
		    break;
		}
	}

	auto swapchainExtent = self->renderer.getSwapchainExtent();
	self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );

	self->cameraController.processEvents( self->camera, events.data(), events.size() );

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool test_compute_app_update( test_compute_app_o *self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	test_compute_app_process_ui_events( self );

	le::RenderModule mainModule{};
	{
		le::RenderPass passInitial( "initial", LE_RENDER_PASS_TYPE_TRANSFER );

		passInitial
		    .setSetupCallback( self, pass_initial_setup )
		    .setExecuteCallback( self, pass_initial_exec );

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
	self->anim_frame += self->anim_speed;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_compute_app_destroy( test_compute_app_o *self ) {
	if ( self->mesh ) {
		delete self->mesh;
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
