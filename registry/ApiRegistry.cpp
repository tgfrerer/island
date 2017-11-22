#include "ApiRegistry.hpp"
#include "loader/ApiLoader.h"
#include <unordered_map>

#include "file_watcher/file_watcher.h"

static std::unordered_map<const char *, void *> apiTable;

static auto file_watcher_i = Registry::addApiStatic<pal_file_watcher_i>();
static auto file_watcher   = file_watcher_i -> create();

void* pal_registry_get_api(const char* id){
	return apiTable[id];
};

void pal_registry_set_api(const char* id, void * api){
	apiTable[id] = api;
}

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

pal_api_loader_i *Registry::getLoaderInterface() {
	return Registry::addApiStatic<pal_api_loader_i>();
}

pal_api_loader_o *Registry::createLoader( pal_api_loader_i *loaderInterface_, const char *libPath_ ) {
	return loaderInterface_->create( libPath_ );
}

void Registry::loadLibrary(pal_api_loader_i* loaderInterface_, pal_api_loader_o* loader_)
{
	loaderInterface_->load(loader_);
}

void Registry::registerApi(pal_api_loader_i* loaderInterface, pal_api_loader_o* loader, void* api, const char* api_register_fun_name)
{
	loaderInterface->register_api(loader,api,api_register_fun_name);
}

void Registry::pollForDynamicReload() {
	file_watcher_i->poll_notifications( file_watcher );
}
