#include "compute_example_app.h"

#include "le_window.h"
#include "le_renderer.hpp"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/string_cast.hpp"

#include "le_camera.h"
#include "le_pipeline_builder.h"

#include "le_mesh_generator.h"
#include "le_mesh.h"
#include "le_rendergraph_visualizer.h"
#include "le_ui_event.h"

#include <chrono>
#include "le_timebase.h"
#include "private/le_timebase/le_timebase_ticks_type.h"
#include "le_backend_vk.h"

#include <iostream>
// #include <memory>
// #include <sstream>
#include <vector>

constexpr size_t cNumDataElements = 64;
constexpr size_t C_SHOULD_USE_FIXED_FPS = false;
constexpr size_t FIXED_FPS              = 60; // set to a fixed fps frame rate if you render out image sequences, for example

struct compute_example_app_o {
	le::Window   window;
	le::Renderer renderer;

	uint64_t frame_counter = 0;
	float    anim_t        = 0;
	int32_t  anim_speed    = 1.f;

	le::Mesh waves_mesh; // owning

	le::Camera                camera;
	le::CameraController      cameraController;
	le::RendergraphVisualizer rendergraph_visualizer{ renderer, true, 1024, 1024 }; // don't show initially, window_width, window_height
	le::Timebase              timebase;
};

static constexpr le_mesh_attribute_info_t mesh_layout[ 1 ]{
    { le_mesh_attribute_name::ePosition, sizeof( glm::vec4 ) },
};

// ----------------------------------------------------------------------

static void app_initialize() {

	// If you do not want validation layers active in a debug build, you can
	// override validation layer usage here:
	//
	// LE_SETTING( const bool, LE_SETTING_IDENTIFIER_SHOULD_USE_VALIDATION_LAYERS, false );

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
	app->renderer.setup( app->window );

	// Set up the camera
	reset_camera( app );

	{
		// Initialize the waves mesh

		le::Mesh tmp_mesh;
		LeMeshGenerator::generatePlane( tmp_mesh, 1024, 1024, cNumDataElements, cNumDataElements );

		size_t   vertex_count        = tmp_mesh.getVertexCount();
		uint32_t num_bytes_per_index = 0;
		size_t   index_count         = tmp_mesh.getIndexCount( &num_bytes_per_index );

		// Now that we have generated a plane mesh, we copy over only the attribute(s) that we need
		// for our waves mesh.

		app->waves_mesh.setVertexCount( vertex_count );
		{
			void* data = app->waves_mesh.allocateVertexData( mesh_layout, 1, app->renderer );
			tmp_mesh.readVertexDataIntoBuffer( data, sizeof( glm::vec4 ) * vertex_count, mesh_layout, 1 );
		}
		{
			void* data = app->waves_mesh.allocateIndexData( index_count, &num_bytes_per_index, app->renderer );
			tmp_mesh.readIndexDataInto( data, index_count * num_bytes_per_index );
		}
	}

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( compute_example_app_o* self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->cameraController.setControlRect( 0, 0, float( extents.width ), float( extents.height ) );
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
	float t_val = app->anim_t;

	le_buffer_resource_handle vertex_buffer = app->waves_mesh.getBufferForAttribute( le_mesh_attribute_name::ePosition );

	encoder
	    .bindComputePipeline( psoCompute )
	    .bindArgumentBuffer( LE_ARGUMENT_NAME( "ParticleBuf" ), vertex_buffer )
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

	rp.addColorAttachment( app->renderer.getSwapchainResource(), attachment_info ); // color attachment

	// declare that we want to use our mesh with this rendergraph
	app->waves_mesh.setupRenderPass( rp );

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

	// Note that we don't define a binding - this means that
	// it assumes the binding that is reflected from the shader?

	MvpUbo_t mvp;
	mvp.model = glm::mat4( 1.f ); // identity matrix
	app->camera.getViewMatrix( ( float* )( &mvp.view ) );
	app->camera.getProjectionMatrix( ( float* )( &mvp.projection ) );

	encoder
	    .setLineWidth( 1 )
	    .bindGraphicsPipeline( psoDefaultGraphics )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo_t ) );

	uint32_t num_indices = app->waves_mesh.bind( encoder );
	encoder.drawIndexed( num_indices );
}

