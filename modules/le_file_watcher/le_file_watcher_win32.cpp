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
#	include <array>

#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>

/*

TODO:

While this works for a rough POC, there are a couple of issues with it: 

While on linux, creating a watch for a directory which is already watched does not create another watch,
on windows each watch is created, and will get triggered. Linux uses an internal cache for watches. We
should do so, too.

Since our api watches files, this means that we end up with a trigger for each file 
which is in a directory, which is not what we want, because the number of messages explodes as soon 
as we have plenty of files per directory.

We want an architecture where we internally have a list of watched directories, and each directory has 
a list of watched files. If a directory triggers, we find if the file that did the trigger is on our
list of watched files, and if it is, we trigger the callback.


*/ 




// ----------------------------------------------------------------------

struct Watch {
	OVERLAPPED                 overlapped; // imortant that this is the first element, so that we may cast.
	int                        watch_handle = -1;
	std::string                path;
	std::string                filename;
	std::string                basename;
	std::array<BYTE, 4096 * 4> buffer; // 4 pages
	HANDLE                     directory_handle;
	void *                     callback_user_data = nullptr;
	DWORD                      notify_filter;
	bool ( *callback_fun )( const char *path, void *user_data );
};

// ----------------------------------------------------------------------

struct le_file_watcher_o {
	int                inotify_socket_handle = -1;
	int                padding               = 0;
	std::list<Watch *> mWatches;
};

// ----------------------------------------------------------------------

static le_file_watcher_o *instance_create() {
	auto instance = new le_file_watcher_o();

	// todo: implement

	return instance;
}

static void refresh_watch( Watch *w ); // ffdecl

// ----------------------------------------------------------------------

static void instance_destroy( le_file_watcher_o *instance ) {

	// close all watch objects in instance
	// destroy all watch objects in instance
	// remove all watch objects from instance
	// destroy instance

	for ( auto &w : instance->mWatches ) {
		CloseHandle( w->overlapped.hEvent );
		CloseHandle( w->directory_handle );
		delete w;
	}

	instance->mWatches.clear();

	delete ( instance );
}

// ----------------------------------------------------------------------
/// \brief add a watch based on a particular file path
static int add_watch( le_file_watcher_o *instance, le_file_watcher_watch_settings const *settings ) noexcept {

	auto watch = new Watch(); // zero-initialise everything.

	auto tmp_path = std::filesystem::canonical( settings->filePath );

	std::cout << "Adding watch for: '" << tmp_path.string() << "'" << std::endl
	          << std::flush;

	watch->path               = tmp_path.string();
	watch->filename           = tmp_path.filename().string();
	watch->basename           = tmp_path.remove_filename().string(); // note this changes the path
	watch->callback_fun       = settings->callback_fun;
	watch->callback_user_data = settings->callback_user_data;
	watch->notify_filter =
	    FILE_NOTIFY_CHANGE_LAST_WRITE |
	    FILE_NOTIFY_CHANGE_LAST_ACCESS;

	watch->directory_handle =
	    CreateFile( watch->basename.c_str(), FILE_LIST_DIRECTORY /*GENERIC_READ*/,
	                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL );

	if ( watch->directory_handle != INVALID_HANDLE_VALUE ) {
		watch->overlapped.hEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	}

	refresh_watch( watch );

	instance->mWatches.emplace_back( watch );

	return watch->watch_handle;
}

// ----------------------------------------------------------------------
/// \brief remove watch given by watch_id
/// \return true on success, otherwise false.
static bool remove_watch( le_file_watcher_o *instance, int watch_id ) {

	// find watch, delete it
	// delete it from list.

	return false;
};

// ----------------------------------------------------------------------

static void CALLBACK watch_callback( DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped ) {
	char                     callback_filename[ MAX_PATH ];
	PFILE_NOTIFY_INFORMATION pNotify;
	Watch *                  watch  = ( Watch * )lpOverlapped;
	size_t                   offset = 0;

	if ( dwNumberOfBytesTransfered == 0 )
		return;

	if ( dwErrorCode == ERROR_SUCCESS ) {
		do {
			pNotify = ( PFILE_NOTIFY_INFORMATION )&watch->buffer[ offset ];
			offset += pNotify->NextEntryOffset;

			int count =
			    WideCharToMultiByte( CP_ACP, 0, pNotify->FileName,
			                         pNotify->FileNameLength / sizeof( WCHAR ),
			                         callback_filename, MAX_PATH - 1, NULL, NULL );

			callback_filename[ count ] = '\0';

			// Since watches are triggered directory-wide, we must filter out which 
			// watch was actually triggered. We do this by comparing the filename 
			// from the Notify Information with the filename in our watch.
			// If they match, we must trigger the callback. Otherwise, we silently 
			// ignore the notification.
			//
			//
			if ( 0 == strncmp( callback_filename, watch->filename.c_str(), count ) ) {
				( *watch->callback_fun )( watch->path.c_str(), watch->callback_user_data );
			}

		} while ( pNotify->NextEntryOffset != 0 );

		// we must re-issue watch

		refresh_watch( watch );
	}
}

static void refresh_watch( Watch *w ) {

	BOOL result = ReadDirectoryChangesW(
	    w->directory_handle,
	    w->buffer.data(),
	    DWORD( w->buffer.size() ),
	    FALSE,
	    w->notify_filter,
	    NULL,
	    &w->overlapped,
	    watch_callback );
}

// ----------------------------------------------------------------------
/// \brief trigger callbacks on any watches which have pending notifications
///
static void poll_notifications( le_file_watcher_o *instance ) {

	// todo: implement
	MsgWaitForMultipleObjectsEx( 0, NULL, 0, QS_ALLINPUT, MWMO_ALERTABLE );
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