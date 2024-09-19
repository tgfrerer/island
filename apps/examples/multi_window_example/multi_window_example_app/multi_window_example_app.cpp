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

struct cached_mesh_data_t {
	std::unordered_map<le_mesh_api::attribute_name_t, std::vector<char>> attributes;
	std::vector<char>                                                    indices;
	size_t                                                               indices_count;
	le::IndexType                                                        index_type;
};

struct multi_window_example_app_o {
	std::unordered_map<uint64_t, window_and_swapchain_t> windows;
	le::Renderer                                         renderer;
	LeCameraController                                   cameraController;
	LeCamera                                             camera;
	uint64_t                                             frame_counter = 0;
	cached_mesh_data_t                                   mesh;
	std::vector<uint8_t>                                 test_vec;
};

// ----------------------------------------------------------------------

static void app_initialize() {

	//
	// Because we set up the renderer without naming swapchain settings in renderer settings
	// we must explicitly trigger a request for backend capabilities to support this particular
	// type of swapchains.
	//
	bool has_capabilities = le_swapchain_vk_api_i->swapchain_i.request_backend_capabilities( le_swapchain_windowed_settings_t{} );

	assert( has_capabilities );

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

	LE_SETTING( const bool, LE_SETTING_SHOULD_USE_VALIDATION_LAYERS, true );

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

		LeMesh meshImporter;

		// Creature model created by user sugamo on poly.google.com: <https://poly.google.com/user/cyypmbztDpj>
		// Licensed CC-BY.
		static bool result = meshImporter.loadFromPlyFile( "./local_resources/meshes/sugamo-doraemon.ply" );
		assert( result );

		std::vector<le_mesh_api::attribute_info_t> attribute_infos( 5 );

		size_t num_attribute_infos = attribute_infos.size();
		meshImporter.readAttributeInfosInto( attribute_infos.data(), &num_attribute_infos );
		attribute_infos.resize( num_attribute_infos );

		size_t num_vertices = meshImporter.getVertexCount();

		for ( auto& a : attribute_infos ) {
			auto& attribute_data = app->mesh.attributes[ a.name ];
			attribute_data.resize( a.bytes_per_vertex * num_vertices );
			meshImporter.readAttributeDataInto( attribute_data.data(), attribute_data.size(), a.name );
		}

		uint32_t num_bytes_per_index = 0;
		size_t   num_indices         = meshImporter.getIndexCount( &num_bytes_per_index );
		app->mesh.index_type         = ( num_bytes_per_index == 2 ) ? le::IndexType::eUint16 : le::IndexType::eUint32;
		app->mesh.indices_count      = num_indices;

		app->mesh.indices.resize( num_bytes_per_index * num_indices );
		meshImporter.readIndexDataInto( app->mesh.indices.data(), app->mesh.indices.size() );
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

	uniforms.color = { 1, 1, 1, 1 };

	encoder
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports ) //
	    ;

	encoder
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::ePosition ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::ePosition ].size(), 0 )
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::eNormal ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::eNormal ].size(), 1 )
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::eUv ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::eUv ].size(), 2 )
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::eColour ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::eColour ].size(), 3 )
	    .setIndexData( app->mesh.indices.data(), app->mesh.indices.size(), app->mesh.index_type );

	encoder
	    .bindGraphicsPipeline( pipelineDefault )
	    .setArgumentData( LE_ARGUMENT_NAME( "MVP_Default" ), &mvp, sizeof( MVP_DefaultUbo_t ) )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniform_Data" ), &uniforms, sizeof( UniformsUbo_t ) )
	    .setLineWidth( 1.f )                    //
	    .drawIndexed( app->mesh.indices_count ) //
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

	uniforms.color = { 1, 1, 1, 1 };

	encoder
	    .setScissors( 0, 1, scissors )
	    .setViewports( 0, 1, viewports ) //
	    ;

	encoder
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::ePosition ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::ePosition ].size(), 0 )
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::eNormal ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::eNormal ].size(), 1 )
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::eUv ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::eUv ].size(), 2 )
	    .setVertexData( app->mesh.attributes[ le_mesh_api::attribute_name_t::eColour ].data(),
	                    app->mesh.attributes[ le_mesh_api::attribute_name_t::eColour ].size(), 3 )
	    .setIndexData( app->mesh.indices.data(), app->mesh.indices.size(), app->mesh.index_type );

	encoder
	    .bindGraphicsPipeline( pipelineWireframe )
	    .setArgumentData( LE_ARGUMENT_NAME( "MVP_Default" ), &mvp, sizeof( MVP_DefaultUbo_t ) )
	    .setArgumentData( LE_ARGUMENT_NAME( "Uniform_Data" ), &uniforms, sizeof( UniformsUbo_t ) )
	    .setLineWidth( 1.f )                    //
	    .drawIndexed( app->mesh.indices_count ) //
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
		case ( LeUiEvent::Type::eWindowResize ): {
			auto& e        = event.windowSize;
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
		LE_SETTING( uint32_t, LE_SETTING_GENERATE_QUEUE_SYNC_DOT_FILES, 0 );
		*LE_SETTING_GENERATE_QUEUE_SYNC_DOT_FILES = 2; // generate 2 .dot files
		LE_SETTING( uint32_t, LE_SETTING_RENDERGRAPH_GENERATE_DOT_FILES, 0 );
		*LE_SETTING_RENDERGRAPH_GENERATE_DOT_FILES = 2; // generate 2 .dot files
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

	for ( auto& [ idx, window ] : self->windows ) {
		IMG_SWAP[ idx ] = self->renderer.getSwapchainResource( window.swapchain );
	}

	le::RenderGraph renderGraph{};
	{

		le_image_attachment_info_t attachmentInfo[ 2 ];
		attachmentInfo[ 0 ].clearValue.color =
		    { { { 0xf1 / 255.f, 0x8e / 255.f, 0x00 / 255.f, 0xff / 255.f } } };
		attachmentInfo[ 1 ].clearValue.color =
		    { { { 0x22 / 255.f, 0x22 / 255.f, 0x22 / 255.f, 0xff / 255.f } } };

		// Define a renderpass, which outputs to window_0. Note that it uses
		// IMG_SWAP_0 as a color attachment.

		auto renderPassMain =
		    le::RenderPass( "to_window_0", le::QueueFlagBits::eGraphics )
		        .addColorAttachment( IMG_SWAP[ 0 ], attachmentInfo[ 0 ] ) // IMG_SWAP_0 == swapchain 0 attachment
		        .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_BUFFER_0" ) )
		        .setSampleCount( le::SampleCountFlagBits::e8 ) //
		        .setExecuteCallback( self, pass_to_window_0 )  //
		    ;

		renderGraph
		    .addRenderPass( renderPassMain )
		    .declareResource( LE_IMG_RESOURCE( "DEPTH_BUFFER_0" ), le::ImageInfoBuilder().addUsageFlags( le::ImageUsageFlags( le::ImageUsageFlagBits::eDepthStencilAttachment ) ).build() ) //
		    ;

		// Define a renderpass, which outputs to window_1. Note that it uses
		// IMG_SWAP_1 as a color attachment.
		auto renderPassSecond =
		    le::RenderPass( "to_window_1" )
		        .addColorAttachment( IMG_SWAP[ 1 ], attachmentInfo[ 1 ] ) // IMG_SWAP_1 == swapchain 1 attachment
		        .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_BUFFER_1" ) )
		        .setSampleCount( le::SampleCountFlagBits::e8 ) //
		        .setExecuteCallback( self, pass_to_window_1 )  //
		    ;

		renderGraph
		    .addRenderPass( renderPassSecond )
		    .declareResource( LE_IMG_RESOURCE( "DEPTH_BUFFER_1" ), le::ImageInfoBuilder().addUsageFlags( le::ImageUsageFlags( le::ImageUsageFlagBits::eDepthStencilAttachment ) ).build() ) //
		    ;
	}

	self->renderer.update( renderGraph );
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
