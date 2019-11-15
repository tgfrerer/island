#include "pal_window/pal_window.h"
#include "le_ui_event/le_ui_event.h"

#include "assert.h"
#include <iostream>
#include <vector>
#include <array>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include <vector>
#include <atomic>

constexpr size_t EVENT_QUEUE_SIZE = 100; // Only allocate space for 100 events per-frame

struct pal_window_settings_o {
	int          width          = 640;
	int          height         = 480;
	std::string  title          = "Island default window title";
	GLFWmonitor *monitor        = nullptr;
	uint32_t     useEventsQueue = true; // whether to use an events queue or not
};

struct WindowGeometry {
	int x      = 0;
	int y      = 0;
	int width  = 0;
	int height = 0;
};

struct pal_window_o {

	GLFWwindow *          window   = nullptr;
	VkSurfaceKHR          mSurface = nullptr;
	VkExtent2D            mSurfaceExtent{};
	pal_window_settings_o mSettings{};
	size_t                referenceCount = 0;
	void *                user_data      = nullptr;

	uint32_t                                               eventQueueBack = 0;      // Event queue currently used to record events
	std::array<std::atomic<uint32_t>, 2>                   numEventsForQueue{0, 0}; // Counter for events per queue (works as arena allocator marker for events queue)
	std::array<std::array<LeUiEvent, EVENT_QUEUE_SIZE>, 2> eventQueue;              // Events queue is double-bufferd, flip happens on `get_event_queue`

	WindowGeometry windowGeometry{};
	bool           isFullscreen = false;
};

// ----------------------------------------------------------------------
// Check if there is an available index to write at given an event counter and sets
// eventIdx as a side-effect.
// Returns false if no index in the queue is available for writing.
// Returns true and an available index as a side-effect in eventIdx.
//
// Note that we limit the return value `eventIdx` to EVENT_QUEUE_SIZE-1
bool event_queue_idx_available( std::atomic<uint32_t> &atomicCounter, uint32_t &eventIdx ) {

	// We post-increment here, so that eventIdx receives the current value of the atomic
	// counter, just before the counter gets incremented.
	eventIdx = atomicCounter++;

	if ( eventIdx >= EVENT_QUEUE_SIZE ) {
		eventIdx = atomicCounter = EVENT_QUEUE_SIZE - 1;
		return false;
	} else {
		return true;
	}
}

// ----------------------------------------------------------------------
static void glfw_window_key_callback( GLFWwindow *glfwWindow, int key, int scancode, int action, int mods ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->mSettings.useEventsQueue ) {

		uint32_t queueIdx = window->eventQueueBack;
		uint32_t eventIdx = 0;

		if ( event_queue_idx_available( window->numEventsForQueue[ queueIdx ], eventIdx ) ) {
			auto &event = window->eventQueue[ queueIdx ][ eventIdx ];
			event.event = LeUiEvent::Type::eKey;
			auto &e     = event.key;
			e.key       = LeUiEvent::NamedKey( key );
			e.action    = LeUiEvent::ButtonAction( action );
			e.scancode  = scancode;
			e.mods      = mods;
		} else {
			// we're over the high - watermark for events, we should probably print a warning.
		}
	}
}

// ----------------------------------------------------------------------
static void glfw_window_character_callback( GLFWwindow *glfwWindow, unsigned int codepoint ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->mSettings.useEventsQueue ) {

		uint32_t queueIdx = window->eventQueueBack;
		uint32_t eventIdx = 0;

		if ( event_queue_idx_available( window->numEventsForQueue[ queueIdx ], eventIdx ) ) {
			auto &event = window->eventQueue[ queueIdx ][ eventIdx ];
			event.event = LeUiEvent::Type::eCharacter;
			auto &e     = event.character;
			e.codepoint = codepoint;
		} else {
			// we're over the high - watermark for events, we should probably print a warning.
		}
	}
}

