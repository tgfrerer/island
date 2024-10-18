#include "screenshot_example_app.h"

#include "le_log.h"
#include "le_window.h"
#include "le_renderer.hpp"

#include "le_pipeline_builder.h"
#include "le_ui_event.h"
#include "le_resource_manager.h"

#include "le_screenshot.h"
#include "le_debug_print_text.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/easing.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

/*
 * This example demonstrates how to use le_screenshot in context.
 *
 * It also shows how to use le_debug_print_text.
 *
 * le_screenshot has some more documentation itself, which you can
 * find in le_screenshot.h
 *
 */

struct screenshot_example_app_o {
	le::Window               window;
	uint32_t                 window_width;
	uint32_t                 window_height;
	le::Renderer             renderer;
	uint64_t                 frame_counter = 0;
	glm::vec2                mouse_pos;
	glm::quat                previous_rotation;
	glm::quat                current_rotation;
	uint64_t                 animation_start                   = 0;
	le_image_resource_handle swapchain_image                   = nullptr;
	uint32_t                 num_screenshot_examples_to_record = 0;
	le_image_resource_handle map_image;
	le_texture_handle        map_texture;
	bool                     hide_help_text = false;
	bool                     hide_grid      = false;
	le::ResourceManager      resource_manager;

	le_screenshot_o* screen_grabber; // Object to easily save screenshots
};

typedef screenshot_example_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Screenshot::init();
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static auto logger = le::Log( "test_app" );

// ----------------------------------------------------------------------

static screenshot_example_app_o* screenshot_example_app_create() {

	// If you want to disable validation layers in a debug build,
	// set LE_SETTING_SHOULD_USE_VALIDATION_LAYERS to false:
	LE_SETTING( const bool, LE_SETTING_SHOULD_USE_VALIDATION_LAYERS, true );

	auto app = new ( screenshot_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 800 * 2 )
	    .setHeight( 400 * 2 )
	    .setTitle( "Island // ScreenshotExampleApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( app->window );
	app->renderer.getSwapchainExtent( &app->window_width, &app->window_height );
	app->current_rotation  = glm::quat{ 1, 0, 0, 0 };
	app->previous_rotation = glm::quat{ 1, 0, 0, 0 };
	app->swapchain_image   = app->renderer.getSwapchainResource();

	{
		// Load the map image via the resource manager

		app->map_image   = LE_IMG_RESOURCE( "map_image" );
		char const* path = "./local_resources/images/world_winter.jpg";
		auto        image_info =
		    le::ImageInfoBuilder()
		        .setFormat( le::Format::eR8G8B8A8Unorm )
		        .setImageType( le::ImageType::e2D )
		        .build();

		app->map_texture = LE_TEXTURE( "map_texture" );
		app->resource_manager.add_item( app->map_image, image_info, &path, true );
	}

	app->screen_grabber = le_screenshot::le_screenshot_i.create( app->renderer );

	return app;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto                app = static_cast<screenshot_example_app_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	le::Extent2D extents = encoder.getRenderpassExtent();

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
		glm::mat4 u_model_matrix;
		glm::vec2 u_resolution;
		float     u_time;
		uint32_t  u_show_grid; // whether to superimpose a grid
	};

	ShaderParams params{};
	params.u_resolution   = glm::vec2( extents.width, extents.height );
	params.u_time         = app->frame_counter / 60.f; // we assume 60fps
	params.u_model_matrix = glm::mat4( 1 );
	params.u_show_grid    = app->hide_grid ? 0 : 1;

	float animation_time = glm::clamp( ( app->frame_counter - app->animation_start ) * 0.01f, 0.f, 1.f );

	animation_time        = glm::quinticEaseInOut( animation_time );
	params.u_model_matrix = glm::mat4x4( glm::slerp( app->previous_rotation, app->current_rotation, animation_time ) );

	encoder
	    .bindGraphicsPipeline( pipelineFullscreenQuad )
	    .setPushConstantData( &params, sizeof( ShaderParams ) )
	    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_0" ), app->map_texture )
	    .draw( 4 );
}

// ----------------------------------------------------------------------

static void place_north_pole( app_o* self, glm::vec2 st ) {

	float lon = ( st.x - 0.5 ) * glm::two_pi<float>();
	float lat = ( st.y - 0.5 ) * glm::pi<float>();

	float animation_time    = glm::clamp( ( self->frame_counter - self->animation_start ) * 0.01f, 0.f, 1.f );
	animation_time          = glm::quinticEaseInOut( animation_time );
	self->previous_rotation = glm::slerp( self->previous_rotation, self->current_rotation, animation_time );

	glm::vec3 previous_position = glm::vec3( 0, 0, -1 ); // z points up, right handed coordinate system

	glm::vec3 pos = glm::vec3{
	    cos( lat ) * cos( lon ),
	    cos( lat ) * sin( lon ),
	    sin( lat ),
	};

	pos = glm::normalize( pos );

	glm::vec3 axis  = normalize( glm::cross( previous_position, pos ) );
	float     angle = acos( glm::dot( previous_position, pos ) );

	if ( angle > glm::pi<float>() ) {
		angle *= -1;
	}

	// pos is by definition on the unit sphere
	float     w  = glm::cos( angle / 2 );
	float     v  = glm::sin( angle / 2 );
	glm::vec3 qv = axis * v;

	// create a quaternion from angle/axis
	self->current_rotation = glm::quat( w, qv );

	self->animation_start = self->frame_counter;
}

