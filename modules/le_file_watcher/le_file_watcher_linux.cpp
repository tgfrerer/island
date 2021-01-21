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

// ----------------------------------------------------------------------

struct FileWatch {
	int                inotify_watch_handle = -1;
	int                padding              = 0;
	le_file_watcher_o *watcher_o;
	std::string        path;
	std::string        filename;
	std::string        basename;
	void *             callback_user_data = nullptr;
	bool ( *callback_fun )( const char *path, void *user_data );
};

struct DirectoryWatch {
	int                inotify_watch_handle = -1;
	int                padding              = 0;
	le_file_watcher_o *watcher_o;
	std::string        path;
	void *             callback_user_data = nullptr;
	bool ( *callback_fun )( le_file_watcher_api::Event event, const char *path, void *user_data );
};

// ----------------------------------------------------------------------

struct le_file_watcher_o {
	int                       inotify_socket_handle = -1;
	int                       padding               = 0;
	std::list<FileWatch>      mFileWatches;
	std::list<DirectoryWatch> mDirectoryWatches;
};

// ----------------------------------------------------------------------

static le_file_watcher_o *instance_create() {
	auto tmp                   = new le_file_watcher_o();
	tmp->inotify_socket_handle = inotify_init1( IN_NONBLOCK );
	return tmp;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_file_watcher_o *instance ) {

	for ( auto &w : instance->mFileWatches ) {
		inotify_rm_watch( instance->inotify_socket_handle, w.inotify_watch_handle );
	}
	instance->mFileWatches.clear();

	if ( instance->inotify_socket_handle > 0 ) {
		std::cout << "Closing inotify instance file handle: " << std::hex << instance->inotify_socket_handle << std::endl;
		close( instance->inotify_socket_handle );
	}

	delete ( instance );
}

// ----------------------------------------------------------------------

static int add_watch_file( le_file_watcher_o *instance, le_file_watcher_watch_settings const *settings ) noexcept {
	FileWatch tmp;

	auto tmp_path = std::filesystem::canonical( settings->filePath );

	tmp.path               = tmp_path.string();
	tmp.filename           = tmp_path.filename().string();
	tmp.basename           = tmp_path.remove_filename().string(); // note this changes the path
	tmp.watcher_o          = instance;
	tmp.callback_fun       = settings->callback_fun;
	tmp.callback_user_data = settings->callback_user_data;

	tmp.inotify_watch_handle = inotify_add_watch( instance->inotify_socket_handle, tmp.basename.c_str(), IN_CLOSE_WRITE );

	instance->mFileWatches.emplace_back( std::move( tmp ) );
	return instance->mFileWatches.back().inotify_watch_handle; // tmp should not be used after move
}

static int add_watch_directory( le_file_watcher_o *instance, le_file_watcher_api::directory_settings const *settings ) noexcept {
	DirectoryWatch tmp;

	auto tmp_path = std::filesystem::canonical( settings->path );

	tmp.path               = tmp_path.string();
	tmp.watcher_o          = instance;
	tmp.callback_fun       = settings->callback_fun;
	tmp.callback_user_data = settings->callback_user_data;

	tmp.inotify_watch_handle = inotify_add_watch( instance->inotify_socket_handle, tmp.path.c_str(), IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVE );

	instance->mDirectoryWatches.emplace_back( std::move( tmp ) );
	return instance->mFileWatches.back().inotify_watch_handle;
}

// ----------------------------------------------------------------------

