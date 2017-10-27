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
	Loader *      pLoader;
	const char *  api_register_fun_name = nullptr;
	void *        api;
};

// ----------------------------------------------------------------------

void reloadLibrary( void *user_data ) {
	auto data = reinterpret_cast<LibData *>( user_data );

	std::cout << "Reload callback start" << std::endl;
	//	 reload library file
	data->loaderApi->load( data->pLoader );
	data->loaderApi->register_api( data->pLoader, data->api, data->api_register_fun_name );
	std::cout << "Reload callback end" << std::endl;
}

// ----------------------------------------------------------------------



// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	api_loader_i loaderInterface;
	register_api_loader_i( &loaderInterface );

	file_watcher_i file_watcher;

#ifdef PLUGIN_FILE_WATCHER_STATIC
	std::cout << "using STATIC file watcher" << std::endl;
	register_file_watcher_api( &file_watcher );
#else
	std::cout << "using DYNAMIC file watcher" << std::endl;
	Loader *fileWatcherPlugin = loaderInterface.create( ( "./file_watcher/libfile_watcher.so" ) );
	loaderInterface.load( fileWatcherPlugin );
	loaderInterface.register_api( fileWatcherPlugin, &file_watcher, "register_file_watcher_api" );
#endif

	pal_state_machine_i iTrafficLight;

#ifdef PLUGIN_STATE_MACHINE_STATIC
	std::cout << "using STATIC state machine module " << std::endl;
	register_state_machine_api( &stateMachineApi );
#else
	std::cout << "using DYNAMIC state machine module" << std::endl;
	Loader *stateMachinePluginLoader = loaderInterface.create( ( "./state_machine/libstate_machine.so" ) );
	loaderInterface.load( stateMachinePluginLoader );
	loaderInterface.register_api( stateMachinePluginLoader, &iTrafficLight, "register_state_machine_api" );
#endif

	LibData trafficLightLibData;
	trafficLightLibData.loaderApi             = &loaderInterface;
	trafficLightLibData.pLoader               = stateMachinePluginLoader;
	trafficLightLibData.api                   = &iTrafficLight;
	trafficLightLibData.api_register_fun_name = "register_state_machine_api";

	//pal_state_machine_o *trafficLight = iTrafficLight.create(&iTrafficLight);

	pal::StateMachine trafficLight(&iTrafficLight);

	file_watcher_o *watched_file = file_watcher.create( "./state_machine/" );

	file_watcher.set_callback_function( watched_file, reloadLibrary, &trafficLightLibData );

	std::cout << "file watcher is watching path: " << file_watcher.get_path( watched_file ) << std::endl;

	for ( ;; ) {
		file_watcher.poll_notifications( watched_file );

		//trafficLight->vtable->next_state(trafficLight);
		trafficLight.nextState();

		//std::cout << "Traffic light: " << iTrafficLight.get_state_as_string( trafficLight ) << std::endl;
		std::cout << "Traffic light: " << trafficLight.getStateAsString() << std::endl;

		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};

	// ----
}
