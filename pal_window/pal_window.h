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
		pal_window_o *  ( *create             ) ( const pal_window_settings_o* );
		void            ( *destroy            ) ( pal_window_o * );
		bool            ( *should_close       ) ( pal_window_o * );
		void            ( *update             ) ( pal_window_o * );
		void            ( *draw               ) ( pal_window_o * );
		bool            ( *create_surface     ) ( pal_window_o *, VkInstance_T * );
		void            ( *destroy_surface    ) ( pal_window_o *, VkInstance_T * );
		VkSurfaceKHR_T* ( *get_vk_surface_khr ) ( pal_window_o * );
	};

	int           ( *init                       ) ();
	void          ( *terminate                  ) ();
	void          ( *pollEvents                 ) ();
	const char ** ( *get_required_vk_extensions ) ( uint32_t *count );

	window_interface_t window_i;
	window_settings_interface_t window_settings_i;
};

#ifdef __cplusplus
} // extern "C"


namespace pal {

class Window {
	pal_window_api::window_interface_t const &mWindow;
	pal_window_o *                            self;

  public:
	class Settings {
		pal_window_api::window_settings_interface_t const &windowSettingsI;
		pal_window_settings_o *                            self;

	  public:
		Settings()
		    : windowSettingsI( Registry::getApi<pal_window_api>()->window_settings_i )
		    , self( windowSettingsI.create() ) {
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
	// Note this class disables copy constructor and copy assignment operator,

	// Also, this class disables move operators, as a move will trigger the
	// destructor, and we hijack the destructor to print to the log.

	// copy assignment operator
	Window &operator=( const Window &rhs ) = delete;

	// copy constructor
	Window( const Window &rhs ) = delete;

	// move assignment operator
	Window &operator=( Window &&rhs ) = delete;

	// move constructor
	Window( const Window &&rhs ) = delete;

  public:
	// default constructor
	Window()
	    : mWindow( Registry::getApi<pal_window_api>()->window_i )
	    , self( mWindow.create(nullptr) ) {
	}

	Window(const Settings& settings_)
	    : mWindow( Registry::getApi<pal_window_api>()->window_i )
	    , self( mWindow.create(settings_) ) {
	}

	~Window() {
		mWindow.destroy( self );
	}

	bool shouldClose() {
		return mWindow.should_close( self );
	}

	void update() {
		mWindow.update( self );
	}

	void draw() {
		mWindow.draw( self );
	}

	/// \brief create and store a vk surface in the current window object
	bool createSurface( VkInstance_T *instance ) {
		return mWindow.create_surface( self, instance );
	}

	VkSurfaceKHR_T* getVkSurfaceKHR(){
		return mWindow.get_vk_surface_khr(self);
	}

	void destroySurface( VkInstance_T *instance ) {
		mWindow.destroy_surface( self, instance );
	}

	operator pal_window_o*(){
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

	static void pollEvents(){
		static auto pApi = Registry::getApi<pal_window_api>();
		pApi->pollEvents();
	}

	static const char** getRequiredVkExtensions(uint32_t* count){
		static auto pApi = Registry::getApi<pal_window_api>();
		pApi->get_required_vk_extensions(count);
	}
};

} // namespace pal

#endif // __cplusplus

#endif
