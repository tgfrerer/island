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

struct LeUiEvent; // declared in le_ui_event.h

// clang-format off
struct pal_window_api {
	static constexpr auto id      = "pal_window";
	static constexpr auto pRegFun = register_pal_window_api;


	struct window_settings_interface_t {
		pal_window_settings_o * ( *create     ) ();
		void                    ( *destroy    ) ( pal_window_settings_o * );
		void                    ( *set_title  ) ( pal_window_settings_o *, const char *title_ );
		void                    ( *set_width  ) ( pal_window_settings_o *, int width_ );
		void                    ( *set_height ) ( pal_window_settings_o *, int height_ );
	};

	struct window_interface_t {
		pal_window_o *  ( *create             ) ( );
		void            ( *setup              ) ( pal_window_o * self, const pal_window_settings_o* );
		void            ( *destroy            ) ( pal_window_o * self);

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

		// Returns a sorted array of events pending for the current frame, and the number of events.
		// Note that calling this method invalidates any values returned in the previous call to this method.
		void            ( *get_ui_event_queue )(pal_window_o* self, LeUiEvent const ** events, uint32_t& numEvents);

	};

	int           ( *init                       ) ();
	void          ( *terminate                  ) ();
	void          ( *pollEvents                 ) ();

	const char ** ( *get_required_vk_extensions ) ( uint32_t *count );


	window_interface_t          window_i;
	window_settings_interface_t window_settings_i;
};
// clang-format on

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
  public:
	class Settings {
		pal_window_settings_o *self = nullptr;

	  public:
		Settings()
		    : self( pal_window::settings_i.create() ) {
		}

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

  public:
	pal_window_o *self = nullptr;

	Window()
	    : self( pal_window::window_i.create() ) {
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

	void setup( const Settings &settings = {} ) {
		pal_window::window_i.setup( self, settings );
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