// ----------------------------------------------------------------------
static void glfw_window_cursor_position_callback( GLFWwindow *glfwWindow, double xpos, double ypos ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->mSettings.useEventsQueue ) {

		uint32_t queueIdx = window->eventQueueBack;
		uint32_t eventIdx = 0;

		if ( event_queue_idx_available( window->numEventsForQueue[ queueIdx ], eventIdx ) ) {
			auto &event = window->eventQueue[ queueIdx ][ eventIdx ];
			event.event = LeUiEvent::Type::eCursorPosition;
			auto &e     = event.cursorPosition;
			e.x         = xpos;
			e.y         = ypos;
		} else {
			// we're over the high - watermark for events, we should probably print a warning.
		}
	}
}

// ----------------------------------------------------------------------
static void glfw_window_cursor_enter_callback( GLFWwindow *glfwWindow, int entered ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->mSettings.useEventsQueue ) {

		uint32_t queueIdx = window->eventQueueBack;
		uint32_t eventIdx = 0;

		if ( event_queue_idx_available( window->numEventsForQueue[ queueIdx ], eventIdx ) ) {
			auto &event = window->eventQueue[ queueIdx ][ eventIdx ];
			event.event = LeUiEvent::Type::eCursorEnter;
			auto &e     = event.cursorEnter;
			e.entered   = uint32_t( entered );
		} else {
			// we're over the high - watermark for events, we should probably print a warning.
		}
	}
}

// ----------------------------------------------------------------------
static void glfw_window_mouse_button_callback( GLFWwindow *glfwWindow, int button, int action, int mods ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->mSettings.useEventsQueue ) {

		uint32_t queueIdx = window->eventQueueBack;
		uint32_t eventIdx = 0;

		if ( event_queue_idx_available( window->numEventsForQueue[ queueIdx ], eventIdx ) ) {
			auto &event = window->eventQueue[ queueIdx ][ eventIdx ];
			event.event = LeUiEvent::Type::eMouseButton;
			auto &e     = event.mouseButton;
			e.button    = button;
			e.action    = le::UiEvent::ButtonAction( action );
			e.mods      = mods;
		} else {
			// we're over the high - watermark for events, we should probably print a warning.
		}
	}
}

// ----------------------------------------------------------------------
static void glfw_window_scroll_callback( GLFWwindow *glfwWindow, double xoffset, double yoffset ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	if ( window->mSettings.useEventsQueue ) {

		uint32_t queueIdx = window->eventQueueBack;
		uint32_t eventIdx = 0;

		if ( event_queue_idx_available( window->numEventsForQueue[ queueIdx ], eventIdx ) ) {
			auto &event = window->eventQueue[ queueIdx ][ eventIdx ];
			event.event = LeUiEvent::Type::eScroll;
			auto &e     = event.scroll;
			e.x_offset  = xoffset;
			e.y_offset  = yoffset;
		} else {
			// we're over the high - watermark for events, we should probably print a warning.
		}
	}
}

// ----------------------------------------------------------------------
static void glfw_framebuffer_resize_callback( GLFWwindow *glfwWindow, int width_px, int height_px ) {

	auto window = static_cast<pal_window_o *>( glfwGetWindowUserPointer( glfwWindow ) );

	int w = width_px;
	int h = height_px;

	glfwGetFramebufferSize( glfwWindow, &w, &h );

	window->mSurfaceExtent.width  = uint32_t( w );
	window->mSurfaceExtent.height = uint32_t( h );

	//	std::cout << "\t* Framebuffer resized callback: " << std::dec << w << ", " << h << std::endl
	//	          << std::flush;
};

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

static bool pt2_inside_rect( int x, int y, int left, int top, int width, int height ) {
	return ( x > left &&
	         x < ( left + width ) &&
	         y > top &&
	         y < ( top + height ) );
}

// ----------------------------------------------------------------------

