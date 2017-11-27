#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <string>

#include <chrono>
#include <thread> // needed for sleep_for

#include "pal_api_loader/ApiRegistry.hpp"

// ----------------------------------------------------------------------

#include "pal_window/pal_window.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

#ifdef PLUGIN_PAL_WINDOW_STATIC
	Registry::addApiStatic<pal_window_api>();
#else
	Registry::addApiDynamic<pal_window_api>( true );
#endif

	{
		pal::Window::init();

		pal::Window window{};

		for ( ; window.shouldClose() == false; ) {
			Registry::pollForDynamicReload();
			window.update();
			window.draw();
		}

		pal::Window::terminate();
	}

	return 0;
}
