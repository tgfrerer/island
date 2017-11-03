#include "ApiRegistry.hpp"
#include "file_watcher/file_watcher.h"

std::unordered_map<const char *, void *> Registry::apiTable;

static auto file_watcher_i = Registry::addApiStatic<pal_file_watcher_i>();
static auto file_watcher   = file_watcher_i -> create();

bool Registry::loaderCallback( void *user_data_ ) {

	auto params = static_cast<Registry::CallbackParams *>( user_data_ );
	params->loaderInterface->load( params->loader );
	return params->loaderInterface->register_api( params->loader, params->api, params->lib_register_fun_name );
}

int Registry::addWatch( const char *watchedPath_, Registry::CallbackParams &settings_ ) {

	pal_file_watcher_watch_settings watchSettings;

	watchSettings.callback_fun       = loaderCallback;
	watchSettings.callback_user_data = &settings_;
	watchSettings.filePath           = watchedPath_;

	return file_watcher_i->add_watch( file_watcher, watchSettings );
}

void Registry::pollForDynamicReload(){
	file_watcher_i->poll_notifications(file_watcher);
}
