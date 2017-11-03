#include "ApiLoader.h"

#include <dlfcn.h>
#include <iostream>

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

struct pal_api_loader_o {
	const char *        mApiName             = nullptr;
	const char *        mRegisterApiFuncName = nullptr;
	const char *        mPath                = nullptr;
	void *              mLibraryHandle       = nullptr;
	Pal_File_Watcher_o *mFileWatcher         = nullptr;
};

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {
	std::cout << "Loading Library    : '" << lib_name << "'" << std::endl;
	void *handle = dlopen( lib_name, RTLD_NOW );
	std::cout << "Open library handle: " << std::hex << handle << std::endl;

	if ( !handle ) {
		auto loadResult = dlerror();
		std::cerr << "ERROR: " << loadResult << std::endl;
	}

	return handle;
}

// ----------------------------------------------------------------------

static void unload_library( void *handle_ ) {
	if ( handle_ ) {
		dlclose( handle_ );
		std::cout << "Closed library handle: " << std::hex << handle_ << std::endl;
		handle_ = nullptr;
	}
}

// ----------------------------------------------------------------------

static pal_api_loader_o *create( const char *path_ ) {
	pal_api_loader_o *tmp = new pal_api_loader_o{};
	tmp->mPath            = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void destroy( pal_api_loader_o *obj ) {
	unload_library( obj->mLibraryHandle );
	delete obj;
};

// ----------------------------------------------------------------------

static bool load( pal_api_loader_o *obj ) {
	unload_library( obj->mLibraryHandle );
	obj->mLibraryHandle = load_library( obj->mPath );
	return ( obj->mLibraryHandle != nullptr );
}

// ----------------------------------------------------------------------

static bool register_api( pal_api_loader_o *obj, void *api_interface, const char *api_registry_name ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;
	// load function pointer to initialisation method
	fptr = reinterpret_cast<register_api_fun_p_t>( dlsym( obj->mLibraryHandle, api_registry_name ) );
	if ( !fptr ) {
		std::cerr << "ERROR: " << dlerror() << std::endl;
		return false;
	}
	// Initialize the API. This means telling the API to populate function
	// pointers inside the struct which we are passing as parameter.
	std::cout << "Registering API via '" << api_registry_name << "'" << std::endl;
	( *fptr )( api_interface );
	return true;
}

// ----------------------------------------------------------------------

bool pal_register_api_loader_i( pal_api_loader_i *api ) {
	api->create       = create;
	api->destroy      = destroy;
	api->load         = load;
	api->register_api = register_api;
	return true;
};
