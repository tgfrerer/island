#include "ApiLoader.h"

#include <dlfcn.h>
#include <iostream>



// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

struct api_loader_o {
	const char *mPath   = nullptr;
	void *      mHandle = nullptr;
};

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {
	void *handle = dlopen( lib_name, RTLD_NOW );
	std::cout << "\topening library handle: " << std::hex << handle << std::endl;

	if ( !handle ) {
		auto loadResult = dlerror();
		std::cerr << "error: " << loadResult << std::endl;
	}

	return handle;
}

// ----------------------------------------------------------------------

static void unload_library( void *handle_ ) {
	if ( handle_ ) {
		std::cout << "\tclosing library handle: " << std::hex << handle_ << std::endl;
		dlclose( handle_ );
		handle_ = nullptr;
	}
}

// ----------------------------------------------------------------------

static api_loader_o *create( const char *path_ ) {
	api_loader_o *tmp = new api_loader_o{};
	tmp->mPath  = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void destroy( api_loader_o *obj ) {
	unload_library( obj->mHandle );
	delete obj;
};

// ----------------------------------------------------------------------

static bool load( api_loader_o *obj ) {
	unload_library( obj->mHandle );
	obj->mHandle = load_library( obj->mPath );
	return ( obj->mHandle != nullptr );
}

// ----------------------------------------------------------------------

static bool register_api( api_loader_o *obj, void *api_interface, const char *api_registry_name ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;
	// load function pointer to initialisation method
	fptr = reinterpret_cast<register_api_fun_p_t>( dlsym( obj->mHandle, api_registry_name ) );
	if ( !fptr ) {
		std::cerr << "error: " << dlerror() << std::endl;
		return false;
	}
	// Initialize the API. This means telling the API to populate function
	// pointers inside the struct which we are passing as parameter.
	( *fptr )( api_interface );
	return true;
}

// ----------------------------------------------------------------------

bool register_api_loader_i( api_loader_i *api ) {
	api->create       = create;
	api->destroy      = destroy;
	api->load         = load;
	api->register_api = register_api;
	return true;
};
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
#ifdef __cplusplus

#include "file_watcher/file_watcher.h"
#include <string>

// ----------------------------------------------------------------------

bool pal::ApiLoader::loadLibrary(){
	if (file_watcher_interface == nullptr){
		file_watcher_interface = new file_watcher_i;
		register_file_watcher_api( file_watcher_interface );
		file_watcher = file_watcher_interface->create( loader->mPath );
		file_watcher_interface->set_callback_function( file_watcher, loadLibraryCallback, this );
	}
	return loadLibraryCallback( this );
}

// ----------------------------------------------------------------------

pal::ApiLoader::~ApiLoader(){
	if (file_watcher_interface){
		file_watcher_interface->destroy(file_watcher);
		delete file_watcher_interface;
	}
	   loaderInterface->destroy( loader );
}

bool pal::ApiLoader::checkReload(){
	bool result= false;
	if (file_watcher){
		file_watcher_interface->poll_notifications(file_watcher);
	}
	return result;
}

// ----------------------------------------------------------------------
#endif //__cplusplus
