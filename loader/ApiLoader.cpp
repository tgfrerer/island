#include "ApiLoader.h"

#include <dlfcn.h>
#include <iostream>

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

struct Loader_o {
	const char *mPath = nullptr;
	void *mHandle     = nullptr;
};

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {
	void *handle = dlopen( lib_name, RTLD_NOW );
	std::cout << "\topening library handle: " << std::hex << handle << std::endl;

	if ( !handle ) {
		auto loadResult = dlerror( );
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

Loader_o *create( const char *path_ ) {
	Loader_o *tmp = new Loader_o{};
	tmp->mPath    = path_;
	return tmp;
};

// ----------------------------------------------------------------------

void destroy( Loader_o *obj ) {
	unload_library( obj->mHandle );
	delete obj;
};

// ----------------------------------------------------------------------

bool load( Loader_o *obj ) {
	unload_library( obj->mHandle );
	obj->mHandle = load_library( obj->mPath );
	return ( obj->mHandle != nullptr );
}

// ----------------------------------------------------------------------

bool register_api( Loader_o *obj, void *api ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;
	// load function pointer to initialisation method
	fptr = reinterpret_cast< register_api_fun_p_t >( dlsym( obj->mHandle, "register_api" ) );
	if ( !fptr ) {
		std::cerr << "error: " << dlerror( ) << std::endl;
		return false;
	}
	// Initialize the API. This means telling the API to populate function
	// pointers inside the struct which we are passing as parameter.
	( *fptr )( api );
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
