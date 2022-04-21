#include "imgui_example_app.h"

#include "le_window.h"
#include "le_renderer.h"

#include "le_camera.h"
#include "le_pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include "le_imgui.h"
#include "3rdparty/imgui/imgui.h"

#include "le_ui_event.h"

#if ( WIN32 )
#	pragma comment( lib, "modules/imgui.lib" )
#endif

struct imgui_example_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	glm::vec4 backgroundColor{ 0, 0, 0, 1 };

	le_imgui_o* gui;
	LeCamera    camera;
};

typedef imgui_example_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void reset_camera( imgui_example_app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

static imgui_example_app_o* imgui_example_app_create() {
	auto app = new ( imgui_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // ImguiExampleApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	// Set up the camera
	reset_camera( app );

	app->gui = le_imgui::le_imgui_i.create();

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( imgui_example_app_o* self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( { 0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	self->camera.setViewMatrixGlm( camMatrix );
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto        app = static_cast<imgui_example_app_o*>( user_data );
	le::Encoder encoder{ encoder_ };

	auto extents = encoder.getRenderpassExtent();

	le::Viewport viewports[ 1 ] = {
	    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	// Data as it is laid out in the shader ubo
	struct MvpUbo {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Draw main scene

	static auto pipelineImguiExample =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./resources/shaders/default.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./resources/shaders/default.frag" )
	                .build() )
	        .build();

	MvpUbo mvp;
	mvp.model      = glm::mat4( 1.f ); // identity matrix
	mvp.model      = glm::scale( mvp.model, glm::vec3( 4.5 ) );
	mvp.view       = app->camera.getViewMatrixGlm();
	mvp.projection = app->camera.getProjectionMatrixGlm();

	glm::vec3 imgui_examplePositions[] = {
	    { -50, -50, 0 },
	    { 50, -50, 0 },
	    { 0, 50, 0 },
	};

	glm::vec4 imgui_exampleColors[] = {
	    { 1, 0, 0, 1.f },
	    { 0, 1, 0, 1.f },
	    { 0, 0, 1, 1.f },
	};

	encoder
	    .bindGraphicsPipeline( pipelineImguiExample )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
	    .setVertexData( imgui_examplePositions, sizeof( imgui_examplePositions ), 0 )
	    .setVertexData( imgui_exampleColors, sizeof( imgui_exampleColors ), 1 )
	    .draw( 3 );
}

// ----------------------------------------------------------------------

static bool imgui_example_app_update( imgui_example_app_o* self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	{
		// -- Process UI events

		LeUiEvent const* events;
		uint32_t         numEvents = 0;
		self->window.getUIEventQueue( &events, numEvents );
		le_imgui::le_imgui_i.process_events( self->gui, events, numEvents );
	}

	uint32_t swapchainWidth = 0, swapchainHeight = 0;
	self->renderer.getSwapchainExtent( &swapchainWidth, &swapchainHeight );

	le::RenderModule mainModule{};
	{

		le_imgui::le_imgui_i.setup_resources( self->gui, mainModule, float( swapchainWidth ), float( swapchainHeight ) );

		le::RenderPass passToScreen( "root", LE_RENDER_PASS_TYPE_DRAW );

		le_imgui::le_imgui_i.begin_frame( self->gui );

		ImGui::ShowMetricsWindow();
		ImGui::ShowDemoWindow();

		ImGui::Begin( "Background Color Chooser" ); // begin window

		// Background color edit
		if ( ImGui::ColorEdit3( "Background Color", &self->backgroundColor.x ) ) {
		}

		if ( ImGui::Button( "White Background" ) ) {
			self->backgroundColor = { 1, 1, 1, 1 };
		}

		ImGui::End(); // end window

		le_imgui::le_imgui_i.end_frame( self->gui );

		passToScreen
		    .setSetupCallback( self, []( le_renderpass_o* pRp, void* user_data ) {
			    auto rp  = le::RenderPass{ pRp };
			    auto app = static_cast<imgui_example_app_o*>( user_data );

			    // Attachment resource info may be further specialised using le::ImageAttachmentInfoBuilder().

			    auto info =
			        le::ImageAttachmentInfoBuilder()
			            .setColorClearValue( reinterpret_cast<LeClearValue&>( app->backgroundColor ) )
			            .build();

			    rp
			        .addColorAttachment( app->renderer.getSwapchainResource(), info ) // color attachment
			        .setIsRoot( true );

			    return true;
		    } )
		    .setExecuteCallback( self, pass_main_exec ) //
		    ;

		le_imgui::le_imgui_i.draw( self->gui, passToScreen );

		mainModule.addRenderPass( passToScreen );
	}

	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void imgui_example_app_destroy( imgui_example_app_o* self ) {

	if ( self->gui ) {
		le_imgui::le_imgui_i.destroy( self->gui );
		self->gui = nullptr;
	}

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( imgui_example_app, api ) {
	auto  imgui_example_app_api_i = static_cast<imgui_example_app_api*>( api );
	auto& imgui_example_app_i     = imgui_example_app_api_i->imgui_example_app_i;

	imgui_example_app_i.initialize = app_initialize;
	imgui_example_app_i.terminate  = app_terminate;

	imgui_example_app_i.create  = imgui_example_app_create;
	imgui_example_app_i.destroy = imgui_example_app_destroy;
	imgui_example_app_i.update  = imgui_example_app_update;
}
