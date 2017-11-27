#ifndef GUARD_FILE_WATCHER_H
#define GUARD_FILE_WATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

void register_file_watcher_api( void *api );

struct pal_file_watcher_o;

struct pal_file_watcher_watch_settings {
	const char *filePath                      = nullptr;
	bool ( *callback_fun )( void *user_data ) = nullptr;
	void *callback_user_data                  = nullptr;
};

// file watcher interface
struct pal_file_watcher_i {

	static constexpr auto id      = "file_watcher";
	static constexpr auto pRegFun = register_file_watcher_api;

	pal_file_watcher_o *( *create )();
	void ( *destroy )( pal_file_watcher_o *obj );

	// return value is unique id for the watch, -1 if unsuccessful.
	int ( *add_watch )( pal_file_watcher_o *watcher, const pal_file_watcher_watch_settings& settings );
	bool ( *remove_watch )( pal_file_watcher_o *instance, int watch_id );

	void (*poll_notifications)(pal_file_watcher_o*instance);
};

#ifdef __cplusplus
}
#endif

#endif // GUARD_FILE_SYSTEM_H
