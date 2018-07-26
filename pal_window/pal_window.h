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
		void            ( *set_key_callback            )(pal_window_o*, key_callback_fun_t* callback_fun_ptr); // NOTE: we want a pointer to a function pointer!
		void 		    ( *set_character_callback      )(pal_window_o*, character_callback_fun_t* callback_fun_ptr);
		void            ( *set_cursor_position_callback)(pal_window_o*, cursor_position_callback_fun_t* callback_fun_ptr);
		void            ( *set_cursor_enter_callback   )(pal_window_o*, cursor_enter_callback_fun_t* callback_fun_ptr);
		void            ( *set_mouse_button_callback   )(pal_window_o*, mouse_button_callback_fun_t* callback_fun_ptr);
		void            ( *set_scroll_callback         )(pal_window_o*, scroll_callback_fun_t* callback_fun_ptr);
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

namespace pal {

class Window : NoMove, NoCopy {
	const pal_window_api::window_interface_t &mWindow = Registry::getApi<pal_window_api>()->window_i;
	pal_window_o *                            self    = mWindow.create( nullptr );

  public:
	class Settings {
		const pal_window_api::window_settings_interface_t &windowSettingsI = Registry::getApi<pal_window_api>()->window_settings_i;
		pal_window_settings_o *                            self            = windowSettingsI.create();

	  public:
		Settings() = default;

		~Settings() {
			windowSettingsI.destroy( self );
		}

		Settings &setWidth( int width_ ) {
			windowSettingsI.set_width( self, width_ );
			return *this;
		}
		Settings &setHeight( int height_ ) {
			windowSettingsI.set_height( self, height_ );
			return *this;
		}
		Settings &setTitle( const char *title_ ) {
			windowSettingsI.set_title( self, title_ );
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
	    : self( mWindow.create( settings_ ) ) {
		mWindow.increase_reference_count( self );
	}

	Window( pal_window_o *ref )
	    : self( ref ) {
		mWindow.increase_reference_count( self );
	}

	~Window() {
		mWindow.decrease_reference_count( self );
		if ( mWindow.get_reference_count( self ) == 0 ) {
			mWindow.destroy( self );
		}
	}

	bool shouldClose() {
		return mWindow.should_close( self );
	}

	/// \brief create and store a vk surface in the current window object
	bool createSurface( VkInstance_T *instance ) {
		return mWindow.create_surface( self, instance );
	}

	uint32_t getSurfaceWidth() {
		return mWindow.get_surface_width( self );
	}

	uint32_t getSurfaceHeight() {
		return mWindow.get_surface_height( self );
	}

	VkSurfaceKHR_T *getVkSurfaceKHR() {
		return mWindow.get_vk_surface_khr( self );
	}

	void destroySurface() {
		mWindow.destroy_surface( self );
	}

	operator auto() {
		return self;
	}

	static int init() {
		static auto pApi = Registry::getApi<pal_window_api>();
		return pApi->init();
	}

	static void terminate() {
		static auto pApi = Registry::getApi<pal_window_api>();
		pApi->terminate();
	}

	static void pollEvents() {
		static auto pApi = Registry::getApi<pal_window_api>();
		pApi->pollEvents();
	}

	static const char **getRequiredVkExtensions( uint32_t *count ) {
		static auto pApi = Registry::getApi<pal_window_api>();
		return pApi->get_required_vk_extensions( count );
	}
};

} // namespace pal

#endif // __cplusplus

#endif
