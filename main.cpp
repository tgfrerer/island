#include "pal_api_loader/ApiRegistry.hpp"
#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

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

	{

		// TODO: we need a way to easily add enabled device extensions
		// and to add easily to requestedExtensions.

		uint32_t numRequestedExtensions = 0;

		pal::Window::init();
		auto requestedExtensions = pal::Window::getRequiredVkExtensions( &numRequestedExtensions );

		pal::Window::Settings settings;
		settings
		    .setWidth ( 640 )
		    .setHeight( 480 )
		    .setTitle ( "Hello world" )
		    ;

		pal::Window window{settings};

		le::Instance instance{requestedExtensions, numRequestedExtensions};

		window.createSurface( instance.getVkInstance() );

		le::Device device{instance};

		le::Swapchain::Settings swapchainSettings;
		swapchainSettings
		    .setImageCountHint          ( 3 )
		    .setPresentModeHint         ( le::Swapchain::Presentmode::eFifo )
		    .setWidthHint               ( window.getSurfaceWidth() )
		    .setHeightHint              ( window.getSurfaceHeight() )
		    .setVkDevice                ( device.getVkDevice() )
		    .setVkPhysicalDevice        ( device.getVkPhysicalDevice() )
		    .setGraphicsQueueFamilyIndex( device.getDefaultGraphicsQueueFamilyIndex() )
		    .setVkSurfaceKHR            ( window.getVkSurfaceKHR() )
		    ;

		{
			// create swapchain, and attach it to window via the window's VkSurface
			le::Swapchain swapchain{swapchainSettings};

			// TODO: `swapchain.reset()` needs to run when surface has been lost -
			// Swapchain will report as such.
			//
			// Window will then have to re-acquire surface.
			// then swapchain must be reset.
			// swapchain.reset(swapchainSettings);

			for ( ; window.shouldClose() == false; ) {

				Registry::pollForDynamicReload();

				pal::Window::pollEvents();

				// app.update
				// app.draw

			}
		}
		window.destroySurface( instance.getVkInstance() );

		pal::Window::terminate();
	}

	return 0;
}