// Goes fullscreen (or returns to windowed mode) on the monitor which contains
// the current window.
//
// If more than one monitor is available, the monitor which contains the current
// window's centre receives the fullscreen window.
//
static void window_toggle_fullscreen( pal_window_o *self ) {

	auto &g = self->windowGeometry;

	if ( self->isFullscreen ) {
		//restore previous window state

		glfwSetWindowMonitor( self->window, nullptr, g.x, g.y, g.width, g.height, 0 );

		self->isFullscreen = false;
	} else {
		// note that monitor vidmode width and window width and height are both given in screen coordinates
		// screen coordinates have y go from top to bottom (y-axis points down, origin is at the top left)

		// First store current window geometry to restore state later.
		glfwGetWindowPos( self->window, &g.x, &g.y );
		glfwGetWindowSize( self->window, &g.width, &g.height );

		int                 monitorCount      = 0;
		GLFWmonitor **      monitors          = glfwGetMonitors( &monitorCount );
		GLFWmonitor **const monitors_end      = monitors + monitorCount;
		GLFWmonitor *       fullscreenMonitor = *monitors;

		// Iterate over all monitors, and find out if our window is inside any of them.
		// If so, that monitor will be our fullscreen monitor.

		for ( auto monitor = monitors; monitor != monitors_end; monitor++ ) {

			int x_m{};
			int y_m{};
			glfwGetMonitorPos( *monitor, &x_m, &y_m );

			// Get monitor extents (in screen corrds) by querying its GLFWvidmode
			GLFWvidmode const *mode = glfwGetVideoMode( *monitor );

			// Check if the current window's centre point is inside the monitor in question.
			// If yes, we have found our target monitor for going fullscreen.

			if ( pt2_inside_rect( g.x + g.width / 2, g.y + g.height / 2, x_m, y_m, mode->width, mode->height ) ) {
				fullscreenMonitor = *monitor;
				break;
			}
		}

		auto videoMode = glfwGetVideoMode( fullscreenMonitor );

		glfwSetWindowMonitor( self->window, fullscreenMonitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate );
		self->isFullscreen = true;
	}
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
// Creates a khr surface using glfw - note that ownership is handed over to
// the caller which must outlive this pal_window_o, and take responsibility
// of deleting  the surface.
static VkSurfaceKHR_T *window_create_surface( pal_window_o *self, VkInstance vkInstance ) {
	auto result = glfwCreateWindowSurface( vkInstance, self->window, nullptr, &self->mSurface );
	if ( result == VK_SUCCESS ) {
		int tmp_w = 0;
		int tmp_h = 0;
		glfwGetFramebufferSize( self->window, &tmp_w, &tmp_h );
		self->mSurfaceExtent.height = uint32_t( tmp_h );
		self->mSurfaceExtent.width  = uint32_t( tmp_w );
		std::cout << "Created surface" << std::endl;
	} else {
		std::cerr << "Error creating surface" << std::endl;
		return nullptr;
	}
	return self->mSurface;
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

// ----------------------------------------------------------------------

static void window_set_callbacks( pal_window_o *self ) {

	// Note: Callback function address target may have changed after library hot-reload
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

static void window_remove_callbacks( pal_window_o *self ) {

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

	glfwSetKeyCallback( self->window, nullptr );
	glfwSetCharCallback( self->window, nullptr );
	glfwSetCursorPosCallback( self->window, nullptr );
	glfwSetCursorEnterCallback( self->window, nullptr );
	glfwSetMouseButtonCallback( self->window, nullptr );
	glfwSetScrollCallback( self->window, nullptr );
	glfwSetFramebufferSizeCallback( self->window, nullptr );
}

// ----------------------------------------------------------------------
// Returns an array of events pending since the last call to this method.
// Note that calling this method invalidates any values returned from the previous call to this method.
static void window_get_ui_event_queue( pal_window_o *self, LeUiEvent const **events, uint32_t &numEvents ) {

	if ( false == self->mSettings.useEventsQueue ) {
		*events   = nullptr;
		numEvents = 0;

		std::cout << "WARNING: Querying ui event queue when event queue not in use. Use window.settings to enable events queue." << std::endl
		          << std::flush;
		return;
	}

	// ----------| Invariant: Event queue is in use.

	// Flip front and back event queue
	auto eventQueueFront = self->eventQueueBack;
	self->eventQueueBack ^= 1;

	// Note: In the unlikely event that any LeUiEvent will be added asynchronously in between
	// these two calls it will be added to the very end of the back queue and then implicitly
	// released, as the event counter for the back queue is reset at the next step.
	// This is not elegant, but otherwise we must mutex for all event callbacks...

	// Clear new back event queue
	self->numEventsForQueue[ self->eventQueueBack ] = 0;

	// Hand out front events queue
	*events   = self->eventQueue[ eventQueueFront ].data();
	numEvents = self->numEventsForQueue[ eventQueueFront ];
}

// ----------------------------------------------------------------------

static pal_window_o *window_create() {
	auto obj = new pal_window_o();
	return obj;
}

// ----------------------------------------------------------------------

static void window_setup( pal_window_o *self, const pal_window_settings_o *settings ) {
	if ( settings ) {
		self->mSettings = *settings;
	}

	// TODO: implement GLFW window hints, based on settings.
	// See: http://www.glfw.org/docs/latest/window_guide.html#window_hints
	glfwWindowHint( GLFW_FLOATING, GLFW_TRUE ); // < window is created so that it is always on top.
	glfwWindowHint( GLFW_VISIBLE, GLFW_FALSE ); // < window is initially not visible
	glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );

	self->window = glfwCreateWindow( self->mSettings.width, self->mSettings.height, self->mSettings.title.c_str(), self->mSettings.monitor, nullptr );

#ifndef NDEBUG

	int           monitorCount = 0;
	GLFWmonitor **monitors     = glfwGetMonitors( &monitorCount );

	int windowX = 100;
	int windowY = 100;

	if ( monitorCount > 1 ) {
		// if there is more than one monitor, we want our window to appear on the secondary monitor by default.
		glfwGetMonitorPos( monitors[ 1 ], &windowX, &windowY );
	}

	glfwSetWindowPos( self->window, windowX, windowY );

#endif

	glfwShowWindow( self->window );

	// Set the user pointer so callbacks know which window they belong to
	glfwSetWindowUserPointer( self->window, self );

	window_set_callbacks( self );
}

// ----------------------------------------------------------------------

static void window_destroy( pal_window_o *self ) {

	if ( self->window ) {
		window_remove_callbacks( self );
	}

	if ( self->window ) {
		glfwDestroyWindow( self->window );
	}

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
// Polls Window events via GLFW - this will trigger glfw callbacks for
// any events raised via glfw. Since this triggers polling for any window
// associated with this glfw instance, we must find a way to communicate
// to all windows that their event queue is stale at this moment.
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

	windowApi->init                                = init;
	windowApi->terminate                           = terminate;
	windowApi->pollEvents                          = pollEvents;
	windowApi->get_required_vk_instance_extensions = get_required_vk_instance_extensions;

	auto &window_i                    = windowApi->window_i;
	window_i.create                   = window_create;
	window_i.destroy                  = window_destroy;
	window_i.setup                    = window_setup;
	window_i.should_close             = window_should_close;
	window_i.get_surface_width        = window_get_surface_width;
	window_i.get_surface_height       = window_get_surface_height;
	window_i.create_surface           = window_create_surface;
	window_i.increase_reference_count = window_increase_reference_count;
	window_i.decrease_reference_count = window_decrease_reference_count;
	window_i.get_reference_count      = window_get_reference_count;
	window_i.get_glfw_window          = window_get_glfw_window;

	window_i.toggle_fullscreen  = window_toggle_fullscreen;
	window_i.get_ui_event_queue = window_get_ui_event_queue;

	auto &window_settings_i      = windowApi->window_settings_i;
	window_settings_i.create     = window_settings_create;
	window_settings_i.destroy    = window_settings_destroy;
	window_settings_i.set_title  = window_settings_set_title;
	window_settings_i.set_width  = window_settings_set_width;
	window_settings_i.set_height = window_settings_set_height;

	Registry::loadLibraryPersistently( "libglfw.so" );
}
