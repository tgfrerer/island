#include "le_file_watcher.h"
#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <sys/inotify.h>
#include <unistd.h>
#include <bitset>
#include <stdio.h>
#include <string>
#include <list>
#include <filesystem>
#include <algorithm>

// ----------------------------------------------------------------------

struct Watch {
	int                 inotify_watch_handle = -1;
	int                 padding              = 0;
	le_file_watcher_o *watcher_o;
	std::string         path;
	std::string         filename;
	std::string         basename;
	void *              callback_user_data = nullptr;
	bool ( *callback_fun )( const char *path, void *user_data );
};

// ----------------------------------------------------------------------

struct le_file_watcher_o {
	int              inotify_socket_handle = -1;
	int              padding               = 0;
	std::list<Watch> mWatches;
};

// ----------------------------------------------------------------------

static le_file_watcher_o *instance_create() {
	auto tmp                   = new le_file_watcher_o();
	tmp->inotify_socket_handle = inotify_init1( IN_NONBLOCK );
	return tmp;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_file_watcher_o *instance ) {

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

static int add_watch( le_file_watcher_o *instance, const le_file_watcher_watch_settings &settings ) noexcept {
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

static bool remove_watch( le_file_watcher_o *instance, int watch_id ) {
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

static void poll_notifications( le_file_watcher_o *instance ) {
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

				std::string path          = ev->name;
				const char *prev_filename = nullptr; // store previous filename to declutter printout

				// Only trigger on close-write
				if ( ev->mask & IN_CLOSE_WRITE ) {

					// We trigger *all* callbacks which watch the current file path
					// For this, we must iterate through all watches and filter the
					// ones which watch the event's file.
					for ( auto const &w : instance->mWatches ) {

						if ( w.inotify_watch_handle == ev->wd && w.filename == path ) {

							// Only print notice if it is for another filename:
							if ( prev_filename != path.c_str() ) {
								std::cout << "Watch triggered for: " << path << " [" << w.basename << "]" << std::endl
								          << std::flush;
								prev_filename = path.c_str();
							}

							// Trigger Callback.
							( *w.callback_fun )( w.path.c_str(), w.callback_user_data );
						}
					}
				}
			}

		} else {
			break;
		}
	}
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_file_watcher, p_api ) {
	auto  api                = reinterpret_cast<le_file_watcher_api *>( p_api );
	auto &api_i              = api->le_file_watcher_i;
	api_i.create             = instance_create;
	api_i.destroy            = instance_destroy;
	api_i.add_watch          = add_watch;
	api_i.remove_watch       = remove_watch;
	api_i.poll_notifications = poll_notifications;
};

// ----------------------------------------------------------------------
