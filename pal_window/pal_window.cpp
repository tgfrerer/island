#include "pal_window/pal_window.h"
#include "assert.h"

#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

struct pal_window_settings_o {
	int          width   = 640;
	int          height  = 480;
	std::string  title   = "default window title";
	GLFWmonitor *monitor = nullptr;
};

struct pal_window_o {
	GLFWwindow *          window   = nullptr;
	VkSurfaceKHR          mSurface = nullptr;
	VkExtent2D            mSurfaceExtent{};
	pal_window_settings_o mSettings{};
	VkInstance            mInstance      = nullptr;
	size_t                referenceCount = 0;
	void *                user_data      = nullptr;

	pal_window_api::key_callback_fun_t *            key_callback             = nullptr;
	pal_window_api::character_callback_fun_t *      character_callback       = nullptr;
	pal_window_api::cursor_position_callback_fun_t *cursor_position_callback = nullptr;
	pal_window_api::cursor_enter_callback_fun_t *   cursor_enter_callback    = nullptr;
	pal_window_api::mouse_button_callback_fun_t *   mouse_button_callback    = nullptr;
	pal_window_api::scroll_callback_fun_t *         scroll_callback          = nullptr;
};

// ----------------------------------------------------------------------
static void glfw_window_key_callback( GLFWwindow *glfwWindow, int key, int scancode, int action, int mods ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->key_callback ) {
		// NOTE: we first look up the address, then call,
		// this is because the function may be from an indirection table.
		( *window->key_callback )( window->user_data, key, scancode, action, mods );
	}
}

// ----------------------------------------------------------------------
static void glfw_window_character_callback( GLFWwindow *glfwWindow, unsigned int codepoint ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->character_callback ) {
		// NOTE: we first look up the address, then call,
		// this is because the function may be from an indirection table.
		( *window->character_callback )( window->user_data, codepoint );
	}
}

// ----------------------------------------------------------------------
static void glfw_window_cursor_position_callback( GLFWwindow *glfwWindow, double xpos, double ypos ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->cursor_position_callback ) {
		// NOTE: we first look up the address, then call,
		// this is because the function may be from an indirection table.
		( *window->cursor_position_callback )( window->user_data, xpos, ypos );
	}
}

// ----------------------------------------------------------------------
static void glfw_window_cursor_enter_callback( GLFWwindow *glfwWindow, int entered ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->cursor_enter_callback ) {
		// NOTE: we first look up the address, then call,
		// this is because the function may be from an indirection table.
		( *window->cursor_enter_callback )( window->user_data, entered );
	}
}

// ----------------------------------------------------------------------
static void glfw_window_mouse_button_callback( GLFWwindow *glfwWindow, int button, int action, int mods ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->mouse_button_callback ) {
		// NOTE: we first look up the address, then call,
		// this is because the function may be from an indirection table.
		( *window->mouse_button_callback )( window->user_data, button, action, mods );
	}
}

// ----------------------------------------------------------------------
static void glfw_window_scroll_callback( GLFWwindow *glfwWindow, double xoffset, double yoffset ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->scroll_callback ) {
		// NOTE: we first look up the address, then call,
		// this is because the function may be from an indirection table.
		( *window->scroll_callback )( window->user_data, xoffset, yoffset );
	}
}

// ----------------------------------------------------------------------
static void window_set_key_callback( pal_window_o *self, pal_window_api::key_callback_fun_t *callback ) {
	self->key_callback = callback;
}

// ----------------------------------------------------------------------
static void window_set_character_callback( pal_window_o *self, pal_window_api::character_callback_fun_t *callback ) {
	self->character_callback = callback;
}

// ----------------------------------------------------------------------
static void window_set_cursor_position_callback( pal_window_o *self, pal_window_api::cursor_position_callback_fun_t *callback ) {
	self->cursor_position_callback = callback;
}

// ----------------------------------------------------------------------
static void window_set_cursor_enter_callback( pal_window_o *self, pal_window_api::cursor_enter_callback_fun_t *callback ) {
	self->cursor_enter_callback = callback;
}

// ----------------------------------------------------------------------
static void window_set_mouse_button_callback( pal_window_o *self, pal_window_api::mouse_button_callback_fun_t *callback ) {
	self->mouse_button_callback = callback;
}

