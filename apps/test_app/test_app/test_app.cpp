#include "test_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"
#include "le_gltf_loader/le_gltf_loader.h"

#include "simple_module/simple_module.h"

#include "le_camera/le_camera.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h> // for key codes

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "gtx/easing.hpp"

#include <iostream>
#include <memory>

#include "horse_image.h"
#include "imgui/imgui.h"

#include <sstream>
#include <bitset>

#include <chrono> // for nanotime
using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct le_graphics_pipeline_state_o; // owned by renderer

struct GltfUboMvp {
	glm::mat4 projection;
	glm::mat4 model;
	glm::mat4 view;
};

struct FontTextureInfo {
	uint8_t *          pixels            = nullptr;
	int32_t            width             = 0;
	int32_t            height            = 0;
	le::ResourceHandle le_texture_handle = nullptr;
	le::ResourceHandle le_image_handle   = nullptr;
	bool               wasUploaded       = false;
};

struct le_mouse_event_data_o {
	uint8_t   buttonState{};
	glm::vec2 cursor_pos;
};

struct camera_o {
	glm::mat4    matrix;       // camera position in world space
	float        fovRadians{}; // field of view angle (in radians)
	le::Viewport viewport;     // current camera viewport
};

struct camera_controller_o {

	glm::mat4 matrix; // initial transform

	enum Mode {
		eNeutral = 0,
		eRotXY   = 1,
		eRotZ,
		eTranslateXY,
		eTranslateZ,
	};

	Mode                 mode{};
	std::array<float, 4> controlRect = {}; // active rectangle for mouse inputs

	glm::vec2 mouse_pos_initial; // initial position of mouse on mouse_down
};

struct test_app_o {
	std::unique_ptr<le::Backend>  backend;
	std::unique_ptr<pal::Window>  window;
	std::unique_ptr<le::Renderer> renderer;
	le_graphics_pipeline_state_o *psoMain;           // weak ref, owned by renderer
	le_graphics_pipeline_state_o *psoFullScreenQuad; // weak ref, owned by renderer
	le_graphics_pipeline_state_o *psoImgui;          // weak ref, owned by renderer
	ImGuiContext *                imguiContext  = nullptr;
	uint64_t                      frame_counter = 0;
	float                         deltaTimeSec  = 0;

	FontTextureInfo imguiTexture = {};

	std::array<bool, 5>   mouseButtonStatus{}; // status for each mouse button
	glm::vec2             mousePos{};          // current mouse position
	le_mouse_event_data_o mouseData;

	NanoTime update_start_time;

	// Note we use the c++ facade for resource handles as this guarantees that resource
	// handles are initialised to nullptr, otherwise this is too easy to forget...
	le::ResourceHandle resImgPrepass     = nullptr;
	le::ResourceHandle resImgDepth       = nullptr;
	le::ResourceHandle resTexPrepass     = nullptr;
	le::ResourceHandle resImgHorse       = nullptr;
	le::ResourceHandle resTexHorse       = nullptr;
	le::ResourceHandle resBufTrianglePos = nullptr;

	bool                imgHorseWasUploaded = false;
	le_gltf_document_o *gltfDoc             = nullptr;

