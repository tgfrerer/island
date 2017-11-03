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


// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

#ifdef PLUGIN_TRAFFIC_LIGHT_STATIC
	Registry::addApiStatic<pal_traffic_light_i>();
#else
	Registry::addApiDynamic<pal_traffic_light_i>(true);
#endif

	pal::TrafficLight trafficLight( Registry::getApi<pal_traffic_light_i>() );

	for ( ;; ) {

		Registry::pollForDynamicReload();

		trafficLight.step();
		std::cout << trafficLight.getStateAsString() << std::endl;
		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};
}
