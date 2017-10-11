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

static void callbackFun( const char *path ) {
	std::cout << "**** callback ****" << std::endl
	          << path << std::endl
	          << "******************" << std::endl;
}

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	auto fileWatcherApiLoader = std::make_unique< pal::ApiLoader >(
	    "./file_watcher/libfile_watcher.so" );

	file_watcher_i file_watcher;
	fileWatcherApiLoader->register_api( &file_watcher );

	auto watched_file = file_watcher.create( "/tmp/" );

	file_watcher.set_callback_function( watched_file, callbackFun );

	std::cout << "file watcher is watching path: "
	          << file_watcher.get_path( watched_file ) << std::endl;

	for ( ;; ) {
		file_watcher.poll_notifications( watched_file );
		// std::cout << ".";
	};
	// ----

	pal_state_machine_i stateMachineApi{};

	auto apiLoaderStateMachine = std::make_unique< pal::ApiLoader >(
	    "./state_machine/libstate_machine.so" );

	apiLoaderStateMachine->register_api( &stateMachineApi );

	auto trafficLight = stateMachineApi.createState( );

	//	bool appShouldLoop = true;
	//	do {

	//		std::cout << "Current state machine state: "
	//		          << stateMachineApi.get_state_as_string( trafficLight )
	//		          << std::endl;

	//		char i = 0;
	//		std::cin >> i;

	//		switch ( i ) {
	//		case 'l': {
	//			std::cout << "reloading accumulator library" <<
	// std::endl;
	//			apiLoaderAccum->reload( );
	//			apiLoaderAccum->register_api( &accum );
	//			break;
	//		}
	//		case 'a': {
	//			std::cout << "reloading state machine library" <<
	// std::endl;
	//			pal_state_machine_i tmp;

	//			apiLoaderStateMachine->reload( );
	//			apiLoaderStateMachine->register_api( &tmp );
	//			break;
	//		}
	//		case 'r': {
	//			stateMachineApi.reset_state( trafficLight );
	//			break;
	//		}
	//		case 'i': {
	//			stateMachineApi.next_state( trafficLight );
	//			break;
	//		}
	//		case 'q':
	//			appShouldLoop = false;
	//		    break;
	//		default:
	//		    break;
	//		};

	//	} while ( appShouldLoop );

	//	std::cout << "And with this, goodbye." << std::endl;

	//	//	stateMachineApi.destroyState( trafficLight );
	//	file_watcher.destroy( watched_file );

	// main needs to load the dynamic library and then watch the dynamic library
	// file - if there is change the library needs to be re-loaded.

	return 0;
}