// ----------------------------------------------------------------------

static void app_process_ui_events( app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, &numEvents );

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
					// toggle full screen
					wants_toggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eS ) {
					// save screenshot
					self->num_screenshot_examples_to_record = 1;
				} else if ( e.key == LeUiEvent::NamedKey::eG ) {
					// toggle grid
					self->hide_grid ^= 1;
				} else if ( e.key == LeUiEvent::NamedKey::eH ) {
					// toggle help text
					self->hide_help_text ^= 1;
				} else if ( e.key == LeUiEvent::NamedKey::eR ) {
					// reset projection
					place_north_pole( self, glm::vec2( 0.5, 0 ) );
				}
			}
		} break;
		case ( LeUiEvent::Type::eMouseButton ): {
			auto& e = event.mouseButton;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				place_north_pole( self, self->mouse_pos / glm::vec2( self->window_width, self->window_height ) );
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

	if ( was_resized ) {
		self->renderer.resizeSwapchain( window_extents.width, window_extents.height );
		self->renderer.getSwapchainExtent( &self->window_width, &self->window_height );
	}

	if ( wants_toggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool screenshot_example_app_update( screenshot_example_app_o* self ) {

	// Polls events for all windows
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// Process user interface events such as mouse, keyboard
	app_process_ui_events( self );

	le::RenderGraph rg{};

	self->resource_manager.update( rg );

	{
		le::RenderPass rp_to_screen( "to_screen", le::QueueFlagBits::eGraphics );

		rp_to_screen
		    .addColorAttachment( self->renderer.getSwapchainResource() )
		    .sampleTexture( self->map_texture, self->map_image )
		    .setExecuteCallback( self, pass_main_exec ) //
		    ;

		// Draw messages to screen if there are any messages to draw
		//
		// Note le::DebugPrint will, if you don't explicitly tell it to draw
		// to a particular renderpass, automatically print into the last
		// (root) renderpass, assuming that renderpass is going to screen.
		//
		le::DebugPrint::drawAllMessages( rp_to_screen );

		rg.addRenderPass( rp_to_screen );
	}

	if ( self->screen_grabber ) {
		//
		// Note that you must call record on the screen_grabber for as long as
		// the screen_grabber is alife. This will be a largely a no-op if
		// num_screenshot_examples_to_record is 0, but it is none the less necessary
		// in case the screen_grabber has any objects in-flight that need to be
		// updated.
		//
		le_screenshot_api_i->le_screenshot_i.record( self->screen_grabber, rg, self->swapchain_image, &self->num_screenshot_examples_to_record, nullptr );
	}

	if ( !self->hide_help_text ) {

		// Debug messages are accumulated and will only be
		// drawn to a renderpass once the rendergraph gets
		// updated (executed).

		le::DebugPrint::setBgColour( { 0, 0, 0, .65 } );
		le::DebugPrint::setColour( { 1, 1, 1, 1 } );
		float content_scale_x = 1.f;
		// Update the contentscale in case we're drawing on an HiDPI monitor
		self->window.getContentScale( &content_scale_x, nullptr );
		content_scale_x *= 2;
		float y_offset = self->window_height - 16 * content_scale_x * ( 6.f + 1.5f );
		le::DebugPrint::setScale( content_scale_x );
		le::DebugPrint::setCursor( { 10.f * content_scale_x, y_offset + 10.f * content_scale_x } );
		le::DebugPrint::printf( " Click anywhere to place North Pole on map \n" );
		le::DebugPrint::printf( "\n" );
		le::DebugPrint::printf( " Key <S> to save screen to .png \n" );
		le::DebugPrint::printf( " Key <G> to toggle grid \n" );
		le::DebugPrint::printf( " Key <R> to reset projection \n" );
		le::DebugPrint::printf( " Key <H> to hide/show this text \n" );
	}

	self->renderer.update( rg );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void screenshot_example_app_destroy( screenshot_example_app_o* self ) {

	if ( self->screen_grabber ) {
		le_screenshot_api_i->le_screenshot_i.destroy( self->screen_grabber );
		self->screen_grabber = nullptr;
	}
	delete ( self );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( screenshot_example_app, api ) {

	auto  screenshot_example_app_api_i = static_cast<screenshot_example_app_api*>( api );
	auto& screenshot_example_app_i     = screenshot_example_app_api_i->screenshot_example_app_i;

	screenshot_example_app_i.initialize = app_initialize;
	screenshot_example_app_i.terminate  = app_terminate;

	screenshot_example_app_i.create  = screenshot_example_app_create;
	screenshot_example_app_i.destroy = screenshot_example_app_destroy;
	screenshot_example_app_i.update  = screenshot_example_app_update;
}
