#ifndef GUARD_FILE_WATCHER_H
#define GUARD_FILE_WATCHER_H

#include "le_core/le_core.h"

#if defined( __linux__ )
#	define LE_FILE_WATCHER_IMPL_LINUX
#elif defined( _WIN32 )
#	define LE_FILE_WATCHER_IMPL_WIN32
#endif

struct le_file_watcher_o;

struct le_file_watcher_watch_settings {
	const char *filePath                                             = nullptr;
	void ( *callback_fun )( const char *file_path, void *user_data ) = nullptr;
	void *callback_user_data                                         = nullptr;
};

// clang-format off
struct le_file_watcher_api {

	struct le_file_watcher_interface_t{
		le_file_watcher_o *( *create             )();
		void                ( *destroy            )( le_file_watcher_o *self );
		
		// returns unique id for the watch, -1 if unsuccessful.
		int                 ( *add_watch          )( le_file_watcher_o *self, le_file_watcher_watch_settings const *settings );
		
		bool                ( *remove_watch       )( le_file_watcher_o *self, int watch_id );
		void                ( *poll_notifications )( le_file_watcher_o *self);
	};

	le_file_watcher_interface_t le_file_watcher_i;
	
};
// clang-format on

LE_MODULE( le_file_watcher );

// File watcher can only be loaded as a static module - it will always
// be statically linked into the core module.
LE_MODULE_LOAD_DEFAULT( le_file_watcher );

// ----------------------------------------------------------------------

#ifdef __cplusplus
namespace le_file_watcher {
static const auto &api               = le_file_watcher_api_i;
static const auto &le_file_watcher_i = api -> le_file_watcher_i;
} // namespace le_file_watcher
#endif // !__cplusplus

#endif // GUARD_FILE_SYSTEM_H