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

static void unload_library( void *handle_, const char* path) {
	if ( handle_ ) {
		auto result = dlclose( handle_ );
		std::cout << "Closed library handle: " << std::hex << handle_ << " - Result: " << result << std::endl;
		if (result) {
			std::cerr << "ERROR dlclose: " << dlerror() << std::endl;
		}
		auto handle = dlopen(path,RTLD_NOLOAD);
		if (handle){
			std::cerr << "ERROR dlclose: " << "handle "<< std::hex << (void*)handle << " staying resident.";
		}

		handle_ = nullptr;
	}
}

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {


	std::cout << "Loading Library    : '" << lib_name << "'" << std::endl;

	// We may pre-load any library dependencies so that these won't get deleted
	// when our main plugin gets reloaded.
	//
	// TODO: allow modules to specify resident library dependencies
	//
	// We manually load symbols for libraries upon which our plugins depend -
	// and make sure these are loaded with the NO_DELETE flag so that dependent
	// libraries will not be reloaded if a module which uses the library is unloaded.
	//
	// This is necessary since with linux linking against a library does not mean
	// its symbols are actually loaded, symbols are loaded lazily by default,
	// which means they are only loaded when the library is first used by the module
	// against which the library was linked.



	static auto handleglfw = dlopen( "libglfw.so", RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE );
	static auto handlevk = dlopen( "libvulkan.so", RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE );

	void *handle = dlopen( lib_name, RTLD_NOW | RTLD_LOCAL );
	std::cout << "Open library handle: " << std::hex << handle << std::endl;

	if ( !handle ) {
		auto loadResult = dlerror();
		std::cerr << "ERROR: " << loadResult << std::endl;
	}

	return handle;
}

// ----------------------------------------------------------------------

static pal_api_loader_o *create( const char *path_ ) {
	pal_api_loader_o *tmp = new pal_api_loader_o{};
	tmp->mPath            = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void destroy( pal_api_loader_o *obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath );
	delete obj;
};

// ----------------------------------------------------------------------

static bool load( pal_api_loader_o *obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath );
	obj->mLibraryHandle = load_library( obj->mPath );
	return ( obj->mLibraryHandle != nullptr );
}

// ----------------------------------------------------------------------

static bool register_api( pal_api_loader_o *obj, void *api_interface, const char *register_api_fun_name ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;

	// load function pointer to initialisation method
	fptr = reinterpret_cast<register_api_fun_p_t>( dlsym( obj->mLibraryHandle, register_api_fun_name ) );
	if ( !fptr ) {
		std::cerr << "ERROR: " << dlerror() << std::endl;
		return false;
	}
	// Initialize the API. This means telling the API to populate function
	// pointers inside the struct which we are passing as parameter.
	std::cout << "Registering API via: '" << register_api_fun_name << "'" << std::endl;
//	std::cout << "fptr                  = " << std::hex << (void*)fptr << std::endl;
//	std::cout << "Api interface address = " << std::hex << api_interface << std::endl;
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
