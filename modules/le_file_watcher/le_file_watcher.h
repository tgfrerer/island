#ifndef GUARD_FILE_WATCHER_H
#define GUARD_FILE_WATCHER_H

#include "le_core/le_core.h"

#if defined( __linux__ )
#	define LE_FILE_WATCHER_IMPL_LINUX
#elif defined( _WIN32 )
#	define LE_FILE_WATCHER_IMPL_WIN32
#endif

struct le_file_watcher_o;

// clang-format off
struct le_file_watcher_api {

    enum class Event : int {
        FILE_CREATED       = 0,
        FILE_DELETED       = 1,
        FILE_MODIFIED      = 2,
        FILE_MOVED         = 3,
        DIRECTORY_CREATED  = 4,
        DIRECTORY_DELETED  = 5,
        DIRECTORY_MOVED    = 6
    };

    struct directory_settings {
        const char *path                                                              = nullptr;
        bool ( *callback_fun )( Event event, const char *file_path, void *user_data ) = nullptr;
        void *callback_user_data                                                      = nullptr;
    };

    struct file_settings {
        const char *filePath                                             = nullptr;
        bool ( *callback_fun )( const char *file_path, void *user_data ) = nullptr;
        void *callback_user_data                                         = nullptr;
    };

	struct le_file_watcher_interface_t{
		le_file_watcher_o  *( *create             )();
		void                ( *destroy            )( le_file_watcher_o *self );
		
		// returns unique id for the watch, -1 if unsuccessful.
		int                 ( *add_watch           )( le_file_watcher_o *self, file_settings const *settings );
        int                 ( *add_watch_directory )( le_file_watcher_o *self, directory_settings const *settings );

		bool                ( *remove_watch       )( le_file_watcher_o *self, int watch_id );
		void                ( *poll_notifications )( le_file_watcher_o *self);
	};

	le_file_watcher_interface_t le_file_watcher_i;
	
};
// clang-format on

using le_file_watcher_watch_settings      = le_file_watcher_api::file_settings;
using le_directory_watcher_watch_settings = le_file_watcher_api::directory_settings;

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

namespace le {

class FileWatcher : NoCopy, NoMove {
	le_file_watcher_o *watcher;

  public:
	using Event = le_file_watcher_api::Event;

	FileWatcher()
	    : watcher( le_file_watcher::le_file_watcher_i.create() ) {
	}

	~FileWatcher() {
		le_file_watcher::le_file_watcher_i.destroy( watcher );
	}

	int watch_file( const char *file_path, bool ( *callback_fun )( const char *file_path, void *user_data ), void *callback_userdata ) {
		le_file_watcher_api::file_settings settings{ file_path, callback_fun, callback_userdata };
		return le_file_watcher::le_file_watcher_i.add_watch( watcher, &settings );
	}

	int watch_directory( const char *file_path, bool ( *callback_fun )( Event event, const char *file_path, void *user_data ), void *callback_userdata ) {
		le_file_watcher_api::directory_settings settings{ file_path, callback_fun, callback_userdata };
		return le_file_watcher::le_file_watcher_i.add_watch_directory( watcher, &settings );
	}

	bool remove_watch( int watch_id ) {
		return le_file_watcher::le_file_watcher_i.remove_watch( watcher, watch_id );
	}

	void poll() {
		return le_file_watcher::le_file_watcher_i.poll_notifications( watcher );
	}
};

} // namespace le

#endif // !__cplusplus

#endif // GUARD_FILE_SYSTEM_H