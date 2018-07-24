#include "ApiRegistry.hpp"
#include "pal_api_loader/ApiLoader.h"
#include "pal_file_watcher/pal_file_watcher.h"
#include <unordered_map>
#include <string>
#include <stdio.h>
#include <iostream>
#include <iomanip>

/*

  Note that we're using string as hash input for our unordered map
  - as using const char * is not reliable. Object ids might have more
  than one location - this is probably due to templating in ApiRegistry.

  It's better to compare by value.

*/

static std::unordered_map<std::string, void *> apiTable;

static auto file_watcher_i = Registry::addApiStatic<pal_file_watcher_i>();
static auto file_watcher   = file_watcher_i -> create();

struct dynamic_api_info_o {
	std::string module_path;
	std::string modules_dir;
	std::string register_fun_name;
};

// ----------------------------------------------------------------------

extern "C" void *pal_registry_get_api( const char *id ) {
	//#ifndef NDEBUG
	//	auto find_result = apiTable.find(std::string(id));
	//	if (find_result == apiTable.end()){
	//		std::cerr << "warning: could not find api: " << id << std::endl;
	//	}
	//#endif
	return apiTable[ std::string( id ) ];
};

// ----------------------------------------------------------------------

extern "C" void pal_registry_set_api( const char *id, void *api ) {
	//#ifndef NDEBUG
	//	auto find_result = apiTable.find(std::string(id));
	//	if (find_result == apiTable.end()){
	//		std::cerr << "set api warning: could not find api: " << id << std::endl;
	//	}
	//#endif
	apiTable[ std::string( id ) ] = api;
}

// ----------------------------------------------------------------------

bool Registry::loaderCallback( const char *path, void *user_data_ ) {
	auto params = static_cast<Registry::CallbackParams *>( user_data_ );
	params->loaderInterface->load( params->loader );
	return params->loaderInterface->register_api( params->loader, params->api, params->lib_register_fun_name );
}

// ----------------------------------------------------------------------

int Registry::addWatch( const char *watchedPath_, Registry::CallbackParams &settings_ ) {

	pal_file_watcher_watch_settings watchSettings;

	watchSettings.callback_fun       = loaderCallback;
	watchSettings.callback_user_data = &settings_;
	watchSettings.filePath           = watchedPath_;

	return file_watcher_i->add_watch( file_watcher, watchSettings );
}

// ----------------------------------------------------------------------

pal_api_loader_i *Registry::getLoaderInterface() {
	return Registry::addApiStatic<pal_api_loader_i>();
}

// ----------------------------------------------------------------------

pal_api_loader_o *Registry::createLoader( pal_api_loader_i *loaderInterface_, const char *libPath_ ) {
	return loaderInterface_->create( libPath_ );
}

// ----------------------------------------------------------------------

void Registry::loadApi( pal_api_loader_i *loaderInterface_, pal_api_loader_o *loader_ ) {
	loaderInterface_->load( loader_ );
}

// ----------------------------------------------------------------------

void Registry::loadLibraryPersistently( pal_api_loader_i *loaderInterface_, const char *libName_ ) {
	loaderInterface_->loadLibraryPersistent( libName_ );
}

// ----------------------------------------------------------------------

dynamic_api_info_o *Registry::create_dynamic_api_info( const char *id ) {

	auto obj = new dynamic_api_info_o();

	// NOTE: we could do some basic file system operations here, such as
	// checking if file paths are valid.

	obj->module_path       = "./modules/lib" + std::string( id ) + ".so";
	obj->modules_dir       = "./modules";
	obj->register_fun_name = "register_" + std::string( id ) + "_api";

	return obj;
}

// ----------------------------------------------------------------------

const char *Registry::dynamic_api_info_get_module_path( const dynamic_api_info_o *info ) {
	return info->module_path.c_str();
}

// ----------------------------------------------------------------------

const char *Registry::dynamic_api_info_get_modules_dir( const dynamic_api_info_o *info ) {
	return info->modules_dir.c_str();
}

// ----------------------------------------------------------------------

const char *Registry::dynamic_api_info_get_register_fun_name( const dynamic_api_info_o *info ) {
	return info->register_fun_name.c_str();
}

// ----------------------------------------------------------------------

void Registry::destroy_dynamic_api_info( dynamic_api_info_o *info ) {
	delete info;
}

// ----------------------------------------------------------------------

void Registry::registerApi( pal_api_loader_i *loaderInterface, pal_api_loader_o *loader, void *api, const char *api_register_fun_name ) {
	loaderInterface->register_api( loader, api, api_register_fun_name );
}

// ----------------------------------------------------------------------

void Registry::pollForDynamicReload() {
	file_watcher_i->poll_notifications( file_watcher );
}
