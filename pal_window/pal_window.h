#ifndef GUARD_PAL_WINDOW_H
#define GUARD_PAL_WINDOW_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_pal_window_api( void *api );

struct VkInstance_T;
struct pal_window_o;
struct pal_window_settings_o;
struct VkSurfaceKHR_T;
struct GLFWwindow;

struct pal_window_api {
	static constexpr auto id      = "pal_window";
	static constexpr auto pRegFun = register_pal_window_api;

	typedef void ( *key_callback_fun_t )( void *user_data, int key, int scancode, int action, int mods );
	typedef void ( *character_callback_fun_t )( void *user_data, unsigned int codepoint );
	typedef void ( *cursor_position_callback_fun_t )( void *user_data, double xpos, double ypos );
	typedef void ( *cursor_enter_callback_fun_t )( void *user_data, int entered );
	typedef void ( *mouse_button_callback_fun_t )( void *user_data, int button, int action, int mods );
	typedef void ( *scroll_callback_fun_t )( void *user_data, double xoffset, double yoffset );

	// clang-format off

	struct window_settings_interface_t {
		pal_window_settings_o * ( *create     ) ();
		void                    ( *destroy    ) ( pal_window_settings_o * );
		void                    ( *set_title  ) ( pal_window_settings_o *, const char *title_ );
		void                    ( *set_width  ) ( pal_window_settings_o *, int width_ );
		void                    ( *set_height ) ( pal_window_settings_o *, int height_ );
	};

	struct window_interface_t {
		pal_window_o *  ( *create             ) ( const pal_window_settings_o* );
		void            ( *destroy            ) ( pal_window_o * );

		void            ( *increase_reference_count )( pal_window_o* self );
		void            ( *decrease_reference_count )( pal_window_o* self );
		size_t          ( *get_reference_count      )( pal_window_o* self );

		bool            ( *should_close       ) ( pal_window_o * );
		bool            ( *create_surface     ) ( pal_window_o *, VkInstance_T * );
		void            ( *destroy_surface    ) ( pal_window_o * );
		uint32_t        ( *get_surface_width  ) ( pal_window_o * );
		uint32_t        ( *get_surface_height ) ( pal_window_o * );
		VkSurfaceKHR_T* ( *get_vk_surface_khr ) ( pal_window_o * );
		GLFWwindow*     ( *get_glfw_window    ) ( pal_window_o* self );

		void            ( *toggle_fullscreen  ) ( pal_window_o* self );

		void            ( *set_callback_user_data      )(pal_window_o* self, void* user_data);
		void            ( *set_key_callback            )(pal_window_o*, key_callback_fun_t const * callback_fun_ptr); // NOTE: we want a pointer to a function pointer!
		void 		    ( *set_character_callback      )(pal_window_o*, character_callback_fun_t const * callback_fun_ptr);
		void            ( *set_cursor_position_callback)(pal_window_o*, cursor_position_callback_fun_t const * callback_fun_ptr);
		void            ( *set_cursor_enter_callback   )(pal_window_o*, cursor_enter_callback_fun_t const * callback_fun_ptr);
		void            ( *set_mouse_button_callback   )(pal_window_o*, mouse_button_callback_fun_t const *  callback_fun_ptr);
		void            ( *set_scroll_callback         )(pal_window_o*, scroll_callback_fun_t const * callback_fun_ptr);
	};

	int           ( *init                       ) ();
	void          ( *terminate                  ) ();
	void          ( *pollEvents                 ) ();
	const char ** ( *get_required_vk_extensions ) ( uint32_t *count );

	// clang-format on

	window_interface_t          window_i;
	window_settings_interface_t window_settings_i;
};

#ifdef __cplusplus
} // extern "C"

namespace pal_window {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<pal_window_api>( true );
#	else
const auto api = Registry::addApiStatic<pal_window_api>();
#	endif

static const auto &window_i   = api -> window_i;
static const auto &settings_i = api -> window_settings_i;

} // namespace pal_window

namespace pal {

class Window : NoMove, NoCopy {
	pal_window_o *self = pal_window::window_i.create( nullptr );

  public:
	class Settings {
		pal_window_settings_o *self = pal_window::settings_i.create();

	  public:
		Settings() = default;

		~Settings() {
			pal_window::settings_i.destroy( self );
		}

		Settings &setWidth( int width_ ) {
			pal_window::settings_i.set_width( self, width_ );
			return *this;
		}
		Settings &setHeight( int height_ ) {
			pal_window::settings_i.set_height( self, height_ );
			return *this;
		}
		Settings &setTitle( const char *title_ ) {
			pal_window::settings_i.set_title( self, title_ );
			return *this;
		}

		operator const pal_window_settings_o *() const {
			return self;
		}
	};

  private:
	// deactivate default constructor
	Window() = delete;

  public:
	Window( const Settings &settings_ )
	    : self( pal_window::window_i.create( settings_ ) ) {
		pal_window::window_i.increase_reference_count( self );
	}

	Window( pal_window_o *ref )
	    : self( ref ) {
		pal_window::window_i.increase_reference_count( self );
	}

	~Window() {
		pal_window::window_i.decrease_reference_count( self );
		if ( pal_window::window_i.get_reference_count( self ) == 0 ) {
			pal_window::window_i.destroy( self );
		}
	}

	bool shouldClose() {
		return pal_window::window_i.should_close( self );
	}

	/// \brief create and store a vk surface in the current window object
	bool createSurface( VkInstance_T *instance ) {
		return pal_window::window_i.create_surface( self, instance );
	}

	uint32_t getSurfaceWidth() {
		return pal_window::window_i.get_surface_width( self );
	}

	uint32_t getSurfaceHeight() {
		return pal_window::window_i.get_surface_height( self );
	}

	VkSurfaceKHR_T *getVkSurfaceKHR() {
		return pal_window::window_i.get_vk_surface_khr( self );
	}

	void destroySurface() {
		pal_window::window_i.destroy_surface( self );
	}

	operator auto() {
		return self;
	}

	static int init() {
		return pal_window::api->init();
	}

	static void terminate() {
		pal_window::api->terminate();
	}

	static void pollEvents() {
		pal_window::api->pollEvents();
	}

	static const char **getRequiredVkExtensions( uint32_t *count ) {
		return pal_window::api->get_required_vk_extensions( count );
	}
};

} // namespace pal

#endif // __cplusplus

#endif
