#include "ApiRegistry.hpp"
#include "pal_api_loader/ApiLoader.h"
#include "pal_file_watcher/pal_file_watcher.h"
#include <vector>
#include <string>
#include <stdio.h>
#include <iostream>
#include <iomanip>
#include <array>

/*

  Note that we're using string as hash input for our unordered map
  - as using const char * is not reliable. Object ids might have more
  than one location - this is probably due to templating in ApiRegistry.

  It's better to compare by value.

*/

struct ApiStore {
	static constexpr size_t        fp_num_bytes = 4096 * 10; // 10 pages of function pointers should be enough
	std::array<char, fp_num_bytes> fp_storage{};
	size_t                         fp_used_bytes = 0; // number of bytes in use for function pointer usage

	std::vector<std::string> names;
	std::vector<uint64_t>    nameHashes;
	std::vector<void *>      ptrs;
};

static ApiStore apiStore;

static auto file_watcher_i = Registry::addApiStatic<pal_file_watcher_i>();
static auto file_watcher   = file_watcher_i -> create();

struct dynamic_api_info_o {
	std::string module_path;
	std::string modules_dir;
	std::string register_fun_name;
};

// ----------------------------------------------------------------------

extern "C" void *pal_registry_get_api( uint64_t id, const char *debug_id ) {
	//#ifndef NDEBUG
	//	auto find_result = apiTable.find(std::string(id));
	//	if (find_result == apiTable.end()){
	//		std::cerr << "warning: could not find api: " << id << std::endl;
	//	}
	//#endif

	size_t foundElement = 0;
	for ( const auto &n : apiStore.nameHashes ) {
		if ( n == id ) {
			break;
		}
		++foundElement;
	}

	if ( foundElement == apiStore.nameHashes.size() ) {
		// no element found, we need to add an element
		apiStore.nameHashes.emplace_back( id );
		apiStore.ptrs.emplace_back( nullptr );   // initialise to nullptr
		apiStore.names.emplace_back( debug_id ); // implicitly creates a string
	}

	// invariant: foundElement points to correct element

	return apiStore.ptrs[ foundElement ];
};

// ----------------------------------------------------------------------

extern "C" void *pal_registry_create_api( uint64_t id, size_t apiStructSize, const char *debugName ) {
	//#ifndef NDEBUG
	//	auto find_result = apiTable.find(std::string(id));
	//	if (find_result == apiTable.end()){
	//		std::cerr << "set api warning: could not find api: " << id << std::endl;
	//	}
	//#endif

	size_t foundElement = 0;
	for ( const auto &n : apiStore.nameHashes ) {
		if ( n == id ) {
			break;
		}
		++foundElement;
	}

	if ( foundElement == apiStore.nameHashes.size() ) {

		// Element with this name not found - we must insert it.

		apiStore.nameHashes.emplace_back( id );
		apiStore.ptrs.emplace_back( nullptr );    // initialise to nullptr
		apiStore.names.emplace_back( debugName ); // implicitly creates a string
	}

	auto &apiPtr = apiStore.ptrs[ foundElement ];

	if ( apiPtr == nullptr ) {

		// api struct has not yet been allocated, we must do so now.

		void *apiMemory = ( apiStore.fp_storage.data() + apiStore.fp_used_bytes ); // point to next free space in api store
		apiStore.fp_used_bytes += apiStructSize;                                   // increase number of used bytes in api store

		apiPtr = apiMemory; // Store updated address for api - this address won't change for the
		                    // duration of the program.
	}

	return apiPtr;
}

// ----------------------------------------------------------------------

struct Registry::callback_params_o {
	pal_api_loader_i *loaderInterface;
	pal_api_loader_o *loader;
	void *            api;
	const char *      lib_register_fun_name;
};

// ----------------------------------------------------------------------

Registry::callback_params_o *Registry::create_callback_params( pal_api_loader_i *loaderInterface, pal_api_loader_o *loader, void *api, const char *lib_register_fun_name ) {
	// We create the callback_params object here so that it is not created in the header file.
	// Creating it here means we force the object to be allocated in this translation unit,
	// which is the only translation unit in a plugin-based program which is guaranteed not
	// to be reloaded. Therefore we can assume that any static data in here is persistent,
	// and that pointers to it remain valid for the duration of the program.
	auto obj = new callback_params_o{loaderInterface, loader, api, lib_register_fun_name};
	return obj;
}

// ----------------------------------------------------------------------

bool Registry::loaderCallback( const char *path, void *user_data_ ) {
	auto params = static_cast<Registry::callback_params_o *>( user_data_ );
	params->loaderInterface->load( params->loader );
	return params->loaderInterface->register_api( params->loader, params->api, params->lib_register_fun_name );
}

// ----------------------------------------------------------------------

int Registry::addWatch( const char *watchedPath_, Registry::callback_params_o *settings_ ) {

	pal_file_watcher_watch_settings watchSettings;

	watchSettings.callback_fun       = loaderCallback;
	watchSettings.callback_user_data = reinterpret_cast<void *>( settings_ );
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
