#include "file_watcher/file_watcher.h"
#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>
#include <type_traits>
#include <bitset>
#include <string>

using namespace std;

// ----------------------------------------------------------------------

struct file_watcher_o {
	int in_socket_handle = -1;
	int in_watch_handle  = -1;
	const char *path     = nullptr;

	void ( *callback_fun )( const char *path );
};

// ----------------------------------------------------------------------

file_watcher_o *create( const char *path ) noexcept {
	auto tmp  = new file_watcher_o{};
	tmp->path = path;

	tmp->in_socket_handle = inotify_init1( IN_NONBLOCK );
	tmp->in_watch_handle  = inotify_add_watch( tmp->in_socket_handle, path, IN_CLOSE_WRITE );

	return tmp;
}

// ----------------------------------------------------------------------

void destroy( file_watcher_o *instance ) noexcept {
	if ( instance->in_socket_handle > 0 ) {
		std::cout << "closing inotify instance file handle: " << std::hex << instance->in_socket_handle << std::endl;
		close( instance->in_socket_handle );
	}
	delete ( instance );
	instance = nullptr;
}

// ----------------------------------------------------------------------

void set_callback_function( file_watcher_o *instance, void ( *callback_fun_p )( const char *path ) ) {
	instance->callback_fun = callback_fun_p;
};

// ----------------------------------------------------------------------

const char *get_path( file_watcher_o *instance ) {
	return instance->path;
}

// ----------------------------------------------------------------------

void poll_notifications( file_watcher_o *instance ) {

	for ( ;; ) {

		alignas( inotify_event ) char buffer[ sizeof( inotify_event ) + NAME_MAX + 1 ];

		// careful ! read is a blocking call
		ssize_t ret = read( instance->in_socket_handle, buffer, sizeof( buffer ) );

		if ( ret > 0 ) {

			inotify_event *ev = nullptr;
			for ( ssize_t i = 0; i < ret; i += ev->len + sizeof( struct inotify_event ) ) {

				ev = reinterpret_cast< inotify_event * >( &buffer + i );

				std::cout << "some bytes read. Flags: 0x" << std::bitset< 32 >( ev->mask ) << std::endl;

				if ( ev->mask & IN_CLOSE_WRITE ) {
					std::cout << "CLOSE_WRITE detected" << std::endl;
				}

				if ( ev->wd == instance->in_watch_handle ) {
					std::cout << "current watch handle affected" << std::endl;

					if ( ev->len > 1 ) {
						std::string tmpNameStr( ev->name, ev->len );
						std::string tmpPathStr( instance->path );
						( *instance->callback_fun )( ( tmpPathStr + tmpNameStr ).c_str( ) );
					} else {
						( *instance->callback_fun )( instance->path );
					}
				}
				std::cout << "i: " << i << std::endl;
			}

			std::cout << std::flush;
		} else {
			break;
		}
	}
}

// ----------------------------------------------------------------------

void register_api( void *api_p ) {
	auto api                   = reinterpret_cast< file_watcher_i * >( api_p );
	api->create                = create;
	api->destroy               = destroy;
	api->set_callback_function = set_callback_function;
	api->get_path              = get_path;
	api->poll_notifications    = poll_notifications;
};

// ----------------------------------------------------------------------
