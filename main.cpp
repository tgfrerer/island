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
	std::cout << "**** callback ****" << std::endl << path << std::endl << "******************" << std::endl;
}

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	api_loader_i loaderApi;
	register_api_loader_i( &loaderApi );

	Loader_o *fileWatcherPlugin = loaderApi.create( ( "./file_watcher/libfile_watcher.so" ) );
	loaderApi.load( fileWatcherPlugin );

	file_watcher_i file_watcher;
	loaderApi.register_api( fileWatcherPlugin, &file_watcher );

	file_watcher_o *watched_file = file_watcher.create( "/tmp/hello.txt" );

	file_watcher.set_callback_function( watched_file, callbackFun );

	std::cout << "file watcher is watching path: " << file_watcher.get_path( watched_file ) << std::endl;

	for ( ;; ) {
		file_watcher.poll_notifications( watched_file );
		// std::cout << ".";
	};

	// ----

	return 0;
}
