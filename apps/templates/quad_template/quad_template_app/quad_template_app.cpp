#include "quad_template_app.h"

#include "le_window.h"
#include "le_renderer.h"

#include "le_pipeline_builder.h"
#include "le_ui_event.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct quad_template_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;
	glm::vec2    mouse_pos;
};

typedef quad_template_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

// ----------------------------------------------------------------------

static quad_template_app_o* quad_template_app_create() {
	auto app = new ( quad_template_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // QuadTemplateApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	return app;
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o* pRp, void* user_data ) {
	auto rp  = le::RenderPass{ pRp };
	auto app = static_cast<quad_template_app_o*>( user_data );

	// Attachment resource info may be further specialised using ImageInfoBuilder().
	// Attachment clear color, load and store op may be set via le_image_attachment_info_t.

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto        app = static_cast<quad_template_app_o*>( user_data );
	le::Encoder encoder{ encoder_ };

	auto extents = encoder.getRenderpassExtent();

	// Draw main scene

	static auto shaderVert =
	    LeShaderModuleBuilder( encoder.getPipelineManager() )
	        .setShaderStage( le::ShaderStage::eVertex )
	        .setSourceFilePath( "./local_resources/shaders/glsl/fullscreen.vert" )
	        .setSourceLanguage( le::ShaderSourceLanguage::eGlsl )
	        .build();

	static auto shaderFrag =
	    LeShaderModuleBuilder( encoder.getPipelineManager() )
	        .setShaderStage( le::ShaderStage::eFragment )
	        .setSourceFilePath( "./local_resources/shaders/glsl/fullscreen.frag" )
	        .setSourceLanguage( le::ShaderSourceLanguage::eGlsl )
	        .build();

	static auto pipelineFullscreenQuad =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .build();

	struct ShaderParams {
		glm::vec2 u_mouse;
		glm::vec2 u_resolution;
		float     u_time;
	};

	ShaderParams params{};
	params.u_resolution = glm::vec2( extents.width, extents.height );
	params.u_mouse      = app->mouse_pos / params.u_resolution;
	params.u_time       = app->frame_counter / 60.f; // we assume 60fps

	encoder
	    .bindGraphicsPipeline( pipelineFullscreenQuad )
	    .setPushConstantData( &params, sizeof( ShaderParams ) )
	    .draw( 4 );
}

// ----------------------------------------------------------------------

static void app_process_ui_events( app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool wantsToggle = false;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				}
			}
		} break;
		case ( LeUiEvent::Type::eCursorPosition ): {
			auto& e         = event.cursorPosition;
			self->mouse_pos = glm::vec2{ e.x, e.y };
			break;
		}
		default:
			// do nothing
			break;
		}
	}

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool quad_template_app_update( quad_template_app_o* self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// Process user interface events such as mouse, keyboard
	app_process_ui_events( self );

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

static void quad_template_app_destroy( quad_template_app_o* self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( quad_template_app, api ) {

	auto  quad_template_app_api_i = static_cast<quad_template_app_api*>( api );
	auto& quad_template_app_i     = quad_template_app_api_i->quad_template_app_i;

	quad_template_app_i.initialize = app_initialize;
	quad_template_app_i.terminate  = app_terminate;

	quad_template_app_i.create  = quad_template_app_create;
	quad_template_app_i.destroy = quad_template_app_destroy;
	quad_template_app_i.update  = quad_template_app_update;
}
