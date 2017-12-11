#include "pal_api_loader/ApiRegistry.hpp"
#include "pal_window/pal_window.h"
#include "pal_backend_vk/pal_backend_vk.h"
// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

#ifdef PLUGIN_PAL_WINDOW_STATIC
	Registry::addApiStatic<pal_window_api>();
#else
	Registry::addApiDynamic<pal_window_api>( true );
#endif

#ifdef PLUGIN_PAL_BACKEND_VK_STATIC
	Registry::addApiStatic<pal_backend_vk_api>( );
#else
	Registry::addApiDynamic<pal_backend_vk_api>( true );
#endif

	{

		pal::Window::init();
		pal::Window window{};

		// todo: feed backend list of required extensions coming from glfw
		pal::vk::Instance mBackend;

		// the window must create a surface - and it can only create a surface
		// by using the backend
		// the window will own the surface
		//window.createSurface(mBackend);

		for ( ; window.shouldClose() == false; ) {

			Registry::pollForDynamicReload();

			pal::Window::pollEvents();
			window.update();
			window.draw();
		}

		// todo: implement
		//window.destroySurface(mBackend);
		pal::Window::terminate();
	}

	return 0;
}
