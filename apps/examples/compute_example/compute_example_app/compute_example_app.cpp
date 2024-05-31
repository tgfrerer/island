#include "compute_example_app.h"

#include "le_window.h"
#include "le_renderer.hpp"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#include "le_camera.h"
#include "le_pipeline_builder.h"

#include "le_mesh_generator.h"
#include "le_mesh.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "le_ui_event.h"

constexpr size_t cNumDataElements = 64;

struct GpuMeshData {
	le_buffer_resource_handle vertex_handle;
	le_buffer_resource_handle index_handle;
	uint32_t               vertex_num_bytes;
	uint32_t               index_num_bytes;
};

struct compute_example_app_o {
	le::Window   window;
	le::Renderer renderer;

	uint64_t frame_counter = 0;
	uint32_t anim_frame    = 0;
	int32_t  anim_speed    = 1;

	GpuMeshData* gpu_mesh     = nullptr; // owning
	bool         meshUploaded = false;

	LeCamera           camera;
	LeCameraController cameraController;
};

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void reset_camera( compute_example_app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

static compute_example_app_o* compute_example_app_create() {
	auto app = new ( compute_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // ComputeExampleApp" );

	// Create a new window
	app->window.setup( settings );

	// Set up the renderer
	app->renderer.setup(
	    le::RendererInfoBuilder( app->window )
	        .build() );

	// Set up the camera
	reset_camera( app );

	app->gpu_mesh = new GpuMeshData{
	    LE_BUF_RESOURCE( "vertex_buffer" ),
	    LE_BUF_RESOURCE( "index_buffer" ),
	    ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ) * sizeof( glm::vec4 ),    // vertex_num_bytes
	    ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ) * 6 * sizeof( uint16_t ), // indices_num_bytes
	};

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( compute_example_app_o* self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( { 0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix =
	    { { 0.930103, -0.093034, -0.355320, -0.000000 },
	      { -0.007937, 0.962072, -0.272678, 0.000000 },
	      { 0.367212, 0.256439, 0.894089, -0.000000 },
	      { 25.002544, -99.994820, -616.479797, 1.000000 } };
	self->camera.setViewMatrix( ( float* )( &camMatrix ) );
}

// ----------------------------------------------------------------------

static bool pass_initialise_setup( le_renderpass_o* pRp, void* user_data ) {

	auto app = static_cast<compute_example_app_o*>( user_data );

	// --------| invariant: particle buffer handle exists

	le::RenderPass rp( pRp );
	rp
	    .useBufferResource( app->gpu_mesh->vertex_handle, le::AccessFlagBits2::eTransferWrite ) //
	    .useBufferResource( app->gpu_mesh->index_handle, le::AccessFlagBits2::eTransferWrite )  //
	    ;

	if ( app->meshUploaded ) {
		return false;
	} else {
		app->meshUploaded = true;
		return true;
	}
}

// ----------------------------------------------------------------------

static void pass_initialise_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {

	auto                app = static_cast<compute_example_app_o*>( user_data );
	le::TransferEncoder encoder( encoder_ );

	LeMesh mesh;

	LeMeshGenerator::generatePlane( mesh, 1024, 1024, cNumDataElements, cNumDataElements );

	{
		// This is really annoying - we must use vec4 instead of vec3 for vertex position
		// as ssbo alignment only allows us vec4 - we can't have that packed tightly

		size_t num_vertices          = mesh.getVertexCount();
		size_t num_vertex_data_bytes = num_vertices * sizeof( glm::vec4 );

		void* gpu_memory = nullptr;
		if ( encoder.mapBufferMemory( app->gpu_mesh->vertex_handle, 0, num_vertex_data_bytes, &gpu_memory ) ) {

			mesh.readAttributeDataInto( gpu_memory, num_vertex_data_bytes, le_mesh_api::ePosition, nullptr, nullptr, 0, sizeof( glm::vec4 ) );
		}
	}

	{
		void*    gpu_memory          = nullptr;
		uint32_t num_bytes_per_index = 2;
		size_t   num_indices         = mesh.getIndexCount( &num_bytes_per_index );
		size_t   num_bytes_to_write  = num_indices * num_bytes_per_index;
		if ( encoder.mapBufferMemory( app->gpu_mesh->index_handle, 0, num_bytes_to_write, &gpu_memory ) ) {
			mesh.readIndexDataInto( gpu_memory, num_bytes_to_write, &num_bytes_per_index, &num_indices, 0 );
		}
	}
}

// ----------------------------------------------------------------------

static void pass_compute_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto               app = static_cast<compute_example_app_o*>( user_data );
	le::ComputeEncoder encoder{ encoder_ };

	// Compute pipelines are delightfully simple to set up - they only need to
	// know about their one shader stage.

	le_pipeline_manager_o* pipeline_manager;

	static auto psoCompute =
	    LeComputePipelineBuilder( encoder.getPipelineManager() )
	        .setShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eCompute )
	                .setSourceFilePath( "./local_resources/shaders/compute.glsl" )
	                .build() )
	        .build();

	// The only uniform we want to upload to the shader is the current time tick value, so we
	// don't really need to set up a separate struct for our uniforms.
	float t_val = ( app->anim_frame % ( 240 * 10 ) ) / 240.f;

	encoder
	    .bindComputePipeline( psoCompute )
	    .bindArgumentBuffer( LE_ARGUMENT_NAME( "ParticleBuf" ), app->gpu_mesh->vertex_handle )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniforms" ), &t_val, sizeof( float ) )
	    .dispatch( ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ), 1, 1 );
}

