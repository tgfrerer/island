#include "ApiLoader.h"

#include <dlfcn.h>
#include <iostream>

using namespace pal;

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {
	void *handle = dlopen( lib_name, RTLD_NOW );
	std::cout << "\topening library handle: " << std::hex << handle
	          << std::endl;

	if ( !handle ) {
		auto loadResult = dlerror( );
		std::cerr << "error: " << loadResult << std::endl;
	}

	return handle;
}

// ----------------------------------------------------------------------

static void unload_library( void *handle_ ) {
	if ( handle_ ) {
		std::cout << "\tclosing library handle: " << std::hex << handle_
		          << std::endl;
		dlclose( handle_ );
		handle_ = nullptr;
	}
}

// ----------------------------------------------------------------------

ApiLoader::ApiLoader( const char *path_ ) : mPath( path_ ), mHandle( nullptr ) {
	mHandle = load_library( mPath );
};

// ----------------------------------------------------------------------

ApiLoader::~ApiLoader( ) {
	unload_library( mHandle );
};

// ----------------------------------------------------------------------

bool ApiLoader::reload( ) {
	unload_library( mHandle );
	mHandle = load_library( mPath );
	return ( mHandle != nullptr );
}

// ----------------------------------------------------------------------

bool ApiLoader::register_api( void *api ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;
	// load function pointer to initialisation method
	fptr = reinterpret_cast< register_api_fun_p_t >(
	    dlsym( mHandle, "register_api" ) );
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
