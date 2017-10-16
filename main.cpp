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

//#include "accum/accumulator.h"
#include "file_watcher/file_watcher.h"
#include "loader/ApiLoader.h"
#include "state_machine/state_machine.h"

#include <thread>

// ----------------------------------------------------------------------

struct LibData {
	api_loader_i *loaderApi;
	Loader *pLoader;
	const char *registry_func_name = nullptr;
	void *interface;
};

// ----------------------------------------------------------------------

void reloadLibrary( void *user_data ) {
	auto data = reinterpret_cast< LibData * >( user_data );

	std::cout << "Reload callback start" << std::endl;
	//	 reload library file
	data->loaderApi->load( data->pLoader );
	data->loaderApi->register_api( data->pLoader, data->interface, data->registry_func_name );
	std::cout << "Reload callback end" << std::endl;
}

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	api_loader_i loaderApi;
	register_api_loader_i( &loaderApi );

	file_watcher_i file_watcher;

#ifdef PLUGIN_FILE_WATCHER_STATIC
	std::cout << "using STATIC file watcher" << std::endl;
	register_file_watcher_api( &file_watcher );
#else
	std::cout << "using DYNAMIC file watcher" << std::endl;
	Loader *fileWatcherPlugin = loaderApi.create( ( "./file_watcher/libfile_watcher.so" ) );
	loaderApi.load( fileWatcherPlugin );
	loaderApi.register_api( fileWatcherPlugin, &file_watcher, "register_file_watcher_api" );
#endif

#ifdef PLUGIN_STATE_MACHINE_STATIC
#else
#endif

	Loader *stateMachinePlugin = loaderApi.create( ( "./state_machine/libstate_machine.so" ) );
	pal_state_machine_i stateMachineApi;

	LibData trafficLightLibData;
	trafficLightLibData.loaderApi          = &loaderApi;
	trafficLightLibData.pLoader            = stateMachinePlugin;
	trafficLightLibData.interface          = &stateMachineApi;
	trafficLightLibData.registry_func_name = "register_state_machine_api";

	//	loaderApi.load( stateMachinePlugin );
	//	loaderApi.register_api( stateMachinePlugin, &stateMachineApi );

	reloadLibrary( &trafficLightLibData );

	TrafficLight *trafficLight = stateMachineApi.create( );

	//	file_watcher_o *watched_file = file_watcher.create( "/tmp/hello.txt" );
	file_watcher_o *watched_file = file_watcher.create( "./state_machine/" );

	file_watcher.set_callback_function( watched_file, reloadLibrary, &trafficLightLibData );

	std::cout << "file watcher is watching path: " << file_watcher.get_path( watched_file ) << std::endl;

	for ( ;; ) {
		file_watcher.poll_notifications( watched_file );

		stateMachineApi.next_state( trafficLight );
		std::cout << "Traffic light: " << stateMachineApi.get_state_as_string( trafficLight ) << std::endl;
		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};

	// ----
}
