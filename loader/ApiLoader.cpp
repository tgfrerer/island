#include "ApiLoader.h"

#include <dlfcn.h>
#include <iostream>

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

struct Loader {
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

static Loader *create( const char *path_ ) {
	Loader *tmp = new Loader{};
	tmp->mPath  = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void destroy( Loader *obj ) {
	unload_library( obj->mHandle );
	delete obj;
};

// ----------------------------------------------------------------------

static bool load( Loader *obj ) {
	unload_library( obj->mHandle );
	obj->mHandle = load_library( obj->mPath );
	return ( obj->mHandle != nullptr );
}

// ----------------------------------------------------------------------

static bool register_api( Loader *obj, void *api_interface, const char *api_registry_name ) {
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
