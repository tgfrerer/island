#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <string>

#include <chrono>
#include <thread> // needed for sleep_for

#include "registry/ApiRegistry.hpp"

// ----------------------------------------------------------------------

#include "traffic_light/traffic_light.h"
#include "logger/logger.h"
#include "GLFW/glfw3.h"

// ----------------------------------------------------------------------

#include "pal_window/pal_window.h"

// ----------------------------------------------------------------------

void test_traffic_light() {
#ifdef PLUGIN_TRAFFIC_LIGHT_STATIC
	Registry::addApiStatic<pal_traffic_light_i>();
#else
	Registry::addApiDynamic<pal_traffic_light_api>( true );
#endif

#ifdef PLUGIN_LOGGER_STATIC
	Registry::addApiStatic<pal_logger_i>();
#else
	Registry::addApiDynamic<pal_logger_api>( true );
#endif

	pal::TrafficLight trafficLight{};

	for ( ;; ) {

		Registry::pollForDynamicReload();

		trafficLight.step();

		pal::Logger() << trafficLight.getStateAsString();

		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};
}

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {
	// test_traffic_light();

#ifdef PLUGIN_TRAFFIC_LIGHT_STATIC
	Registry::addApiStatic<pal_traffic_light_i>();
#else
	Registry::addApiDynamic<pal_traffic_light_api>( true );
#endif

	Registry::addApiDynamic<pal_window_api>( true );

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