// ----------------------------------------------------------------------
static void window_set_scroll_callback( pal_window_o *self, pal_window_api::scroll_callback_fun_t *callback ) {
	self->scroll_callback = callback;
}
// ----------------------------------------------------------------------
static void glfw_framebuffer_resize_callback( GLFWwindow *glfwWindow, int width_px, int height_px ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	glfwGetWindowSize( glfwWindow, &window->mSettings.width, &window->mSettings.height );
	window->mSurfaceExtent.width  = uint32_t( width_px );
	window->mSurfaceExtent.height = uint32_t( height_px );

	std::cout << " Framebuffer resized callback: " << std::dec << width_px << ", " << height_px << std::endl
	          << std::flush;
};

// ----------------------------------------------------------------------
static void window_set_callback_user_data( pal_window_o *self, void *user_data ) {
	self->user_data = user_data;
}

// ----------------------------------------------------------------------

static size_t window_get_reference_count( pal_window_o *self ) {
	return self->referenceCount;
}

// ----------------------------------------------------------------------

static void window_increase_reference_count( pal_window_o *self ) {
	++self->referenceCount;
}

// ----------------------------------------------------------------------

static void window_decrease_reference_count( pal_window_o *self ) {
	--self->referenceCount;
}

// ----------------------------------------------------------------------

static pal_window_settings_o *window_settings_create() {
	pal_window_settings_o *obj = new ( pal_window_settings_o );
	return obj;
}

// ----------------------------------------------------------------------

static void window_settings_set_title( pal_window_settings_o *self_, const char *title_ ) {
	self_->title = std::string( title_ );
}

// ----------------------------------------------------------------------

static void window_settings_set_width( pal_window_settings_o *self_, int width_ ) {
	self_->width = width_;
}

// ----------------------------------------------------------------------

static void window_settings_set_height( pal_window_settings_o *self_, int height_ ) {
	self_->height = height_;
}

// ----------------------------------------------------------------------

static void window_settings_destroy( pal_window_settings_o *self_ ) {
	delete self_;
}

// ----------------------------------------------------------------------

static bool window_create_surface( pal_window_o *self, VkInstance vkInstance ) {
	auto result = glfwCreateWindowSurface( vkInstance, self->window, nullptr, &self->mSurface );
	if ( result == VK_SUCCESS ) {
		int tmp_w = 0;
		int tmp_h = 0;
		glfwGetFramebufferSize( self->window, &tmp_w, &tmp_h );
		self->mSurfaceExtent.height = uint32_t( tmp_h );
		self->mSurfaceExtent.width  = uint32_t( tmp_w );
		self->mInstance             = vkInstance;
		std::cout << "Created surface" << std::endl;
	} else {
		std::cerr << "Error creating surface" << std::endl;
		return false;
	}
	return true;
}

// ----------------------------------------------------------------------
// note: this is the only function for which we need to link this lib against vulkan!
static void window_destroy_surface( pal_window_o *self ) {
	if ( self->mInstance ) {
		PFN_vkDestroySurfaceKHR destroySurfaceFun = reinterpret_cast<PFN_vkDestroySurfaceKHR>( vkGetInstanceProcAddr( self->mInstance, "vkDestroySurfaceKHR" ) );
		destroySurfaceFun( self->mInstance, self->mSurface, nullptr );
		std::cout << "Surface destroyed" << std::endl;
		self->mSurface = nullptr;
	}
}

// ----------------------------------------------------------------------

static uint32_t window_get_surface_width( pal_window_o *self ) {
	if ( self->mSurface ) {
		return self->mSurfaceExtent.width;
	}
	return 0;
}

// ----------------------------------------------------------------------

static uint32_t window_get_surface_height( pal_window_o *self ) {
	if ( self->mSurface ) {
		return self->mSurfaceExtent.height;
	}
	return 0;
}

// ----------------------------------------------------------------------

static VkSurfaceKHR window_get_vk_surface_khr( pal_window_o *self ) {
	return self->mSurface;
}

static void window_set_callbacks( pal_window_o *self ) {

	// FIXME: Callback function address target may have changed after library hot-reload
	// Problem -- the address of the callback function may have changed
	// after the library was reloaded, and we would have to go through
	// all windows to patch the callback function.
	//
	// We could make sure that there is a forwarder which has a constant
	// address, and which calls, in turn, a method which we can patch during
	// reload.
	//
	// The forwarder function would have to live somewhere permanent.
	// Could this be a function object inside registry?
	//
	// This problem arises mostly because each window within GLFW may have its own
	// callbacks - which means, each GLFW window would have to be patched after hot-reloading.

	glfwSetKeyCallback( self->window, glfw_window_key_callback );
	glfwSetCharCallback( self->window, glfw_window_character_callback );
	glfwSetCursorPosCallback( self->window, glfw_window_cursor_position_callback );
	glfwSetCursorEnterCallback( self->window, glfw_window_cursor_enter_callback );
	glfwSetMouseButtonCallback( self->window, glfw_window_mouse_button_callback );
	glfwSetScrollCallback( self->window, glfw_window_scroll_callback );

	glfwSetFramebufferSizeCallback( self->window, glfw_framebuffer_resize_callback );
}