// ----------------------------------------------------------------------

static bool pass_draw_setup( le_renderpass_o* pRp, void* user_data ) {
	auto rp  = le::RenderPass{ pRp };
	auto app = static_cast<compute_example_app_o*>( user_data );

	auto attachment_info =
	    le::ImageAttachmentInfoBuilder()
	        .setColorClearValue( { le::ClearColorValue{ { 0, 0, 0, 255 } } } )
	        .setLoadOp( le::AttachmentLoadOp::eClear )
	        .build();
	rp
	    .addColorAttachment( app->renderer.getSwapchainResource(), attachment_info ) // color attachment
	    .useBufferResource( app->gpu_mesh->vertex_handle )
	    .useBufferResource( app->gpu_mesh->index_handle, le::AccessFlagBits2::eIndexRead ) //
	    ;

	return true;
}

// ----------------------------------------------------------------------

static void pass_draw_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto                app = static_cast<compute_example_app_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	le::Extent2D extents;
	encoder.getRenderpassExtent( &extents );

	le::Viewport viewports[ 1 ] = {
	    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	// Data as it is laid out in the shader ubo
	struct MvpUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene

	static auto psoDefaultGraphics =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./local_resources/shaders/default.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./local_resources/shaders/default.frag" )
	                .build() )
	        .withInputAssemblyState()
	        .setTopology( le::PrimitiveTopology::eTriangleList )
	        .end()
	        .withRasterizationState()
	        .setPolygonMode( le::PolygonMode::eLine )
	        .end()
	        .build();

	MvpUbo_t mvp;
	mvp.model = glm::mat4( 1.f ); // identity matrix
	app->camera.getViewMatrix( ( float* )( &mvp.view ) );
	app->camera.getProjectionMatrix( ( float* )( &mvp.projection ) );

	uint64_t bufferOffsets[ 1 ] = { 0 };

	encoder
	    .setLineWidth( 1 )
	    .bindGraphicsPipeline( psoDefaultGraphics )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo_t ) )
	    .bindVertexBuffers( 0, 1, &app->gpu_mesh->vertex_handle, bufferOffsets )
	    .bindIndexBuffer( app->gpu_mesh->index_handle, 0 )
	    .drawIndexed( 6 * ( cNumDataElements + 1 ) * ( cNumDataElements + 1 ) );
}

