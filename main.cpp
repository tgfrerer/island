#include "pal_api_loader/ApiRegistry.hpp"
#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

#ifdef PLUGIN_PAL_WINDOW_STATIC
	Registry::addApiStatic<pal_window_api>();
#else
	Registry::addApiDynamic<pal_window_api>( true );
#endif

#ifdef PLUGIN_PAL_BACKEND_VK_STATIC
	Registry::addApiStatic<pal_backend_vk_api>();
#else
	Registry::addApiDynamic<le_backend_vk_api>( true );
#endif

	{
		uint32_t numRequestedExtensions = 0;

		pal::Window::init();
		auto requestedExtensions = pal::Window::getRequiredVkExtensions(&numRequestedExtensions);
		pal::Window window{};

		le::Backend mBackend( requestedExtensions, numRequestedExtensions );

		// The window must create a surface - and it can only create a surface
		// by using the backend
		window.createSurface( mBackend.getVkInstance() );

		//backend.createSwapchain(window.getSurface);

		for ( ; window.shouldClose() == false; ) {

			Registry::pollForDynamicReload();

			pal::Window::pollEvents();
			window.update();
			window.draw();
		}

		window.destroySurface( mBackend.getVkInstance() );
		pal::Window::terminate();
	}

	return 0;
}
