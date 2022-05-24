#include "multi_window_example_app.h"

#include "le_window.h"
#include "le_renderer.h"

#include "le_camera.h"
#include "le_pipeline_builder.h"
#include "le_ui_event.h"

#include "le_mesh.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

typedef multi_window_example_app_o app_o;

struct le_mouse_event_data_o {
	uint32_t  buttonState{};
	glm::vec2 cursor_pos;
};

struct multi_window_example_app_o {
	le::Window   window_0;
	le::Window   window_1;
	le::Renderer renderer;

	LeCameraController cameraController;
	LeCamera           camera;

	LeMesh mesh;
};

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void reset_camera( multi_window_example_app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

static multi_window_example_app_o* app_create() {
	auto app = new ( multi_window_example_app_o );

	le::Window::Settings settings_0;
	settings_0
	    .setWidth( 1920 / 2 )
	    .setHeight( 1080 / 2 )
	    .setTitle( "Island // MultiWindowExampleApp- Window 0" );

	le::Window::Settings settings_1;
	settings_1
	    .setWidth( 200 )
	    .setHeight( 400 )
	    .setTitle( "Island // Window 1" );

	// Setup both windows
	app->window_0.setup( settings_0 );
	app->window_1.setup( settings_1 );

	// Attach windows to swapchains via renderer
	app->renderer.setup(
	    le::RendererInfoBuilder()
	        .addSwapchain()
	        .asWindowSwapchain()
	        .setWindow( app->window_0 )
	        .end()
	        .end()
	        .addSwapchain()
	        .asWindowSwapchain()
	        .setWindow( app->window_1 )
	        .end()
	        .end()
	        .build() );

	reset_camera( app ); // set up the camera

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( multi_window_example_app_o* self ) {
	uint32_t screenWidth{};
	uint32_t screenHeight{};
	self->renderer.getSwapchainExtent( &screenWidth, &screenHeight );

	self->camera.setViewport( { 0, float( screenHeight ), float( screenWidth ), -float( screenHeight ), 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	self->camera.setViewMatrix( reinterpret_cast<float const*>( &camMatrix ) );
	self->camera.setClipDistances( 10, 10000 );
}

// ----------------------------------------------------------------------

static void pass_to_window_0( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto        app = static_cast<multi_window_example_app_o*>( user_data );
	le::Encoder encoder{ encoder_ };

	uint32_t screenWidth, screenHeight;

	// Note that we fetch extents for swapchain 0:
	app->renderer.getSwapchainExtent( &screenWidth, &screenHeight, 0 );

	// Note that we flip the viewport (negative height) so that +Y is up.
	le::Viewport viewports[ 1 ] = {
	    { 0.f, float( screenHeight ), float( screenWidth ), -float( screenHeight ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 1 ] = {
	    { 0, 0, screenWidth, screenHeight },
	};

	// Draw main scene

	struct MVP_DefaultUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};
	MVP_DefaultUbo_t mvp;

	struct UniformsUbo_t {
		glm::vec4 color;
	} uniforms{
	    glm::vec4{ 1, 1, 1, 1 } };

	mvp.model = glm::mat4( 1.f );
	mvp.model = glm::scale( mvp.model, glm::vec3( 100 ) );
	app->camera.getViewMatrix( &mvp.view[ 0 ][ 0 ] );
	app->camera.getProjectionMatrix( &mvp.projection[ 0 ][ 0 ] );

	// Draw mesh

	static auto pipelineDefault =
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

	        .withRasterizationState()
	        .setPolygonMode( le::PolygonMode::eFill )
	        .setCullMode( le::CullModeFlagBits::eBack )
	        .setFrontFace( le::FrontFace::eCounterClockwise )
	        .end()
	        .withInputAssemblyState()
	        .setTopology( le::PrimitiveTopology::eTriangleList )
	        .end()
	        .withDepthStencilState()
	        .setDepthTestEnable( true )
	        .end()
	        .build();

	uint16_t const* meshIndices  = nullptr;
	float const*    meshVertices = nullptr;
	float const*    meshColours  = nullptr;
	float const*    meshNormals  = nullptr;
	float const*    meshUvs      = nullptr;
	size_t          numVertices  = 0;
	size_t          numIndices   = 0;
	app->mesh.getData( numVertices, numIndices, &meshVertices, &meshNormals, &meshUvs, &meshColours, &meshIndices );

	encoder
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports ) //
	    ;

	encoder
	    .setVertexData( meshVertices, numVertices * 3 * sizeof( float ), 0 )
	    .setVertexData( meshNormals, numVertices * 3 * sizeof( float ), 1 )
	    .setVertexData( meshUvs, numVertices * 2 * sizeof( float ), 2 )
	    .setVertexData( meshColours, numVertices * 4 * sizeof( float ), 3 )
	    .setIndexData( meshIndices, numIndices * sizeof( uint16_t ) );

	uniforms.color = { 1, 1, 1, 1 };

	encoder
	    .bindGraphicsPipeline( pipelineDefault )
	    .setArgumentData( LE_ARGUMENT_NAME( "MVP_Default" ), &mvp, sizeof( MVP_DefaultUbo_t ) )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniform_Data" ), &uniforms, sizeof( UniformsUbo_t ) )
	    .setLineWidth( 1.f )                   //
	    .drawIndexed( uint32_t( numIndices ) ) //
	    ;
}

// ----------------------------------------------------------------------

static void pass_to_window_1( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto        app = static_cast<multi_window_example_app_o*>( user_data );
	le::Encoder encoder{ encoder_ };

	uint32_t screenWidth, screenHeight;
	app->renderer.getSwapchainExtent( &screenWidth, &screenHeight, 1 );

	// Note that we flip the viewport (negative height) so that +Y is up.
	le::Viewport viewports[ 1 ] = {
	    { 0.f, float( screenHeight ), float( screenWidth ), -float( screenHeight ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 1 ] = {
	    { 0, 0, screenWidth, screenHeight },
	};

	// Draw main scene

	struct MVP_DefaultUbo_t {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};
	MVP_DefaultUbo_t mvp;

	struct UniformsUbo_t {
		glm::vec4 color;
	} uniforms{
	    glm::vec4{ 1, 1, 1, 1 } };

	mvp.model = glm::mat4( 1.f );                          // identity matrix
	mvp.model = glm::scale( mvp.model, glm::vec3( 100 ) ); // scale by 100
	app->camera.getViewMatrix( &mvp.view[ 0 ][ 0 ] );
	app->camera.getProjectionMatrix( &mvp.projection[ 0 ][ 0 ] );

	// Draw mesh

	static auto pipelineWireframe =
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
	                .setSourceDefinesString( "SHOW_MONO_COLOUR" )
	                .build() )

	        .withRasterizationState()
	        .setPolygonMode( le::PolygonMode::eLine )
	        .setCullMode( le::CullModeFlagBits::eBack )
	        .setFrontFace( le::FrontFace::eCounterClockwise )
	        .end()
	        .withInputAssemblyState()
	        .setTopology( le::PrimitiveTopology::eTriangleList )
	        .end()
	        .withDepthStencilState()
	        .setDepthTestEnable( true )
	        .end()
	        .build();

	uint16_t const* meshIndices  = nullptr;
	float const*    meshVertices = nullptr;
	float const*    meshColours  = nullptr;
	float const*    meshNormals  = nullptr;
	float const*    meshUvs      = nullptr;
	size_t          numVertices  = 0;
	size_t          numIndices   = 0;
	app->mesh.getData( numVertices, numIndices, &meshVertices, &meshNormals, &meshUvs, &meshColours, &meshIndices );

	uniforms.color = { 1, 1, 1, 1 };

	encoder
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports ) //
	    ;

	encoder
	    .setVertexData( meshVertices, numVertices * 3 * sizeof( float ), 0 )
	    .setVertexData( meshNormals, numVertices * 3 * sizeof( float ), 1 )
	    .setVertexData( meshUvs, numVertices * 2 * sizeof( float ), 2 )
	    .setVertexData( meshColours, numVertices * 4 * sizeof( float ), 3 )
	    .setIndexData( meshIndices, numIndices * sizeof( uint16_t ) );

	encoder
	    .bindGraphicsPipeline( pipelineWireframe )
	    .setArgumentData( LE_ARGUMENT_NAME( "MVP_Default" ), &mvp, sizeof( MVP_DefaultUbo_t ) )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniform_Data" ), &uniforms, sizeof( UniformsUbo_t ) )
	    .setLineWidth( 1.f )                   //
	    .drawIndexed( uint32_t( numIndices ) ) //
	    ;
}

// ----------------------------------------------------------------------
static void app_process_ui_events( app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;

	// Process keyboard events - but only on window 0
	// You could repeat this to process events on window 1

	window_i.get_ui_event_queue( self->window_0, &pEvents, numEvents ); // note: self->window_0

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool wantsToggle = false;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					float distance_to_origin =
					    glm::distance( glm::vec4{ 0, 0, 0, 1 },
					                   glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					reset_camera( self );
					float distance_to_origin =
					    glm::distance( glm::vec4{ 0, 0, 0, 1 },
					                   glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				}

			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
			break;
		}
	}

	{
		// Process camera events for window 0

		auto swapchainExtent = self->renderer.getSwapchainExtent( 0 ); // Note we're using swapchain extents 0
		self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );
		self->cameraController.processEvents( self->camera, pEvents, numEvents );
	}
	{
		// Process camera events for window 1
		window_i.get_ui_event_queue( self->window_1, &pEvents, numEvents ); // note: self->window_1

		auto swapchainExtent = self->renderer.getSwapchainExtent( 1 ); // Note we're using swapchain extents 0
		self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );
		self->cameraController.processEvents( self->camera, pEvents, numEvents );
	}

	if ( wantsToggle ) {
		self->window_0.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool app_update( multi_window_example_app_o* self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	le::Window::pollEvents();

	if ( self->window_0.shouldClose() || self->window_1.shouldClose() ) {
		return false;
	}

	// update interactive camera using mouse data
	app_process_ui_events( self );

	static bool resetCameraOnReload = false; // reload meand module reload
	if ( resetCameraOnReload ) {
		// Reset camera
		reset_camera( self );
		resetCameraOnReload = false;
	}

	// Creature model created by user sugamo on poly.google.com: <https://poly.google.com/user/cyypmbztDpj>
	// Licensed CC-BY.
	static bool result = self->mesh.loadFromPlyFile( "./local_resources/meshes/sugamo-doraemon.ply" );

	static auto IMG_SWAP_0 = self->renderer.getSwapchainResource( 0 );
	static auto IMG_SWAP_1 = self->renderer.getSwapchainResource( 1 );

	assert( result );

	using namespace le_renderer;

	le::RenderGraph renderGraph{};
	{

		le_image_attachment_info_t attachmentInfo[ 2 ];
		attachmentInfo[ 0 ].clearValue.color =
		    { { { 0xf1 / 255.f, 0x8e / 255.f, 0x00 / 255.f, 0xff / 255.f } } };
		attachmentInfo[ 1 ].clearValue.color =
		    { { { 0x22 / 255.f, 0x22 / 255.f, 0x22 / 255.f, 0xff / 255.f } } };

		// Define a renderpass which outputs to window_0.
		auto renderPassMain =
		    le::RenderPass( "to_window_0", LE_RENDER_PASS_TYPE_DRAW )
		        .addColorAttachment( IMG_SWAP_0, attachmentInfo[ 0 ] ) // IMG_SWAP_0 == swapchain 0 attachment
		        .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_BUFFER" ) )
		        .setSampleCount( le::SampleCountFlagBits::e8 )
		        .setExecuteCallback( self, pass_to_window_0 ) //
		    ;

		// Define a renderpass, which outputs to window_1. Note that it uses
		// IMG_SWAP_1 as a color attachment.

		// Because only the image resource for the default swapchain is automatically
		// recognised as drawing to screen, you must set the renderpass Root manually.
		// Root means that the renderpass is tagged as contributing to the final image,
		// otherwise the rendergraph is free to optimise it away, and along with it any
		// renderpasses that contribute to it.

		auto renderPassSecond =
		    le::RenderPass( "to_window_1" )
		        .addColorAttachment( IMG_SWAP_1, attachmentInfo[ 1 ] ) // IMG_SWAP_1 == swapchain 1 attachment
		        .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_BUFFER_1" ) )
		        .setExecuteCallback( self, pass_to_window_1 )
		        .setIsRoot( true ) // IMPORTANT!
		    ;

		renderGraph
		    .addRenderPass( renderPassMain )
		    .addRenderPass( renderPassSecond );
	}

	self->renderer.update( renderGraph );

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void app_destroy( multi_window_example_app_o* self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( multi_window_example_app, api ) {
	auto  multi_window_example_app_api_i = static_cast<multi_window_example_app_api*>( api );
	auto& multi_window_example_app_i     = multi_window_example_app_api_i->multi_window_example_app_i;

	multi_window_example_app_i.initialize = app_initialize;
	multi_window_example_app_i.terminate  = app_terminate;

	multi_window_example_app_i.create  = app_create;
	multi_window_example_app_i.destroy = app_destroy;
	multi_window_example_app_i.update  = app_update;
}
