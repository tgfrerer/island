#include "le_file_watcher.h"

#ifdef LE_FILE_WATCHER_IMPL_LINUX

#	include <iomanip>
#	include <iostream>
#	include <bitset>
#	include <stdio.h>
#	include <string>
#	include <list>
#	include <filesystem>
#	include <algorithm>

#	include <dirent.h>
#	include <sys/inotify.h>
#	include <unistd.h>
#	include "le_log/le_log.h"
#	include <unordered_map>
#	include <mutex>
#	include <cassert>

static constexpr auto LOGGER_LABEL = "le_file_watcher_linux";

// ----------------------------------------------------------------------

struct Watch {
	int                inotify_watch_handle = -1;
	int                unique_watch_id      = -1;
	int                padding              = 0;
	le_file_watcher_o *watcher_o            = nullptr; // Non-owning: Weak pointer back to instance
	std::string        path;
	std::string        filename;
	std::string        basename;
	void *             callback_user_data = nullptr;
	void ( *callback_fun )( const char *path, void *user_data );
};

// ----------------------------------------------------------------------

struct le_file_watcher_o {
	std::mutex mtx;
	int        inotify_socket_handle = -1;
	int        next_unique_watch_id  = 0;

	std::unordered_map<int, std::unordered_map<int, Watch>> mWatches; // indexed by inotify_watch_handle, then unique_watch_id
};

// ----------------------------------------------------------------------

static le_file_watcher_o *instance_create() {
	auto tmp                   = new le_file_watcher_o();
	tmp->inotify_socket_handle = inotify_init1( IN_NONBLOCK );
	return tmp;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_file_watcher_o *instance ) {

	static auto logger = LeLog( LOGGER_LABEL );

	for ( auto &w : instance->mWatches ) {
		int result = inotify_rm_watch( instance->inotify_socket_handle, w.first );
		assert( result == 0 );
	}
	instance->mWatches.clear();

	if ( instance->inotify_socket_handle > 0 ) {
		logger.debug( "Closing inotify instance file handle: %p", instance->inotify_socket_handle );
		close( instance->inotify_socket_handle );
	}

	delete ( instance );
}

// ----------------------------------------------------------------------

static int add_watch( le_file_watcher_o *instance, le_file_watcher_watch_settings const *settings ) noexcept {
	Watch tmp;

	auto lock = std::unique_lock( instance->mtx );

	static auto logger   = LeLog( LOGGER_LABEL );
	auto        tmp_path = std::filesystem::canonical( settings->filePath );

	tmp.path               = tmp_path.string();
	tmp.filename           = tmp_path.filename().string();
	tmp.basename           = tmp_path.remove_filename().string(); // note this changes the path
	tmp.watcher_o          = instance;
	tmp.callback_fun       = settings->callback_fun;
	tmp.callback_user_data = settings->callback_user_data;

	tmp.inotify_watch_handle = inotify_add_watch( instance->inotify_socket_handle, tmp.basename.c_str(), IN_CLOSE_WRITE );
	tmp.unique_watch_id      = instance->next_unique_watch_id++;

	logger.debug( "Added inotify watch handle: %d, '%s'", tmp.unique_watch_id, tmp.path.c_str() );

	instance->mWatches[ tmp.inotify_watch_handle ][ tmp.unique_watch_id ] = tmp;

	return tmp.unique_watch_id;
}

// ----------------------------------------------------------------------

static bool remove_watch( le_file_watcher_o *instance, int watch_id ) {
	static auto logger = LeLog( LOGGER_LABEL );
	auto        lock   = std::unique_lock( instance->mtx );

	auto found_inotify_watch =
	    std::find_if( instance->mWatches.begin(), instance->mWatches.end(),
	                  [ & ]( std::pair<int, std::unordered_map<int, Watch>> const &w ) -> bool {
		                  return w.second.find( watch_id ) != w.second.end();
	                  } );

	if ( found_inotify_watch != instance->mWatches.end() ) {

		auto &inotify_watch_entry = found_inotify_watch->second;
		// Remove entry with current watch_id
		auto found_watch = inotify_watch_entry.find( watch_id );

		logger.debug( "Removing watch handle: %d, '%s'", watch_id, found_watch->second.path.c_str() );

		// remove the watch with this particular id.
		inotify_watch_entry.erase( found_watch );

		if ( inotify_watch_entry.empty() ) {
			// we must only remove the inotify watch if we are the only ones who use this inotify handle.
			// no more files left for this watch.
			logger.info( "Removing inotify watch. socket_handle: %d, watch_hanle: %d", instance->inotify_socket_handle, found_inotify_watch->first );
			int result = inotify_rm_watch( instance->inotify_socket_handle, found_inotify_watch->first );
			if ( result != 0 ) {
				logger.error( "inotify rm watch error: %d", result );
			}
			instance->mWatches.erase( found_inotify_watch );
		}

		return true;
	} else {
		logger.error( "%s : Could not find and thus remove watch with id: 0x%x", __PRETTY_FUNCTION__, watch_id );
		return false;
	}
};

// ----------------------------------------------------------------------

static void poll_notifications( le_file_watcher_o *instance ) {
	static_assert( sizeof( inotify_event ) == sizeof( struct inotify_event ), "must be equal" );
	static auto logger = LeLog( LOGGER_LABEL );

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
					// first make sure there are any watches for this inotify file handle
					auto found_watches = instance->mWatches.find( ev->wd );
					if ( found_watches != instance->mWatches.end() ) {
						// We trigger *all* callbacks which watch the current file path
						// For this, we must iterate through all watches and filter the
						// ones which watch the event's file.
						for ( auto const &w : found_watches->second ) {
							if ( w.second.filename == path ) {
								// Only print notice if it is for a new filename:
								if ( prev_filename != path.c_str() ) {
									logger.info( "Watch triggered for: '%s' [%s]", path.c_str(), w.second.basename.c_str() );
									prev_filename = path.c_str();
								}
								// Trigger Callback.
								( *w.second.callback_fun )( w.second.path.c_str(), w.second.callback_user_data );
							}
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

#endif