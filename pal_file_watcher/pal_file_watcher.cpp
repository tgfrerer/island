#include "pal_file_watcher/pal_file_watcher.h"
#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>
#include <bitset>
#include <stdio.h>
#include <string>
#include <list>
#include <experimental/filesystem>
#include <algorithm>

using namespace std;

namespace std {
namespace filesystem {
using namespace experimental::filesystem;
}
} // namespace std

// ----------------------------------------------------------------------

struct Watch {
	int                 inotify_watch_handle = -1;
	int                 padding              = 0;
	pal_file_watcher_o *watcher_o;
	std::string         path;
	std::string         filename;
	std::string         basename;
	void *              callback_user_data = nullptr;
	bool ( *callback_fun )( const char *path, void *user_data );
};

// ----------------------------------------------------------------------

struct pal_file_watcher_o {
	int              inotify_socket_handle = -1;
	int              padding               = 0;
	std::list<Watch> mWatches;
};

// ----------------------------------------------------------------------

static pal_file_watcher_o *instance_create() {
	auto tmp                   = new pal_file_watcher_o();
	tmp->inotify_socket_handle = inotify_init1( IN_NONBLOCK );
	return tmp;
}

// ----------------------------------------------------------------------

static void instance_destroy( pal_file_watcher_o *instance ) {

	for ( auto &w : instance->mWatches ) {
		inotify_rm_watch( instance->inotify_socket_handle, w.inotify_watch_handle );
	}
	instance->mWatches.clear();

	if ( instance->inotify_socket_handle > 0 ) {
		std::cout << "Closing inotify instance file handle: " << std::hex << instance->inotify_socket_handle << std::endl;
		close( instance->inotify_socket_handle );
	}
	delete ( instance );
}

// ----------------------------------------------------------------------

static int add_watch( pal_file_watcher_o *instance, const pal_file_watcher_watch_settings &settings ) noexcept {
	Watch tmp;

	auto tmp_path = std::filesystem::canonical( settings.filePath );

	tmp.path                 = tmp_path;
	tmp.filename             = tmp_path.filename();
	tmp.basename             = tmp_path.remove_filename(); // note this changes the path
	tmp.watcher_o            = instance;
	tmp.callback_fun         = settings.callback_fun;
	tmp.callback_user_data   = settings.callback_user_data;
	tmp.inotify_watch_handle = inotify_add_watch( instance->inotify_socket_handle, tmp.basename.c_str(), IN_CLOSE_WRITE );

	instance->mWatches.emplace_back( std::move( tmp ) );
	return tmp.inotify_watch_handle;
}

// ----------------------------------------------------------------------

static bool remove_watch( pal_file_watcher_o *instance, int watch_id ) {
	auto found_watch = std::find_if( instance->mWatches.begin(), instance->mWatches.end(), [=]( const Watch &w ) -> bool { return w.inotify_watch_handle == watch_id; } );
	if ( found_watch != instance->mWatches.end() ) {
		std::cout << "Removing inotify watch handle: " << std::hex << found_watch->inotify_watch_handle << std::endl;
		inotify_rm_watch( instance->inotify_socket_handle, found_watch->inotify_watch_handle );
		instance->mWatches.erase( found_watch );
		return true;
	} else {
		std::cout << "WARNING: " << __FUNCTION__ << ": could not find and thus remove watch with id:" << watch_id << std::endl;
		return false;
	}
};

// ----------------------------------------------------------------------

static void poll_notifications( pal_file_watcher_o *instance ) {
	static_assert( sizeof( inotify_event ) == sizeof( struct inotify_event ), "must be equal" );

	for ( ;; ) {

		alignas( inotify_event ) char buffer[ sizeof( inotify_event ) + NAME_MAX + 1 ];

		ssize_t ret = read( instance->inotify_socket_handle, buffer, sizeof( buffer ) );

		if ( ret > 0 ) {

			inotify_event *ev = nullptr;
			for ( ssize_t i = 0; i < ret; i += ev->len + sizeof( struct inotify_event ) ) {

				ev = reinterpret_cast<inotify_event *>( buffer + i );

				if ( !ev->len ) {
					// if there is no filename to compare with, there is no need to check this against
					// our watches, as they require a filename.
					continue;
				}

				auto foundWatch = std::find_if( instance->mWatches.begin(), instance->mWatches.end(), [&]( const Watch &w ) -> bool {
					if ( ev->len == 0 ) {
						return false;
					}

					// Since inotify watches directories, we must check for the name of the file within the directory

					std::string path = std::string( ev->name );
					return ( w.inotify_watch_handle == ev->wd ) && ( w.filename == path );
				} );

				if ( foundWatch != instance->mWatches.end() ) {

					if ( ev->mask & ( IN_CLOSE_WRITE ) ) {

						std::string tmpStr( ev->name );
						std::cout << "Watch triggered for: " << tmpStr << " [" << foundWatch->basename << "]" << std::endl
						          << std::flush;

						( *foundWatch->callback_fun )( foundWatch->path.c_str(), foundWatch->callback_user_data );
					}

				} else {
					std::cout << __FUNCTION__ << ": found no affected watch." << std::endl;
				}
				// std::cout << "watch descriptor: " << ev->wd << std::endl;
			}

		} else {
			break;
		}
	}
}

// ----------------------------------------------------------------------

void register_file_watcher_api( void *api_p ) {
	auto api                = reinterpret_cast<pal_file_watcher_i *>( api_p );
	api->create             = instance_create;
	api->destroy            = instance_destroy;
	api->add_watch          = add_watch;
	api->remove_watch       = remove_watch;
	api->poll_notifications = poll_notifications;
};

// ----------------------------------------------------------------------