	// NOTE: RUNTIME-COMPILE : If you add any new things during run-time, make sure to only add at the end of the object,
	// otherwise all pointers above will be invalidated. this might also overwrite memory which
	// is stored after this object, which is very subtle in introducing errors. We need to think about a way of serializing
	// and de-serializing objects which are allocated on the heap. we don't have to worry about objects which are allocated
	// on the stack, as the stack acts like a pool allocator, and they are only alife while control visits the code section
	// in question.
	SimpleModule testSimpleModule;

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

// ----------------------------------------------------------------------

static void test_app_key_callback( void *user_data, int key, int scancode, int action, int mods ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<test_app_o *>( user_data );

	{
		static auto const &window_i = Registry::getApi<pal_window_api>()->window_i;
		if ( key == GLFW_KEY_F11 && action == GLFW_RELEASE ) {
			window_i.toggle_fullscreen( *app->window );
		}
	}
	ImGuiIO &io = ImGui::GetIO();

	if ( action == GLFW_PRESS ) {
		io.KeysDown[ key ] = true;
	}
	if ( action == GLFW_RELEASE ) {
		io.KeysDown[ key ] = false;
	}

	( void )mods; // Modifiers are not reliable across systems
	io.KeyCtrl  = io.KeysDown[ GLFW_KEY_LEFT_CONTROL ] || io.KeysDown[ GLFW_KEY_RIGHT_CONTROL ];
	io.KeyShift = io.KeysDown[ GLFW_KEY_LEFT_SHIFT ] || io.KeysDown[ GLFW_KEY_RIGHT_SHIFT ];
	io.KeyAlt   = io.KeysDown[ GLFW_KEY_LEFT_ALT ] || io.KeysDown[ GLFW_KEY_RIGHT_ALT ];
	io.KeySuper = io.KeysDown[ GLFW_KEY_LEFT_SUPER ] || io.KeysDown[ GLFW_KEY_RIGHT_SUPER ];
}
static void test_app_character_callback( void *user_data, unsigned int codepoint ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<test_app_o *>( user_data );

	ImGuiIO &io = ImGui::GetIO();

	if ( codepoint > 0 && codepoint < 0x10000 ) {
		io.AddInputCharacter( ( unsigned short )codepoint );
	}
}
static void test_app_cursor_position_callback( void *user_data, double xpos, double ypos ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<test_app_o *>( user_data );

	//	std::cout << "mx: " << xpos << ", my: " << ypos << std::endl
	//	          << std::flush;

	//	std::cout << "inside rect: " << ( is_inside_rect( {float( xpos ), float( ypos )}, {100, 50, 50, 100} ) ? "INSIDE" : "OUTSIDE" ) << std::endl
	//	          << std::flush;

	app->mouseData.cursor_pos = {float( xpos ), float( ypos )};
	app->mousePos             = {float( xpos ), float( ypos )};
}
static void test_app_cursor_enter_callback( void *user_data, int entered ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<test_app_o *>( user_data );
}
static void test_app_mouse_button_callback( void *user_data, int button, int action, int mods ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<test_app_o *>( user_data );

	if ( button >= 0 && button < int( app->mouseButtonStatus.size() ) ) {
		app->mouseButtonStatus[ size_t( button ) ] = ( action == GLFW_PRESS );

		if ( action == GLFW_PRESS ) {
			app->mouseData.buttonState |= uint8_t( 1 << size_t( button ) );
		} else if ( action == GLFW_RELEASE ) {
			app->mouseData.buttonState &= uint8_t( 0 << size_t( button ) );
		}
	}
}
static void test_app_scroll_callback( void *user_data, double xoffset, double yoffset ) {

	if ( user_data == nullptr ) {
		std::cerr << __FILE__ << "#L" << std::dec << __LINE__ << "Missing user data." << std::endl
		          << std::flush;
		return;
	}

	// --------| invariant : user data is not null

	auto app = static_cast<test_app_o *>( user_data );

	ImGuiIO &io = ImGui::GetIO();
	io.MouseWheelH += static_cast<float>( xoffset );
	io.MouseWheel += static_cast<float>( yoffset );
}

// ----------------------------------------------------------------------

static test_app_o *test_app_create() {
	auto app = new ( test_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 768 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window = std::make_unique<pal::Window>( settings );

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );

	app->backend = std::make_unique<le::Backend>( &backendCreateInfo );

	// We need a valid instance at this point.
	app->backend->createWindowSurface( *app->window );
	app->backend->createSwapchain( nullptr ); // TODO (swapchain) - make it possible to set swapchain parameters

	app->backend->setup();

	app->renderer = std::make_unique<le::Renderer>( *app->backend );
	app->renderer->setup();

	{
		// -- Declare graphics pipeline state objects

		{
			// create default pipeline

			auto defaultVertShader = app->renderer->createShaderModule( "./resources/shaders/default.vert", LeShaderType::eVert );
			auto defaultFragShader = app->renderer->createShaderModule( "./resources/shaders/default.frag", LeShaderType::eFrag );

			le_graphics_pipeline_create_info_t pi;
			pi.shader_module_frag = defaultFragShader;
			pi.shader_module_vert = defaultVertShader;

			// The pipeline state object holds all state for the pipeline,
			// that's links to shader modules, blend states, input assembly, etc...
			// Everything, in short, but the renderpass, and subpass (which are added at the last minute)
			//
			// The backend pipeline object is compiled on-demand, when it is first used with a renderpass, and henceforth cached.
			auto pso = app->renderer->createGraphicsPipelineStateObject( &pi );

			if ( pso ) {
				app->psoMain = pso;
			} else {
				std::cerr << "declaring main pipeline failed miserably.";
			}
		}

		{
			// Create pso for imgui rendering
			auto imguiVertShader = app->renderer->createShaderModule( "./resources/shaders/imgui.vert", LeShaderType::eVert );
			auto imguiFragShader = app->renderer->createShaderModule( "./resources/shaders/imgui.frag", LeShaderType::eFrag );

			le_graphics_pipeline_create_info_t                   imGuiPipelineInfo{};
			std::array<le_vertex_input_attribute_description, 3> attrs    = {};
			std::array<le_vertex_input_binding_description, 1>   bindings = {};
			{
				// location 0, binding 0
				attrs[ 0 ].location       = 0;                           // refers to shader parameter location
				attrs[ 0 ].binding        = 0;                           // refers to bound buffer index
				attrs[ 0 ].binding_offset = offsetof( ImDrawVert, pos ); // offset into bound buffer
				attrs[ 0 ].isNormalised   = false;
				attrs[ 0 ].type           = le_vertex_input_attribute_description::eFloat;
				attrs[ 0 ].vecsize        = 2;

				// location 1, binding 0
				attrs[ 1 ].location       = 1;
				attrs[ 1 ].binding        = 0;
				attrs[ 1 ].binding_offset = offsetof( ImDrawVert, uv );
				attrs[ 1 ].isNormalised   = false;
				attrs[ 1 ].type           = le_vertex_input_attribute_description::eFloat;
				attrs[ 1 ].vecsize        = 2;

				// location 2, binding 0
				attrs[ 2 ].location       = 2;
				attrs[ 2 ].binding        = 0;
				attrs[ 2 ].binding_offset = offsetof( ImDrawVert, col );
				attrs[ 2 ].isNormalised   = true;
				attrs[ 2 ].type           = le_vertex_input_attribute_description::eChar;
				attrs[ 2 ].vecsize        = 4;
			}
			{
				// binding 0
				bindings[ 0 ].binding    = 0;
				bindings[ 0 ].input_rate = le_vertex_input_binding_description::ePerVertex;
				bindings[ 0 ].stride     = sizeof( ImDrawVert );
			}

			imGuiPipelineInfo.shader_module_frag = imguiFragShader;
			imGuiPipelineInfo.shader_module_vert = imguiVertShader;

			imGuiPipelineInfo.vertex_input_attribute_descriptions       = attrs.data();
			imGuiPipelineInfo.vertex_input_attribute_descriptions_count = attrs.size();
			imGuiPipelineInfo.vertex_input_binding_descriptions         = bindings.data();
			imGuiPipelineInfo.vertex_input_binding_descriptions_count   = bindings.size();

			auto psoHandle = app->renderer->createGraphicsPipelineStateObject( &imGuiPipelineInfo );

			if ( psoHandle ) {
				app->psoImgui = psoHandle;
			} else {
				std::cerr << "declaring pso for imgui failed miserably.";
			}
		}

		{
			// create full screen quad pipeline

			auto fullScreenQuadVertShader = app->renderer->createShaderModule( "./resources/shaders/fullscreenQuad.vert", LeShaderType::eVert );
			auto fullScreenQuadFragShader = app->renderer->createShaderModule( "./resources/shaders/fullscreenQuad.frag", LeShaderType::eFrag );

			le_graphics_pipeline_create_info_t pi{};
			pi.shader_module_vert = fullScreenQuadVertShader;
			pi.shader_module_frag = fullScreenQuadFragShader;

			auto psoHandle = app->renderer->createGraphicsPipelineStateObject( &pi );

			if ( psoHandle ) {
				app->psoFullScreenQuad = psoHandle;
			} else {
				std::cerr << "declaring test pipeline failed miserably.";
			}
		}
	}

	app->imguiContext = ImGui::CreateContext();

	// get imgui font texture handle
	{
		ImGuiIO &io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF( "./resources/fonts/IBMPlexSans-Regular.otf", 20.0f, nullptr, io.Fonts->GetGlyphRangesDefault() );
		io.Fonts->GetTexDataAsRGBA32( &app->imguiTexture.pixels, &app->imguiTexture.width, &app->imguiTexture.height );

		app->imguiTexture.le_image_handle   = app->renderer->declareResource( LeResourceType::eImage );
		app->imguiTexture.le_texture_handle = app->renderer->declareResource( LeResourceType::eTexture );

		io.DisplaySize  = {float( app->window->getSurfaceWidth() ),
		                  float( app->window->getSurfaceHeight() )};
		io.Fonts->TexID = ( app->imguiTexture.le_texture_handle );

		// Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
		io.KeyMap[ ImGuiKey_Tab ]        = GLFW_KEY_TAB;
		io.KeyMap[ ImGuiKey_LeftArrow ]  = GLFW_KEY_LEFT;
		io.KeyMap[ ImGuiKey_RightArrow ] = GLFW_KEY_RIGHT;
		io.KeyMap[ ImGuiKey_UpArrow ]    = GLFW_KEY_UP;
		io.KeyMap[ ImGuiKey_DownArrow ]  = GLFW_KEY_DOWN;
		io.KeyMap[ ImGuiKey_PageUp ]     = GLFW_KEY_PAGE_UP;
		io.KeyMap[ ImGuiKey_PageDown ]   = GLFW_KEY_PAGE_DOWN;
		io.KeyMap[ ImGuiKey_Home ]       = GLFW_KEY_HOME;
		io.KeyMap[ ImGuiKey_End ]        = GLFW_KEY_END;
		io.KeyMap[ ImGuiKey_Insert ]     = GLFW_KEY_INSERT;
		io.KeyMap[ ImGuiKey_Delete ]     = GLFW_KEY_DELETE;
		io.KeyMap[ ImGuiKey_Backspace ]  = GLFW_KEY_BACKSPACE;
		io.KeyMap[ ImGuiKey_Space ]      = GLFW_KEY_SPACE;
		io.KeyMap[ ImGuiKey_Enter ]      = GLFW_KEY_ENTER;
		io.KeyMap[ ImGuiKey_Escape ]     = GLFW_KEY_ESCAPE;
		io.KeyMap[ ImGuiKey_A ]          = GLFW_KEY_A;
		io.KeyMap[ ImGuiKey_C ]          = GLFW_KEY_C;
		io.KeyMap[ ImGuiKey_V ]          = GLFW_KEY_V;
		io.KeyMap[ ImGuiKey_X ]          = GLFW_KEY_X;
		io.KeyMap[ ImGuiKey_Y ]          = GLFW_KEY_Y;
		io.KeyMap[ ImGuiKey_Z ]          = GLFW_KEY_Z;
	}

	{
		// -- Set window event callbacks

		using namespace pal_window;
		// set the callback user data for all callbacks from window *app->window
		// to be our app pointer.
		window_i.set_callback_user_data( *app->window, app );

		using test_app::test_app_i;

		window_i.set_key_callback( *app->window, &test_app_i.key_callback );
		window_i.set_character_callback( *app->window, &test_app_i.character_callback );

		window_i.set_cursor_position_callback( *app->window, &test_app_i.cursor_position_callback );
		window_i.set_cursor_enter_callback( *app->window, &test_app_i.cursor_enter_callback );
		window_i.set_mouse_button_callback( *app->window, &test_app_i.mouse_button_callback );
		window_i.set_scroll_callback( *app->window, &test_app_i.scroll_callback );
	}

	app->update_start_time = std::chrono::high_resolution_clock::now();

	{
		using le_gltf_loader::gltf_document_i;

		app->gltfDoc = gltf_document_i.create();
		//gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/BoomBoxWithAxes.gltf" );
		gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/FlightHelmet.gltf" );
		//gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/Box.gltf" );
		//gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/exportFile.gltf" );
		gltf_document_i.setup_resources( app->gltfDoc, *app->renderer );
	}

	app->resImgPrepass     = app->renderer->declareResource( LeResourceType::eImage );
	app->resImgDepth       = app->renderer->declareResource( LeResourceType::eImage );
	app->resTexPrepass     = app->renderer->declareResource( LeResourceType::eTexture );
	app->resImgHorse       = app->renderer->declareResource( LeResourceType::eImage );
	app->resTexHorse       = app->renderer->declareResource( LeResourceType::eTexture );
	app->resBufTrianglePos = app->renderer->declareResource( LeResourceType::eBuffer );

	app->camera.setViewport( {0, 0, 1024, 768, 0.f, 1.f} );
	app->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, app->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );

	app->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );

	return app;
}

// ----------------------------------------------------------------------

static float get_image_plane_distance( const le::Viewport &viewport, float fovRadians ) {
	return viewport.height / ( 2.0f * tanf( fovRadians * 0.5f ) );
}

// ----------------------------------------------------------------------

static bool test_app_update( test_app_o *self ) {

	ImGui::SetCurrentContext( self->imguiContext ); // NOTICE: that's important for reload.
	{
		// update frame delta time
		auto   current_time = std::chrono::high_resolution_clock::now();
		double millis       = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>( current_time - self->update_start_time ).count();
		self->deltaTimeSec  = float( millis / 1000.0 );

		auto &io     = ImGui::GetIO();
		io.DeltaTime = self->deltaTimeSec;

		//		self->metrics.appUpdateTimes.push( millis );

		self->update_start_time = current_time;
	}

	// Polls events for all windows
	// this means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	//	std::cout << "mouse button status: " << std::bitset<8>( self->mouseData.buttonState ) << std::endl
	//	          << std::flush;

	if ( self->window->shouldClose() ) {
		return false;
	}

	{
		// update interactive camera using mouse data
		self->cameraController.setControlRect( 0, 0, float( self->window->getSurfaceWidth() ), float( self->window->getSurfaceHeight() ) );
		self->cameraController.updateCamera( self->camera, &self->mouseData );
	}

	{
		ImGuiIO &io = ImGui::GetIO();

		io.DisplaySize = {float( self->window->getSurfaceWidth() ),
		                  float( self->window->getSurfaceHeight() )};

		// update mouse pos and buttons
		for ( size_t i = 0; i < self->mouseButtonStatus.size(); i++ ) {
			// If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
			io.MouseDown[ i ] = self->mouseButtonStatus[ i ];
		}
		io.MousePos = {self->mousePos.x, self->mousePos.y};
	}

	ImGui::NewFrame();

	// ImGui::Text( "Hello Island" );

	//	ImGui::ShowDemoWindow();
	ImGui::ShowMetricsWindow();
	ImGui::Render();

	// Grab interface for encoder so that it can be used in callbacks -
	// making it static allows it to be visible inside the callback context,
	// and it also ensures that the registry call only happens upon first retrieval.

	using namespace le_renderer;

	static auto const &gltf_i = Registry::getApi<le_gltf_loader_api>()->document_i;

	le::RenderModule mainModule{};
	{
		le::RenderPass resourcePass( "resource copy", LE_RENDER_PASS_TYPE_TRANSFER );

		resourcePass.setSetupCallback( self, []( auto pRp, auto user_data_ ) -> bool {
			auto app = static_cast<test_app_o *>( user_data_ );
			auto rp  = le::RenderPassRef{pRp};

			{
				// create image for the horse image
				le_resource_info_t imgInfo{};
				imgInfo.type = LeResourceType::eImage;
				{
					auto &img         = imgInfo.image;
					img.format        = VK_FORMAT_R8G8B8A8_UNORM;
					img.flags         = 0;
					img.arrayLayers   = 1;
					img.extent.depth  = 1;
					img.extent.width  = 160;
					img.extent.height = 106;
					img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
					img.mipLevels     = 1;
					img.samples       = VK_SAMPLE_COUNT_1_BIT;
					img.imageType     = VK_IMAGE_TYPE_2D;
					img.tiling        = VK_IMAGE_TILING_OPTIMAL;
				}
				rp.createResource( app->resImgHorse, imgInfo );
			}

			{
				// create resource for imgui font texture if it does not yet exist.
				// create image for imgui image
				le_resource_info_t imgInfo{};
				imgInfo.type = LeResourceType::eImage;
				{
					auto &img         = imgInfo.image;
					img.format        = VK_FORMAT_R8G8B8A8_UNORM;
					img.flags         = 0;
					img.arrayLayers   = 1;
					img.extent.depth  = 1;
					img.extent.width  = uint32_t( app->imguiTexture.width );
					img.extent.height = uint32_t( app->imguiTexture.height );
					img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
					img.mipLevels     = 1;
					img.samples       = VK_SAMPLE_COUNT_1_BIT;
					img.imageType     = VK_IMAGE_TYPE_2D;
					img.tiling        = VK_IMAGE_TILING_OPTIMAL;
				}
				rp.createResource( app->imguiTexture.le_image_handle, imgInfo );
			}

			{
				// create image for prepass
				le_resource_info_t imgInfo{};
				imgInfo.type = LeResourceType::eImage;
				{
					auto &img         = imgInfo.image;
					img.format        = VK_FORMAT_R8G8B8A8_UNORM;
					img.flags         = 0;
					img.arrayLayers   = 1;
					img.extent.width  = uint32_t( 640 );
					img.extent.height = uint32_t( 480 );
					img.extent.depth  = 1;
					img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
					img.mipLevels     = 1;
					img.samples       = VK_SAMPLE_COUNT_1_BIT;
					img.imageType     = VK_IMAGE_TYPE_2D;
					img.tiling        = VK_IMAGE_TILING_OPTIMAL;
				}
				rp.createResource( app->resImgPrepass, imgInfo );
			}

			{
				// create z-buffer image for main renderpass
				le_resource_info_t imgInfo{};
				imgInfo.type = LeResourceType::eImage;
				{
					auto &img         = imgInfo.image;
					img.format        = VK_FORMAT_D32_SFLOAT_S8_UINT;
					img.flags         = 0;
					img.arrayLayers   = 1;
					img.extent.depth  = 1;
					img.extent.width  = 0; // zero means size of backbuffer.
					img.extent.height = 0; // zero means size of backbuffer.
					img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
					img.mipLevels     = 1;
					img.samples       = VK_SAMPLE_COUNT_1_BIT;
					img.imageType     = VK_IMAGE_TYPE_2D;
					img.tiling        = VK_IMAGE_TILING_OPTIMAL;
				}
				rp.createResource( app->resImgDepth, imgInfo );
			}

			{
				// create resource for triangle vertex buffer
				le_resource_info_t bufInfo{};
				bufInfo.type         = LeResourceType::eBuffer;
				bufInfo.buffer.size  = sizeof( glm::vec3 ) * 3;
				bufInfo.buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
				rp.createResource( app->resBufTrianglePos, bufInfo );
			}

			{
				// create resources for gltf document
				le_resource_info_t *    resourceInfo;
				LeResourceHandle const *resourceHandles;
				size_t                  numResourceInfos;
				gltf_i.get_resource_infos( app->gltfDoc, &resourceInfo, &resourceHandles, &numResourceInfos );

				for ( size_t i = 0; i != numResourceInfos; i++ ) {
					rp.createResource( resourceHandles[ i ], resourceInfo[ i ] );
				}
			}

			return true;
		} );

		resourcePass.setExecuteCallback( self, []( auto encoder, auto user_data_ ) {
			auto app = static_cast<test_app_o *>( user_data_ );

			// Writing is always to encoder scratch buffer memory because that's the only memory that
			// is HOST visible.
			//
			// Type of resource ownership decides whether
			// a copy is added to the queue that transfers from scratch memory
			// to GPU local memory.

			if ( app->imgHorseWasUploaded == false ) {

				encoder_i.write_to_image( encoder, app->resImgHorse, {160, 106}, MagickImage, sizeof( MagickImage ) );
				app->imgHorseWasUploaded = true;
			}

			if ( false == app->imguiTexture.wasUploaded ) {
				// tell encoder to upload imgui image - but only once
				// note that we use the le_image_handle field to signal that the image has been uploaded.
				size_t              numBytes = size_t( app->imguiTexture.width ) * size_t( app->imguiTexture.height ) * 32;
				LeBufferWriteRegion region   = {uint32_t( app->imguiTexture.width ), uint32_t( app->imguiTexture.height )};
				encoder_i.write_to_image( encoder, app->imguiTexture.le_image_handle, region, app->imguiTexture.pixels, numBytes );
				app->imguiTexture.wasUploaded = true;
			}

			{
				// upload triangle data
				glm::vec3 trianglePositions[ 3 ] = {
				    {-50, -50, 0},
				    {50, -50, 0},
				    {0, 50, 0},
				};

				encoder_i.write_to_buffer( encoder, app->resBufTrianglePos, 0, trianglePositions, sizeof( trianglePositions ) );
			}

			gltf_i.upload_resource_data( app->gltfDoc, encoder );
		} );

		le::RenderPass renderPassPre( "prepass", LE_RENDER_PASS_TYPE_DRAW );
		renderPassPre.setSetupCallback( self, []( auto pRp, auto user_data_ ) -> bool {
			auto rp  = le::RenderPassRef{pRp};
			auto app = static_cast<test_app_o *>( user_data_ );

			rp.addImageAttachment( app->resImgPrepass );

			rp.useResource( app->resImgHorse );

			LeTextureInfo textureInfo{};
			textureInfo.imageView.imageId = app->resImgHorse;
			textureInfo.sampler.magFilter = VK_FILTER_NEAREST;
			textureInfo.sampler.minFilter = VK_FILTER_NEAREST;
			rp.sampleTexture( app->resTexHorse, textureInfo );

			rp.setWidth( 640 );
			rp.setHeight( 480 );

			return true;
		} );

		renderPassPre.setExecuteCallback( self, []( le_command_buffer_encoder_o *encoder_, void *user_data ) {
			auto     encoder      = le::Encoder( encoder_ ); // use c++ facade for less typing ;)
			auto     app          = static_cast<test_app_o *>( user_data );
			uint32_t screenWidth  = 640;
			uint32_t screenHeight = 480;

			le::Viewport viewports[ 1 ] = {
			    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},

			};

			le::Rect2D scissors[ 1 ] = {
			    {0, 0, screenWidth, screenHeight},
			};

			// Bind full screen quad pipeline
			if ( true ) {

				encoder
				    .bindGraphicsPipeline( app->psoFullScreenQuad )
				    .setArgumentTexture( app->resTexHorse, hash_64_fnv1a_const( "src_tex_unit_0" ), 0 )
				    .setScissors( 0, 1, &scissors[ 0 ] )
				    .setViewports( 0, 1, &viewports[ 0 ] )
				    .draw( 3, 1, 0, 0 );
			}
		} );

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );

		renderPassFinal.setSetupCallback( self, []( auto pRp, auto user_data_ ) -> bool {
			auto rp  = le::RenderPassRef{pRp};
			auto app = static_cast<test_app_o *>( user_data_ );

			LeImageAttachmentInfo info{};

			rp
			    .addImageAttachment( app->renderer->getBackbufferResource() ) // color attachment
			    .addDepthImageAttachment( app->resImgDepth )                  // depth attachment
			    .setIsRoot( true );

			rp
			    .sampleTexture( app->resTexPrepass, {VK_FILTER_NEAREST, VK_FILTER_NEAREST, app->resImgPrepass, 0} )
			    .sampleTexture( app->imguiTexture.le_texture_handle, {VK_FILTER_LINEAR, VK_FILTER_LINEAR, app->imguiTexture.le_image_handle, 0} );

			return true;
		} );

		renderPassFinal.setExecuteCallback( self, []( le_command_buffer_encoder_o *encoder, void *user_data ) {
			auto app = static_cast<test_app_o *>( user_data );

			auto screenWidth  = app->window->getSurfaceWidth();
			auto screenHeight = app->window->getSurfaceHeight();

			le::Viewport viewports[ 2 ] = {
			    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
			    {10.f, 10.f, 160.f * 3.f + 10.f, 106.f * 3.f + 10.f, 0.f, 1.f},
			};

			app->camera.setViewport( viewports[ 0 ] );

			le::Rect2D scissors[ 2 ] = {
			    {0, 0, screenWidth, screenHeight},
			    {10, 10, 160 * 3 + 10, 106 * 3 + 10},
			};

			glm::vec4 triangleColors[ 3 ] = {
			    {1, 0, 0, 1.f},
			    {0, 1, 0, 1.f},
			    {0, 0, 1, 1.f},
			};

			uint16_t indexData[ 3 ] = {0, 1, 2};

			// data as it is laid out in the ubo for the shader
			struct ColorUbo_t {
				glm::vec4 color;
			};

			struct MatrixStackUbo_t {
				glm::mat4 modelMatrix;
				glm::mat4 viewMatrix;
				glm::mat4 projectionMatrix;
			};

			static float t   = 0;
			t                = fmodf( t + app->deltaTimeSec, 10.f );
			float r_val      = t / 10.f;
			float r_anim_val = glm::elasticEaseOut( r_val );

			ColorUbo_t ubo1{{1, 0, 0, 1}};

			// Bind full screen quad pipeline
			if ( false ) {

				encoder_i.bind_graphics_pipeline( encoder, app->psoFullScreenQuad );
				encoder_i.set_argument_texture( encoder, app->resTexPrepass, hash_64_fnv1a_const( "src_tex_unit_0" ), 0 );
				encoder_i.set_scissor( encoder, 0, 1, &scissors[ 1 ] );
				encoder_i.set_viewport( encoder, 0, 1, &viewports[ 1 ] );
				encoder_i.draw( encoder, 3, 1, 0, 0 );
			}

			// Draw RGB triangle
			if ( true ) {
				encoder_i.bind_graphics_pipeline( encoder, app->psoMain );

				encoder_i.set_scissor( encoder, 0, 1, scissors );
				encoder_i.set_viewport( encoder, 0, 1, viewports );

				MatrixStackUbo_t matrixStack;

				matrixStack.projectionMatrix = *reinterpret_cast<glm::mat4 const *>( app->camera.getProjectionMatrix() );

				matrixStack.modelMatrix = glm::mat4( 1.f ); // identity matrix

				matrixStack.modelMatrix = glm::translate( matrixStack.modelMatrix, glm::vec3( 0, 0, -100 ) );
				matrixStack.modelMatrix = glm::rotate( matrixStack.modelMatrix, glm::radians( r_anim_val * 360 ), glm::vec3( 0, 0, 1 ) );
				matrixStack.modelMatrix = glm::scale( matrixStack.modelMatrix, glm::vec3( 4.5 ) );

				matrixStack.viewMatrix = *reinterpret_cast<glm::mat4 const *>( app->camera.getViewMatrix() );

				encoder_i.set_argument_ubo_data( encoder, hash_64_fnv1a_const( "MatrixStack" ), &matrixStack, sizeof( MatrixStackUbo_t ) );
				encoder_i.set_argument_ubo_data( encoder, hash_64_fnv1a_const( "Color" ), &ubo1, sizeof( ColorUbo_t ) );

				LeResourceHandle buffers[] = {app->resBufTrianglePos};
				uint64_t         offsets[] = {0};

				encoder_i.bind_vertex_buffers( encoder, 0, 1, buffers, offsets );

				encoder_i.set_vertex_data( encoder, triangleColors, sizeof( glm::vec4 ) * 3, 1 );
				encoder_i.set_index_data( encoder, indexData, sizeof( indexData ), 0 ); // 0 for indexType means uint16_t
				encoder_i.draw_indexed( encoder, 3, 1, 0, 0, 0 );
			}

			// Draw GLTF file
			if ( true ) {

				encoder_i.set_scissor( encoder, 0, 1, scissors );
				encoder_i.set_viewport( encoder, 0, 1, viewports );

				GltfUboMvp ubo;

				ubo.projection = *reinterpret_cast<glm::mat4 const *>( app->camera.getProjectionMatrix() );
				ubo.model      = glm::mat4( 1 );
				ubo.model      = glm::translate( ubo.model, glm::vec3( 0, 0, 0 ) );

				ubo.model = glm::rotate( ubo.model, glm::radians( r_val * 360.f ), glm::vec3( 0, 1, 0 ) );
				ubo.model = glm::scale( ubo.model, glm::vec3( 400.f ) ); // identity matrix

				ubo.view = *reinterpret_cast<glm::mat4 const *>( app->camera.getViewMatrix() );

				// FIXME: we must first set the pipeline, before we can upload any arguments

				gltf_i.draw( app->gltfDoc, encoder, &ubo );
			}

			ImDrawData *drawData = ImGui::GetDrawData();
			if ( drawData ) {
				// draw imgui

				auto ortho_projection = glm::ortho( 0.f, float( screenWidth ), 0.f, float( screenHeight ) );

				ImVec2 display_pos = drawData->DisplayPos;

				encoder_i.bind_graphics_pipeline( encoder, app->psoImgui );
				encoder_i.set_viewport( encoder, 0, 1, &viewports[ 0 ] ); // TODO: make sure that viewport covers full screen
				encoder_i.set_argument_ubo_data( encoder, hash_64_fnv1a_const( "MatrixStack" ), &ortho_projection, sizeof( glm::mat4 ) );
				encoder_i.set_argument_texture( encoder, app->imguiTexture.le_texture_handle, hash_64_fnv1a_const( "tex_unit_0" ), 0 );

				LeResourceHandle currentTexture = app->imguiTexture.le_texture_handle; // we check against this so that we don't have to switch state that often.

				ImVec4 currentClipRect{};

				for ( ImDrawList **cmdList = drawData->CmdLists; cmdList != drawData->CmdLists + drawData->CmdListsCount; cmdList++ ) {
					auto &im_cmd_list = *cmdList;

					// upload index data
					encoder_i.set_index_data( encoder, im_cmd_list->IdxBuffer.Data, size_t( im_cmd_list->IdxBuffer.size() * sizeof( ImDrawIdx ) ), 0 );
					// upload vertex data
					encoder_i.set_vertex_data( encoder, im_cmd_list->VtxBuffer.Data, size_t( im_cmd_list->VtxBuffer.size() * sizeof( ImDrawVert ) ), 0 );

					uint32_t index_offset = 0;
					for ( const auto &im_cmd : im_cmd_list->CmdBuffer ) {

						if ( im_cmd.UserCallback ) {
							// call user callback
							continue;
						}
						// -----| invariant: im_cmd was not user callback

						static_assert( sizeof( le::Rect2D ) == sizeof( ImVec4 ), "clip rect size must match for direct assignment" );

						// -- update bound texture, but only if texture different from currently bound texture
						const LeResourceHandle nextTexture = reinterpret_cast<const LeResourceHandle>( im_cmd.TextureId );
						if ( nextTexture != currentTexture ) {
							encoder_i.set_argument_texture( encoder, nextTexture, hash_64_fnv1a_const( "tex_unit_0" ), 0 );
							currentTexture = nextTexture;
						}

						// -- set clip rectangle as scissor
						if ( 0 != memcmp( &im_cmd.ClipRect, &currentClipRect, sizeof( ImVec4 ) ) ) {
							// clip rects are different
							currentClipRect = im_cmd.ClipRect;
							le::Rect2D scissor;
							scissor.x      = ( int32_t )( im_cmd.ClipRect.x - display_pos.x ) > 0 ? ( int32_t )( im_cmd.ClipRect.x - display_pos.x ) : 0;
							scissor.y      = ( int32_t )( im_cmd.ClipRect.y - display_pos.y ) > 0 ? ( int32_t )( im_cmd.ClipRect.y - display_pos.y ) : 0;
							scissor.width  = ( uint32_t )( im_cmd.ClipRect.z - im_cmd.ClipRect.x );
							scissor.height = ( uint32_t )( im_cmd.ClipRect.w - im_cmd.ClipRect.y + 1 ); // FIXME: Why +1 here?

							encoder_i.set_scissor( encoder, 0, 1, &scissor );
						}

						// uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
						encoder_i.draw_indexed( encoder, im_cmd.ElemCount, 1, index_offset, 0, 0 );
						index_offset += im_cmd.ElemCount;
					}

					//					std::cout << std::dec << im_cmd_list->VtxBuffer.size() << std::endl
					//					          << std::flush;
				}
			}
		} );

		mainModule.addRenderPass( resourcePass );
		mainModule.addRenderPass( renderPassPre );
		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer->update( mainModule );

	self->frame_counter++;

	self->testSimpleModule.update();

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_app_destroy( test_app_o *self ) {

	if ( self->gltfDoc ) {
		static auto const &gltf_i = Registry::getApi<le_gltf_loader_api>()->document_i;
		gltf_i.destroy( self->gltfDoc );
		self->gltfDoc = nullptr;
	}

	if ( self->imguiContext ) {
		ImGui::DestroyContext( self->imguiContext );
		self->imguiContext = nullptr;
	}
	delete ( self );
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_test_app_api( void *api ) {
	auto  test_app_api_i = static_cast<test_app_api *>( api );
	auto &test_app_i     = test_app_api_i->test_app_i;

	test_app_i.initialize = initialize;
	test_app_i.terminate  = terminate;

	test_app_i.create  = test_app_create;
	test_app_i.destroy = test_app_destroy;
	test_app_i.update  = test_app_update;

	test_app_i.key_callback             = test_app_key_callback;
	test_app_i.character_callback       = test_app_character_callback;
	test_app_i.cursor_position_callback = test_app_cursor_position_callback;
	test_app_i.cursor_enter_callback    = test_app_cursor_enter_callback;
	test_app_i.mouse_button_callback    = test_app_mouse_button_callback;
	test_app_i.scroll_callback          = test_app_scroll_callback;

#ifdef PLUGINS_DYNAMIC
	Registry::loadLibraryPersistently( "./libs/imgui/libimgui.so" );
#endif
}
