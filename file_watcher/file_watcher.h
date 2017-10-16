#ifndef GUARD_FILE_WATCHER_H
#define GUARD_FILE_WATCHER_H

#ifdef __cplusplus
extern "C" {
#endif

void register_file_watcher_api( void *api );

struct file_watcher_o;

// file watcher interface
struct file_watcher_i {
	file_watcher_o *( *create )( const char *path );
	void ( *destroy )( file_watcher_o *lhs );
	void ( *set_callback_function )( file_watcher_o *instance, void ( *fun )( void *user_data ), void *user_data );
	void ( *poll_notifications )( file_watcher_o *instance );
	const char *( *get_path )( file_watcher_o *instance );
};

#ifdef __cplusplus
}
#endif

#endif // GUARD_FILE_SYSTEM_H
