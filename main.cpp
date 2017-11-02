#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <string>

#include <sys/inotify.h>
#include <dirent.h>
#include <unistd.h>

// ----------------------------------------------------------------------

#include "loader/ApiLoader.h"
#include "traffic_light/traffic_light.h"

#include <thread>

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	api_loader_i loaderInterface;
	register_api_loader_i( &loaderInterface );

	pal_traffic_light_i trafficLightInterface;

#ifdef PLUGIN_TRAFFIC_LIGHT_STATIC
	std::cout << "using STATIC traffic light module " << std::endl;
	loaderInterface.register_static_api( "traffic_light", register_traffic_light_api, &trafficLightInterface );
#else

	api_loader_o* loader = loaderInterface.create("./traffic_light/libtraffic_light.so");

	std::cout << "using DYNAMIC traffic light module" << std::endl;
	pal::ApiLoader trafficLightPlugin( &loaderInterface,
	                                   &trafficLightInterface,
	                                   "./traffic_light/libtraffic_light.so",
	                                   "register_traffic_light_api" );
	trafficLightPlugin.loadLibrary();
#endif

	pal::TrafficLight trafficLight( &trafficLightInterface );

//	interfaceA = loader.load("interface_name", pal::interfaceType::eStatic);
//	loader.pollLibraries();

	for ( ;; ) {

		//trafficLightPlugin.checkReload();

		trafficLight.step();

		std::cout << "Traffic light: " << trafficLight.getStateAsString() << std::endl;

		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};

	// ----
}
