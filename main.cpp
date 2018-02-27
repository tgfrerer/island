#include "pal_api_loader/ApiRegistry.hpp"

#include "test_app/test_app.h"
#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

#ifdef PLUGIN_TEST_APP_STATIC
	Registry::addApiStatic<test_app_api>();
#else
	Registry::addApiDynamic<test_app_api>( true );
#endif

#ifdef PLUGIN_PAL_WINDOW_STATIC
	Registry::addApiStatic<pal_window_api>();
#else
	Registry::addApiDynamic<pal_window_api>( true );
#endif

#ifdef PLUGIN_LE_BACKEND_VK_STATIC
	Registry::addApiStatic<le_backend_vk_api>();
#else
	Registry::addApiDynamic<le_backend_vk_api>( true );
#endif

#ifdef PLUGIN_LE_SWAPCHAIN_VK_STATIC
	Registry::addApiStatic<le_swapchain_vk_api>();
#else
	Registry::addApiDynamic<le_swapchain_vk_api>( true );
#endif

#ifdef PLUGIN_LE_RENDERER_STATIC
	Registry::addApiStatic<le_renderer_api>();
#else
	Registry::addApiDynamic<le_renderer_api>( true );
#endif

	TestApp::initialize();

	{
		// We instantiate TestApp in its own scope - so that 
		// it will be destroyed before TestApp::terminate 
		// is called.
		
		TestApp testApp{};

		for ( ;; ) {
			Registry::pollForDynamicReload();

			auto result = testApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestApp is destroyed
	TestApp::terminate();

	return 0;
}
