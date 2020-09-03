#include "le_file_watcher.h"

#ifdef LE_FILE_WATCHER_IMPL_WIN32

#	include <iomanip>
#	include <iostream>
#	include <bitset>
#	include <stdio.h>
#	include <string>
#	include <list>
#	include <filesystem>
#	include <algorithm>

#	define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ----------------------------------------------------------------------

struct Watch {
	int                watch_handle = -1;
	int                padding              = 0;
	le_file_watcher_o *watcher_o;
	std::string        path;
	std::string        filename;
	std::string        basename;
	void *             callback_user_data = nullptr;
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
	auto instance = new le_file_watcher_o();

	// todo: implement

	return instance;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_file_watcher_o *instance ) {

	// close all watch objects in instance
	// destroy all watch objects in instance 
	// remove all watch objects from instance
	// destroy instance

	// todo: implement

	delete ( instance );
}

// ----------------------------------------------------------------------
/// \brief add a watch based on a particular file path
static int add_watch( le_file_watcher_o *instance, le_file_watcher_watch_settings const *settings ) noexcept {
	Watch watch;

	auto tmp_path = std::filesystem::canonical( settings->filePath );

	watch.path               = tmp_path.string();
	watch.filename           = tmp_path.filename().string();
	watch.basename           = tmp_path.remove_filename().string(); // note this changes the path
	watch.watcher_o          = instance;
	watch.callback_fun       = settings->callback_fun;
	watch.callback_user_data = settings->callback_user_data;

	// todo: implement

	instance->mWatches.emplace_back( std::move( watch ) );
	return watch.watch_handle;
}

// ----------------------------------------------------------------------
/// \brief remove watch given by watch_id
/// \return true on success, otherwise false.
static bool remove_watch( le_file_watcher_o *instance, int watch_id ) {
	return false;
};

// ----------------------------------------------------------------------
/// \brief trigger callbacks on any watches which have pending notifications
/// 
static void poll_notifications( le_file_watcher_o *instance ) {

	// todo: implement

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