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

int main( int argc, char const *argv[] ) {

	api_loader_i loaderInterface;
	register_api_loader_i( &loaderInterface );

	file_watcher_i file_watcher;

#ifdef PLUGIN_FILE_WATCHER_STATIC
	std::cout << "using STATIC file watcher" << std::endl;
	register_file_watcher_api( &file_watcher );
#else
	std::cout << "using DYNAMIC file watcher" << std::endl;
	api_loader_o *fileWatcherPlugin = loaderInterface.create( ( "./file_watcher/libfile_watcher.so" ) );
	loaderInterface.load( fileWatcherPlugin );
	loaderInterface.register_api( fileWatcherPlugin, &file_watcher, "register_file_watcher_api" );
#endif

	pal_state_machine_i trafficLightInterface;

	file_watcher_o *watched_file = nullptr;

#ifdef PLUGIN_STATE_MACHINE_STATIC
	std::cout << "using STATIC state machine module " << std::endl;
	register_state_machine_api( &trafficLightInterface );
#else
	std::cout << "using DYNAMIC state machine module" << std::endl;
	pal::ApiLoader stateMachinePlugin( &loaderInterface, &trafficLightInterface, "./state_machine/libstate_machine.so", "register_state_machine_api" );
	stateMachinePlugin.loadLibrary();

	watched_file = file_watcher.create( "./state_machine/" );
	file_watcher.set_callback_function( watched_file, &pal::ApiLoader::loadLibraryCallback, &stateMachinePlugin );
#endif

	pal::StateMachine trafficLight( &trafficLightInterface );

	if ( watched_file ) {
		std::cout << "file watcher is watching path: " << file_watcher.get_path( watched_file ) << std::endl;
	}

	for ( ;; ) {

		if ( watched_file ) {
			file_watcher.poll_notifications( watched_file );
		}

		// trafficLight->vtable->next_state(trafficLight);
		trafficLight.nextState();

		std::cout << "Traffic light: " << trafficLight.getStateAsString() << std::endl;

		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
	};

	// ----
}
