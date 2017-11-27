#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <string>

#include <chrono>
#include <thread> // needed for sleep_for

#include "pal_api_loader/ApiRegistry.hpp"

// ----------------------------------------------------------------------

#include "traffic_light/traffic_light.h"
#include "logger/logger.h"
#include "GLFW/glfw3.h"

// ----------------------------------------------------------------------

#include "pal_window/pal_window.h"


// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {


#ifdef PLUGIN_TRAFFIC_LIGHT_STATIC
	Registry::addApiStatic<pal_traffic_light_api>();
#else
	Registry::addApiDynamic<pal_traffic_light_api>( true );
#endif

#ifdef PLUGIN_PAL_WINDOW_STATIC
	Registry::addApiStatic<pal_window_api>();
#else
	Registry::addApiDynamic<pal_window_api>( true );
#endif

	glfwInit();

	{
		pal::Window window{};

		for ( ; window.shouldClose() == false; ) {
			Registry::pollForDynamicReload();
			window.update();
			window.draw();
		}
	}

	glfwTerminate();
}