static bool remove_watch( le_file_watcher_o *instance, int watch_id ) {
	auto found_watch = std::find_if( instance->mFileWatches.begin(), instance->mFileWatches.end(), [ = ]( const FileWatch &w ) -> bool { return w.inotify_watch_handle == watch_id; } );
	if ( found_watch != instance->mFileWatches.end() ) {
		std::cout << "Removing inotify file watch handle: " << std::hex << found_watch->inotify_watch_handle << std::endl;
		inotify_rm_watch( instance->inotify_socket_handle, found_watch->inotify_watch_handle );
		instance->mFileWatches.erase( found_watch );
		return true;
	} else {
		auto found_dir_watch = std::find_if( instance->mDirectoryWatches.begin(), instance->mDirectoryWatches.end(), [ = ]( const DirectoryWatch &w ) -> bool { return w.inotify_watch_handle == watch_id; } );
		if ( found_dir_watch != instance->mDirectoryWatches.end() ) {
			std::cout << "Removing inotify directory watch handle: " << std::hex << found_dir_watch->inotify_watch_handle << std::endl;
			inotify_rm_watch( instance->inotify_socket_handle, found_dir_watch->inotify_watch_handle );
			instance->mDirectoryWatches.erase( found_dir_watch );
			return true;
		} else {
			std::cout << "WARNING: " << __FUNCTION__ << ": could not find and thus remove watch with id:" << watch_id << std::endl;
			return false;
		}
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

				std::filesystem::path path          = ev->name;
				const char *          prev_filename = nullptr; // store previous filename to declutter printout

				const auto log_trigger = [ & ]( const std::string &path, const std::string &src ) {
					// Only print notice if it is for another filename:
					if ( prev_filename != path.c_str() ) {
						std::cout << "Watch triggered for: " << path << " [" << src << "]" << std::endl
						          << std::flush;
						prev_filename = path.c_str();
					}
				};

				if ( ev->mask & IN_CREATE ) {
					for ( auto const &w : instance->mDirectoryWatches ) {

						if ( w.inotify_watch_handle == ev->wd ) {
							log_trigger( path, w.path );

							// Trigger Callback.
							auto api_event =  std::filesystem::is_directory( w.path / path ) ? le_file_watcher_api::Event::DIRECTORY_CREATED : le_file_watcher_api::Event::FILE_CREATED;
							( *w.callback_fun )( api_event, w.path.c_str(), w.callback_user_data );
						}
					}
				} else if ( ev->mask & IN_DELETE ) {
					for ( auto const &w : instance->mDirectoryWatches ) {

						if ( w.inotify_watch_handle == ev->wd ) {

							log_trigger( path, w.path );

							// Trigger Callback.
							auto api_event = path.extension().empty() ? le_file_watcher_api::Event::DIRECTORY_DELETED : le_file_watcher_api::Event::FILE_DELETED;
							( *w.callback_fun )( api_event, w.path.c_str(), w.callback_user_data );
						}
					}
				} else if ( ev->mask & IN_MOVE ) {
					for ( auto const &w : instance->mDirectoryWatches ) {

						if ( w.inotify_watch_handle == ev->wd ) {

							log_trigger( path, w.path );

							// Trigger Callback.
							auto api_event = std::filesystem::is_directory( w.path / path ) ? le_file_watcher_api::Event::DIRECTORY_MOVED : le_file_watcher_api::Event::FILE_MOVED;
							( *w.callback_fun )( api_event, w.path.c_str(), w.callback_user_data );
						}
					}
				} else if ( ev->mask & IN_CLOSE_WRITE ) {

					// We trigger *all* callbacks which watch the current file path
					// For this, we must iterate through all watches and filter the
					// ones which watch the event's file.
					for ( auto const &w : instance->mFileWatches ) {

						if ( w.inotify_watch_handle == ev->wd && w.filename == path ) {

							log_trigger( path, w.basename );

							// Trigger Callback.
							( *w.callback_fun )( w.path.c_str(), w.callback_user_data );
						}
					}

					// now do the same for the directory watchers
					for ( auto const &w : instance->mDirectoryWatches ) {

						if ( w.inotify_watch_handle == ev->wd ) {

							log_trigger( path, w.path );

							// Trigger Callback.
							( *w.callback_fun )( le_file_watcher_api::Event::FILE_MODIFIED, w.path.c_str(), w.callback_user_data );
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
	auto  api                 = reinterpret_cast<le_file_watcher_api *>( p_api );
	auto &api_i               = api->le_file_watcher_i;
	api_i.create              = instance_create;
	api_i.destroy             = instance_destroy;
	api_i.add_watch           = add_watch_file;
	api_i.add_watch_directory = add_watch_directory;
	api_i.remove_watch        = remove_watch;
	api_i.poll_notifications  = poll_notifications;
};

#endif