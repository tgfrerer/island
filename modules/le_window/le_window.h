#ifndef GUARD_PAL_WINDOW_H
#define GUARD_PAL_WINDOW_H

#include <stdint.h>
#include "le_core.h"

struct VkInstance_T;
struct le_window_o;
struct le_window_settings_o;
struct VkSurfaceKHR_T;
struct GLFWwindow;

struct LeUiEvent; // declared in le_ui_event.h

// clang-format off
struct le_window_api {

	struct window_settings_interface_t {
		le_window_settings_o * ( *create     ) ();
		void                    ( *destroy    ) ( le_window_settings_o * );
		void                    ( *set_title  ) ( le_window_settings_o *, const char *title_ );
		void                    ( *set_width  ) ( le_window_settings_o *, int width_ );
		void                    ( *set_height ) ( le_window_settings_o *, int height_ );
	};

	struct window_interface_t {
		le_window_o *   ( *create             ) ( );
		void            ( *setup              ) ( le_window_o * self, const le_window_settings_o* );
		void            ( *destroy            ) ( le_window_o * self);

		void            ( *increase_reference_count )( le_window_o* self );
		void            ( *decrease_reference_count )( le_window_o* self );
		size_t          ( *get_reference_count      )( le_window_o* self );

		bool            ( *should_close       ) ( le_window_o * );
		VkSurfaceKHR_T* ( *create_surface     ) ( le_window_o *, VkInstance_T * );
		uint32_t        ( *get_surface_width  ) ( le_window_o * );
		uint32_t        ( *get_surface_height ) ( le_window_o * );
		GLFWwindow*     ( *get_glfw_window    ) ( le_window_o* self );

		void            ( *toggle_fullscreen  ) ( le_window_o* self );

		// Returns a sorted array of events pending for the current frame, and the number of events.
		// Note that calling this method invalidates any values returned in the previous call to this method.
		void            ( *get_ui_event_queue )(le_window_o* self, LeUiEvent const ** events, uint32_t& numEvents);

        // Return an OS-specific handle for the given window
        void *          ( *get_os_native_window_handle)(le_window_o* self);
	};

    struct window_callbacks_interface_t {
        void * glfw_key_callback_addr;
        void * glfw_char_callback_addr;
        void * glfw_cursor_pos_callback_addr;
        void * glfw_cursor_enter_callback_addr;
        void * glfw_mouse_button_callback_addr;
        void * glfw_scroll_callback_addr;
        void * glfw_framebuffer_size_callback_addr; 
        void * glfw_drop_callback_addr; 
    };

	int           ( *init                       ) ();
	void          ( *terminate                  ) ();
	void          ( *pollEvents                 ) ();
	void          ( *set_clipboard_string       ) (char const * str);
	char const*   ( *get_clipboard_string       ) ();

	const char ** ( *get_required_vk_instance_extensions ) ( uint32_t *count );


	window_interface_t           window_i;
	window_settings_interface_t  window_settings_i;
    window_callbacks_interface_t window_callbacks_i;
};
// clang-format on

LE_MODULE( le_window );
LE_MODULE_LOAD_DEFAULT( le_window );

#ifdef __cplusplus

namespace le_window {

static const auto& api        = le_window_api_i;
static const auto& window_i   = api->window_i;
static const auto& settings_i = api->window_settings_i;

} // namespace le_window

namespace le {

class Window : NoMove, NoCopy {
  public:
	class Settings {
		le_window_settings_o* self = nullptr;

	  public:
		Settings()
		    : self( le_window::settings_i.create() ) {
		}

		~Settings() {
			le_window::settings_i.destroy( self );
		}

		Settings& setWidth( int width_ ) {
			le_window::settings_i.set_width( self, width_ );
			return *this;
		}
		Settings& setHeight( int height_ ) {
			le_window::settings_i.set_height( self, height_ );
			return *this;
		}
		Settings& setTitle( const char* title_ ) {
			le_window::settings_i.set_title( self, title_ );
			return *this;
		}

		operator const le_window_settings_o*() const {
			return self;
		}
	};

  public:
	le_window_o* self = nullptr;

	Window()
	    : self( le_window::window_i.create() ) {
		le_window::window_i.increase_reference_count( self );
	}

	Window( le_window_o* ref )
	    : self( ref ) {
		le_window::window_i.increase_reference_count( self );
	}

	~Window() {
		le_window::window_i.decrease_reference_count( self );
		if ( le_window::window_i.get_reference_count( self ) == 0 ) {
			le_window::window_i.destroy( self );
		}
	}

	void setup( const Settings& settings = {} ) {
		le_window::window_i.setup( self, settings );
	}

	bool shouldClose() {
		return le_window::window_i.should_close( self );
	}

	void toggleFullscreen() {
		le_window::window_i.toggle_fullscreen( self );
	}

	void getUIEventQueue( LeUiEvent const** events, uint32_t& numEvents ) {
		le_window::window_i.get_ui_event_queue( self, events, numEvents );
	}

	operator auto() {
		return self;
	}

	static int init() {
		return le_window::api->init();
	}

	static void terminate() {
		le_window::api->terminate();
	}

	static void pollEvents() {
		le_window::api->pollEvents();
	}

	static const char** getRequiredVkExtensions( uint32_t* count ) {
		return le_window::api->get_required_vk_instance_extensions( count );
	}
};

} // namespace le

#endif // __cplusplus

#endif
