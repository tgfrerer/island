#ifndef GUARD_FILE_WATCHER_H
#define GUARD_FILE_WATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

void register_file_watcher_api( void *api );

struct pal_file_watcher_o;

// file watcher interface
struct pal_file_watcher_i {
	pal_file_watcher_o *( *create )( const char *path );
	void ( *destroy )( pal_file_watcher_o *lhs );
	void ( *set_callback_function )( pal_file_watcher_o *instance, bool ( *fun )( void *user_data ), void *user_data );
	void ( *poll_notifications )( pal_file_watcher_o *instance );
	const char *( *get_path )( pal_file_watcher_o *instance );
};

#ifdef __cplusplus
}
#endif

#endif // GUARD_FILE_SYSTEM_H
