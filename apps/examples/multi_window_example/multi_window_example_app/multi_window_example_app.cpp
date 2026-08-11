#include "multi_window_example_app.h"

#include "le_window.h"
#include "le_renderer.hpp"

#include "le_camera.h"
#include "le_pipeline_builder.h"
#include "le_ui_event.h"

#include "le_mesh.h"
#include "le_swapchain_vk.h"
#include "le_swapchain_khr.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <array>

typedef multi_window_example_app_o app_o;

struct le_mouse_event_data_o {
	uint32_t  buttonState{};
	glm::vec2 cursor_pos;
};

struct window_and_swapchain_t {
	le::Window          window;
	le_swapchain_handle swapchain;
	le::Extent2D        extent;
};

struct main_mesh_vertex_t {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec4 colour;
	glm::vec2 uv;
};

constexpr std::array<le_mesh_attribute_info_t, 3> main_mesh_layout{ {
    { le_mesh_attribute_name::ePosition, sizeof( glm::vec3 ) },
    { le_mesh_attribute_name::eNormal, sizeof( glm::vec3 ) },
    { le_mesh_attribute_name::eColour, sizeof( glm::vec4 ) },
} };

struct multi_window_example_app_o {
	std::unordered_map<uint64_t, window_and_swapchain_t> windows;
	le::Renderer                                         renderer;
	LeCameraController                                   cameraController;
	LeCamera                                             camera;
	uint64_t                                             frame_counter = 0;
	le::Mesh                                             mesh;
	std::vector<uint8_t>                                 test_vec;
};

// ----------------------------------------------------------------------