// ----------------------------------------------------------------------

static pal_window_o *window_create( const pal_window_settings_o *settings_ ) {
	auto obj = new pal_window_o();

	if ( settings_ ) {
		obj->mSettings = *settings_;
	}

	// TODO: implement GLFW window hints, based on settings.
	// See: http://www.glfw.org/docs/latest/window_guide.html#window_hints

	glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );

	obj->window = glfwCreateWindow( obj->mSettings.width, obj->mSettings.height, obj->mSettings.title.c_str(), obj->mSettings.monitor, nullptr );

	// Set the user pointer so callbacks know which window they belong to
	glfwSetWindowUserPointer( obj->window, obj );

	window_set_callbacks( obj );

	return obj;
}

// ----------------------------------------------------------------------

static void window_destroy( pal_window_o *self ) {

	if ( self->mSurface ) {
		window_destroy_surface( self );
	}

	glfwDestroyWindow( self->window );

	delete self;
}

// ----------------------------------------------------------------------

static bool window_should_close( pal_window_o *self ) {
	return glfwWindowShouldClose( self->window );
}

// ----------------------------------------------------------------------

static GLFWwindow *window_get_glfw_window( pal_window_o *self ) {
	return self->window;
}

// ----------------------------------------------------------------------

static int init() {
	auto result = glfwInit();
	assert( result == GLFW_TRUE );

	if ( glfwVulkanSupported() ) {
		std::cout << "Vulkan supported." << std::endl;
	} else {
		std::cout << "Vulkan not supported." << std::endl;
	}

	return result;
}

// ----------------------------------------------------------------------

static const char **get_required_vk_instance_extensions( uint32_t *count ) {
	return glfwGetRequiredInstanceExtensions( count );
}

// ----------------------------------------------------------------------

static void pollEvents() {
	glfwPollEvents();
}

// ----------------------------------------------------------------------

static void terminate() {
	glfwTerminate();
	std::cout << "Glfw was terminated." << std::endl;
}

// ----------------------------------------------------------------------

void register_pal_window_api( void *api ) {

	auto windowApi = static_cast<pal_window_api *>( api );

	windowApi->init                       = init;
	windowApi->terminate                  = terminate;
	windowApi->pollEvents                 = pollEvents;
	windowApi->get_required_vk_extensions = get_required_vk_instance_extensions;

	auto &window_i                    = windowApi->window_i;
	window_i.create                   = window_create;
	window_i.destroy                  = window_destroy;
	window_i.should_close             = window_should_close;
	window_i.get_surface_width        = window_get_surface_width;
	window_i.get_surface_height       = window_get_surface_height;
	window_i.create_surface           = window_create_surface;
	window_i.destroy_surface          = window_destroy_surface;
	window_i.get_vk_surface_khr       = window_get_vk_surface_khr;
	window_i.increase_reference_count = window_increase_reference_count;
	window_i.decrease_reference_count = window_decrease_reference_count;
	window_i.get_reference_count      = window_get_reference_count;
	window_i.get_glfw_window          = window_get_glfw_window;

	window_i.set_callback_user_data       = window_set_callback_user_data;
	window_i.set_key_callback             = window_set_key_callback;
	window_i.set_character_callback       = window_set_character_callback;
	window_i.set_cursor_position_callback = window_set_cursor_position_callback;
	window_i.set_cursor_enter_callback    = window_set_cursor_enter_callback;
	window_i.set_mouse_button_callback    = window_set_mouse_button_callback;
	window_i.set_scroll_callback          = window_set_scroll_callback;

	auto &window_settings_i      = windowApi->window_settings_i;
	window_settings_i.create     = window_settings_create;
	window_settings_i.destroy    = window_settings_destroy;
	window_settings_i.set_title  = window_settings_set_title;
	window_settings_i.set_width  = window_settings_set_width;
	window_settings_i.set_height = window_settings_set_height;

	std::cout << "framebuffer resize callback addr: " << std::hex << ( void * )glfw_framebuffer_resize_callback << std::endl
	          << std::flush;

	Registry::loadLibraryPersistently( "libglfw.so" );
}
