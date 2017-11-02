#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <string>

#include <sys/inotify.h>
#include <dirent.h>
#include <unistd.h>

#include "registry/ApiRegistry.hpp"

// ----------------------------------------------------------------------

#include "loader/ApiLoader.h"
#include "traffic_light/traffic_light.h"

#include <thread>

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	pal_api_loader_i loaderInterface;
	pal_register_api_loader_i( &loaderInterface );

	auto trafficLightInterface = Registry::addApi<pal_traffic_light_i>();

	constexpr auto name_of_tl = Registry::getId<pal_traffic_light_i>();

	std::cout << name_of_tl << std::endl;

#ifdef PLUGIN_TRAFFIC_LIGHT_STATIC
	std::cout << "using STATIC traffic light module " << std::endl;
	loaderInterface.register_static_api(register_traffic_light_api, trafficLightInterface );
#else

	pal_api_loader_o *loader = loaderInterface.create( "./traffic_light/libtraffic_light.so" );

	std::cout << "using DYNAMIC traffic light module" << std::endl;
	pal::ApiLoader trafficLightPlugin( &loaderInterface,
	                                   trafficLightInterface,
	                                   "./traffic_light/libtraffic_light.so",
	                                   "register_traffic_light_api" );
	trafficLightPlugin.loadLibrary();
#endif

	pal::TrafficLight trafficLight( Registry::getApi<pal_traffic_light_i>() );

	//	interfaceA = loader.load("interface_name", pal::interfaceType::eStatic);
	//	loader.pollLibraries();

	for ( ;; ) {

#ifndef PLUGIN_TRAFFIC_LIGHT_STATIC
		trafficLightPlugin.checkReload();
#endif

		trafficLight.step();
		auto name_of_tl = Registry::getId<pal_traffic_light_i>();
		std::cout << name_of_tl << std::endl;

		std::cout << "Traffic light: " << trafficLight.getStateAsString() << std::endl;

		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};


}