static void app_initialize() {

	// If you do not want validation layers active in a debug build, you can
	// override validation layer usage here:
	//
	// LE_SETTING( const bool, LE_SETTING_IDENTIFIER_SHOULD_USE_VALIDATION_LAYERS, false );

	//
	// Because we set up the renderer without naming swapchain settings in renderer settings
	// we must explicitly trigger a request for backend capabilities to support this particular
	// type of swapchains.
	//
	//
	le::SwapchainVk::init( le_swapchain_windowed_settings_t() );

	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void reset_camera( multi_window_example_app_o* self, window_and_swapchain_t& window ); // ffdecl.

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

	app->windows[ 0 ].window.setup( settings_0 );
	app->windows[ 1 ].window.setup( settings_1 );

	// Note that we setup the renderer without implicitly creating swapchains
	app->renderer.setup();

	{
		le_swapchain_windowed_settings_t swap_settings = {};
		swap_settings.window                           = app->windows[ 0 ].window;

		// Create swapchain 0
		app->windows[ 0 ].swapchain = app->renderer.addSwapchain( swap_settings );
		app->windows[ 0 ].extent    = app->renderer.getSwapchainExtent( app->windows[ 0 ].swapchain );
	}

	{
		le_swapchain_windowed_settings_t swap_settings = {};
		swap_settings.window                           = app->windows[ 1 ].window;

		// Create swapchain 1
		app->windows[ 1 ].swapchain = app->renderer.addSwapchain( swap_settings );
		app->windows[ 1 ].extent    = app->renderer.getSwapchainExtent( app->windows[ 1 ].swapchain );
	}

	{
		// Import mesh data into local cache.
		// Creature model created by user sugamo on poly.google.com: <https://poly.google.com/user/cyypmbztDpj>
		// Licensed CC-BY.
		static bool result = app->mesh.loadFromPlyFile( "./local_resources/meshes/sugamo-doraemon.ply" );
		assert( result );
	}

	reset_camera( app, app->windows[ 0 ] ); // set up the camera

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( multi_window_example_app_o* self, window_and_swapchain_t& window ) {
	self->camera.setViewport( { 0, float( window.extent.height ), float( window.extent.width ), -float( window.extent.height ), 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	self->camera.setViewMatrix( reinterpret_cast<float const*>( &camMatrix ) );
	self->camera.setClipDistances( 10, 10000 );
}

// ----------------------------------------------------------------------

static void pass_to_window_0( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto                app = static_cast<multi_window_example_app_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	auto [ screenWidth, screenHeight ] = encoder.getRenderpassExtent();

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

	static le_gpso_handle pipelineDefault = nullptr;
	if ( pipelineDefault == nullptr ) {

		LeGraphicsPipelineBuilder builder( encoder.getPipelineManager() );
		builder
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
		    .end();

		app->mesh.applyVertexInputDescriptions( builder, main_mesh_layout.data(), main_mesh_layout.size() );

		pipelineDefault = builder.build();
	}
	uniforms.color = { 1, 1, 1, 1 };

	encoder
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports ) //
	    ;

	uint32_t num_indices = app->mesh.bind( encoder, main_mesh_layout.data(), main_mesh_layout.size() );

	encoder
	    .bindGraphicsPipeline( pipelineDefault )
	    .setArgumentData( LE_ARGUMENT_NAME( "MVP_Default" ), &mvp, sizeof( MVP_DefaultUbo_t ) )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniform_Data" ), &uniforms, sizeof( UniformsUbo_t ) )
	    .drawIndexed( num_indices ) //
	    ;
}

// ----------------------------------------------------------------------

static void pass_to_window_1( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto                app = static_cast<multi_window_example_app_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	auto [ screenWidth, screenHeight ] = encoder.getRenderpassExtent();

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

	static le_gpso_handle pipeline_wireframe = nullptr;

	if ( nullptr == pipeline_wireframe ) {
		LeGraphicsPipelineBuilder builder( encoder.getPipelineManager() );
		builder
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
		    .end();

		app->mesh.applyVertexInputDescriptions( builder, main_mesh_layout.data(), main_mesh_layout.size() );

		pipeline_wireframe = builder.build();
	}
	uniforms.color = { 1, 1, 1, 1 };

	encoder
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports ) //
	    ;

	uint32_t num_indices = app->mesh.bind( encoder, main_mesh_layout.data(), main_mesh_layout.size() );

	encoder
	    .bindGraphicsPipeline( pipeline_wireframe )
	    .setArgumentData( LE_ARGUMENT_NAME( "MVP_Default" ), &mvp, sizeof( MVP_DefaultUbo_t ) )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniform_Data" ), &uniforms, sizeof( UniformsUbo_t ) )
	    .drawIndexed( num_indices ) //
	    ;
}

// ----------------------------------------------------------------------
static void app_process_ui_events( app_o* app, window_and_swapchain_t& window ) {
	uint32_t         numEvents;
	LeUiEvent const* pEvents;

	// Process keyboard events - but only on window 0
	// You could repeat this to process events on window 1

	window.window.getUIEventQueue( &pEvents, &numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

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
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					glm::mat4 view_matrix;
					app->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin =
					    glm::distance( glm::vec4{ 0, 0, 0, 1 },
					                   glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					app->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					app->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					reset_camera( app, window );
					glm::mat4 view_matrix;
					app->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin =
					    glm::distance( glm::vec4{ 0, 0, 0, 1 },
					                   glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					app->cameraController.setPivotDistance( distance_to_origin );
				}

			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
			break;
		}
	}

	// Process camera events
	if ( was_resized ) {
		app->renderer.resizeSwapchain( window_extents.width, window_extents.height, window.swapchain );
		window.extent = window_extents;
	}

	app->cameraController.setControlRect( 0, 0, float( window.extent.width ), float( window.extent.height ) );
	app->cameraController.processEvents( app->camera, pEvents, numEvents );

	if ( wants_toggle ) {
		window.window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool app_update( multi_window_example_app_o* self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	le::Window::pollEvents();

	for ( auto it = self->windows.begin(); it != self->windows.end(); ) {
		if ( it->second.window.shouldClose() ) {
			self->renderer.removeSwapchain( it->second.swapchain );
			//
			// Note that we don't increment the iterator `it` at the end of this branch of the loop, but
			// that we assign to iterator `it` from the result of the erasure operation.
			//
			// We do this so that we don't have to worry about deleting
			// an object from a collection whilst iterating over the collection.
			//
			it = self->windows.erase( it );
			continue;
		}
		++it;
	}

	if ( self->windows.empty() ) {
		// no more windows left, we should quit the application.
		return false;
	}

	if ( self->frame_counter == 10 ) {
		static auto should_generate_dot_files       = LE_SETTING( uint32_t, LE_SETTING_IDENTIFIER_SHOULD_RENDERGRAPH_GENERATE_DOT_FILES, 0 );
		*should_generate_dot_files                  = 2; // generate 2 .dot files
		static auto should_generate_queue_dot_files = LE_SETTING( uint32_t, LE_SETTING_IDENTIFIER_SHOULD_RENDERGRAPH_GENERATE_QUEUE_DOT_FILES, 0 );
		*should_generate_queue_dot_files            = 2; // generate 2 .dot files
	}

	// update interactive camera using mouse data
	for ( auto& [ idx, window ] : self->windows ) {
		app_process_ui_events( self, window );
	}

	// We initialise the swapchain image handles to nullptr so that they are in a known default state
	// if there is no window / swapchain associated with them.
	//
	// In a more common scenario, you would only use swapchain resources for swapchains which you know
	// existed.
	//
	// We keep it this way to demonstate what happens if you add an image resource that is NULL as a
	// Color Attachment, namely: nothing.
	//
	le_image_resource_handle IMG_SWAP[ 2 ] = {
	    nullptr,
	    nullptr,
	};

	static le_image_resource_handle DEPTH_BUFFER[ 2 ]{
	    self->renderer.createImageResourceHandle( "DEPTH_BUFFER_0" ),
	    self->renderer.createImageResourceHandle( "DEPTH_BUFFER_1" ),
	};

	for ( auto& [ idx, window ] : self->windows ) {
		IMG_SWAP[ idx ] = self->renderer.getSwapchainResource( window.swapchain );
	}

	le::RenderGraph rendergraph{};
	{
		le_mesh_o* meshes[] = {
		    self->mesh,
		};

		le::Mesh::setupRendergraph( rendergraph, self->renderer, meshes, 1 );

		le_image_attachment_info_t attachmentInfo[ 2 ];
		attachmentInfo[ 0 ].clearValue.color =
		    { { { 0xf1 / 255.f, 0x8e / 255.f, 0x00 / 255.f, 0xff / 255.f } } };
		attachmentInfo[ 1 ].clearValue.color =
		    { { { 0x22 / 255.f, 0x22 / 255.f, 0x22 / 255.f, 0xff / 255.f } } };

		// Define a renderpass, which outputs to window_0. Note that it uses
		// IMG_SWAP_0 as a color attachment.

		auto renderpass_main =
		    le::RenderPass( "to_window_0", le::QueueFlagBits::eGraphics )
		        .addColorAttachment( IMG_SWAP[ 0 ], attachmentInfo[ 0 ] ) // IMG_SWAP_0 == swapchain 0 attachment
		        .addDepthStencilAttachment( DEPTH_BUFFER[ 0 ] )
		        .setSampleCount( le::SampleCountFlagBits::e4 ) //
		        .setExecuteCallback( self, pass_to_window_0 )  //
		    ;

		self->mesh.useWithRenderpass( renderpass_main );

		rendergraph
		    .addRenderPass( renderpass_main )
		    .declareResource( DEPTH_BUFFER[ 0 ], le::ImageInfoBuilder().addUsageFlags( le::ImageUsageFlags( le::ImageUsageFlagBits::eDepthStencilAttachment ) ).build() ) //
		    ;

		// Define a renderpass, which outputs to window_1. Note that it uses
		// IMG_SWAP_1 as a color attachment.
		auto renderpass_second =
		    le::RenderPass( "to_window_1" )
		        .addColorAttachment( IMG_SWAP[ 1 ], attachmentInfo[ 1 ] ) // IMG_SWAP_1 == swapchain 1 attachment
		        .addDepthStencilAttachment( DEPTH_BUFFER[ 1 ] )
		        .setSampleCount( le::SampleCountFlagBits::e4 ) //
		        .setExecuteCallback( self, pass_to_window_1 )  //
		    ;

		self->mesh.useWithRenderpass( renderpass_second );

		rendergraph
		    .addRenderPass( renderpass_second )
		    .declareResource( DEPTH_BUFFER[ 1 ], le::ImageInfoBuilder().addUsageFlags( le::ImageUsageFlags( le::ImageUsageFlagBits::eDepthStencilAttachment ) ).build() ) //
		    ;
	}

	self->renderer.update( rendergraph );
	++self->frame_counter;

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