// ----------------------------------------------------------------------

static void compute_example_app_process_ui_events( compute_example_app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;

	window_i.get_ui_event_queue( self->window, &pEvents, &numEvents );

	auto const events_end = pEvents + numEvents;

	bool wantsToggle = false;

	for ( auto pEv = pEvents; pEv != events_end; pEv++ ) {
		auto& event = *pEv;
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					reset_camera( self );
					glm::mat4x4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					glm::mat4x4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eP ) {
					// print out current camera view matrix
					glm::mat4x4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					std::cout << "View matrix:" << glm::to_string( view_matrix ) << std::endl
					          << std::flush;
					std::cout << "camera node matrix:" << glm::to_string( glm::inverse( view_matrix ) ) << std::endl
					          << std::flush;
				} else if ( e.key == LeUiEvent::NamedKey::eA ) {
					if ( self->anim_speed != 0 ) {
						self->anim_speed = 0;
					} else {
						self->anim_speed = 1;
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

	self->cameraController.processEvents( self->camera, pEvents, numEvents );

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool compute_example_app_update( compute_example_app_o* self ) {

	// Polls events for all windows
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	compute_example_app_process_ui_events( self );

	le::RenderGraph renderGraph{};
	{
		// This pass will typically only get executed once - it will upload
		// buffers .

		auto passInitialise =
		    le::RenderPass( "initialise", le::QueueFlagBits::eTransfer )
		        .setSetupCallback( self, pass_initialise_setup )
		        .setExecuteCallback( self, pass_initialise_exec );
		auto passCompute =
		    le::RenderPass( "compute", le::QueueFlagBits::eCompute )
		        .useBufferResource( self->gpu_mesh->vertex_handle, le::AccessFlagBits2::eShaderStorageRead, le::AccessFlagBits2::eShaderStorageWrite )
		        .setExecuteCallback( self, pass_compute_exec );
		auto passDraw =
		    le::RenderPass( "draw", le::QueueFlagBits::eGraphics )
		        .setSetupCallback( self, pass_draw_setup )
		        .setExecuteCallback( self, pass_draw_exec )
		        .setSampleCount( le::SampleCountFlagBits::e4 );

		renderGraph
		    .addRenderPass( passInitialise )
		    .addRenderPass( passCompute )
		    .addRenderPass( passDraw )
		    .declareResource(
		        self->gpu_mesh->vertex_handle,
		        le::BufferInfoBuilder()
		            .setSize( self->gpu_mesh->vertex_num_bytes )
		            .addUsageFlags( le::BufferUsageFlagBits::eVertexBuffer | le::BufferUsageFlagBits::eStorageBuffer | le::BufferUsageFlagBits::eTransferDst )
		            .build() )
		    .declareResource(
		        self->gpu_mesh->index_handle,
		        le::BufferInfoBuilder()
		            .setSize( self->gpu_mesh->index_num_bytes )
		            .addUsageFlags( le::BufferUsageFlagBits::eIndexBuffer | le::BufferUsageFlagBits::eTransferDst )
		            .build() ) //
		    ;
	}

	self->renderer.update( renderGraph );

	self->frame_counter++;
	self->anim_frame += self->anim_speed;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void compute_example_app_destroy( compute_example_app_o* self ) {
	if ( self ) {
		delete self->gpu_mesh;
	}
	delete ( self );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( compute_example_app, api ) {
	auto  compute_example_app_api_i = static_cast<compute_example_app_api*>( api );
	auto& compute_example_app_i     = compute_example_app_api_i->compute_example_app_i;

	compute_example_app_i.initialize = app_initialize;
	compute_example_app_i.terminate  = app_terminate;

	compute_example_app_i.create  = compute_example_app_create;
	compute_example_app_i.destroy = compute_example_app_destroy;
	compute_example_app_i.update  = compute_example_app_update;
}