// ----------------------------------------------------------------------

static void compute_example_app_process_ui_events( compute_example_app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;

	window_i.get_ui_event_queue( self->window, &pEvents, &numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	self->rendergraph_visualizer.processAndFilterEvents( events.data(), &numEvents );
	// remove any events that were consumed by rendergraph_visualizer
	events.resize( numEvents );

	bool         wants_toggle = false;
	bool         was_resized  = false;
	le::Extent2D window_extents;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eWindowExtent ): {
			auto& e        = event.windowExtent;
			window_extents = {
			    .width  = e.width,
			    .height = e.height,
			};
			was_resized = true;
		} break;

		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wants_toggle ^= true;
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

	if ( was_resized ) {
		self->renderer.resizeSwapchain( window_extents.width, window_extents.height );
		self->cameraController.setControlRect( 0, 0, float( window_extents.width ), float( window_extents.height ) );
	}

	self->cameraController.processEvents( self->camera, pEvents, numEvents );

	if ( wants_toggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool compute_example_app_update( compute_example_app_o* self ) {

	if constexpr ( C_SHOULD_USE_FIXED_FPS ) {
		self->timebase.update(
		    std::chrono::duration_cast<le::Ticks>(
		        std::chrono::duration<float, std::ratio<1, FIXED_FPS>>( 1 ) )
		        .count() );
	} else {
		self->timebase.update();
	}

	self->anim_t += self->timebase.getSecondsSinceLastFrame() * 0.25 * self->anim_speed;

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

		// Because we use the mesh vertex buffer as a storage buffer when we
		// access it via the compute shader, we must make sure that the mesh buffer
		// resource has the correct capability.
		// 1.) we fetch the buffer handle from the mesh, and its corresponding buffer info

		le_resource_info_t        vertex_buffer_info;
		le_buffer_resource_handle vertex_buffer = self->waves_mesh.getBufferForAttribute( le_mesh_attribute_name::ePosition, &vertex_buffer_info );
		// 2.) extend the buffer info with the required capability
		vertex_buffer_info.buffer.usage |= le::BufferUsageFlagBits::eStorageBuffer;

		// 3) Declare the updated buffer to the rendergraph
		renderGraph.declareResource( vertex_buffer, vertex_buffer_info );

		auto passCompute =
		    le::RenderPass( "compute", le::QueueFlagBits::eCompute )
		        .useBufferResource( vertex_buffer, le::AccessFlagBits2::eShaderStorageRead, le::AccessFlagBits2::eShaderStorageWrite )
		        .setExecuteCallback( self, pass_compute_exec );
		auto passDraw =
		    le::RenderPass( "draw", le::QueueFlagBits::eGraphics )
		        .setSetupCallback( self, pass_draw_setup )
		        .setExecuteCallback( self, pass_draw_exec )
		        .setSampleCount( le::SampleCountFlagBits::e4 );

		// Upload meshes to rendergraph
		//
		// - this happens only once per mesh (unless it is marked as tainted)
		// - this will also automatically declare mesh buffer resources.

		le_mesh_o* meshes[]{
		    self->waves_mesh,
		};
		le::Mesh::submitMeshesToRendergraph( meshes, 1, renderGraph, self->renderer );

		renderGraph
		    .addRenderPass( passCompute )
		    .addRenderPass( passDraw )
		    ;
	}

	self->rendergraph_visualizer.update( renderGraph, self->renderer.getSwapchainResource() );
	self->renderer.update( renderGraph );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void compute_example_app_destroy( compute_example_app_o* self ) {
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
