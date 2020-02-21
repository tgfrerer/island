#ifndef GUARD_FILE_WATCHER_H
#define GUARD_FILE_WATCHER_H

#include "pal_api_loader/ApiRegistry.h"

struct pal_file_watcher_o;

struct pal_file_watcher_watch_settings {
	const char *filePath                                             = nullptr;
	bool ( *callback_fun )( const char *file_path, void *user_data ) = nullptr;
	void *callback_user_data                                         = nullptr;
};

// clang-format off
struct pal_file_watcher_api {

	struct pal_file_watcher_interface_t{
		pal_file_watcher_o *( *create             )();
		void                ( *destroy            )( pal_file_watcher_o *self );
		int                 ( *add_watch          )( pal_file_watcher_o *self, const pal_file_watcher_watch_settings &settings ); /// \return unique id for the watch, -1 if unsuccessful.
		bool                ( *remove_watch       )( pal_file_watcher_o *self, int watch_id );
		void                ( *poll_notifications )( pal_file_watcher_o *self);
	};

	pal_file_watcher_interface_t pal_file_watcher_i;
	
};
// clang-format on

LE_MODULE( pal_file_watcher );

// File watcher can only be loaded as a static module - it will always
// be statically linked into the core module.
LE_MODULE_LOAD_STATIC( pal_file_watcher );

#endif // GUARD_FILE_SYSTEM_H
