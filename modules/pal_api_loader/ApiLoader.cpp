#include "ApiLoader.h"

#include <dlfcn.h>
#include <link.h>

#include <iostream>

#define LOG_PREFIX_STR "LOADER"

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

struct pal_api_loader_o {
	const char *        mApiName             = nullptr;
	const char *        mRegisterApiFuncName = nullptr;
	const char *        mPath                = nullptr;
	void *              mLibraryHandle       = nullptr;
	pal_file_watcher_o *mFileWatcher         = nullptr;
};

// ----------------------------------------------------------------------

static void unload_library( void *handle_, const char *path ) {
	if ( handle_ ) {
		auto result = dlclose( handle_ );

		fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s, handle: %p \n", LOG_PREFIX_STR, "", "Close Module", path, handle_ );

		if ( result ) {
			fprintf( stderr, "[ %-20.20s ] %10s %-20s: handle: %p, error: %s\n", LOG_PREFIX_STR, "ERROR", "dlclose", handle_, dlerror() );
			fflush( stderr );
		}
		auto handle = dlopen( path, RTLD_NOLOAD );
		if ( handle ) {
			std::cerr << LOG_PREFIX_STR "ERROR dlclose: '" << path << "', "
			          << "handle " << std::hex << ( void * )handle << " staying resident.";
		}
		handle_ = nullptr;
	}
}

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {

	// fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s\n", LOG_PREFIX_STR, "", "Load Module", lib_name );
	fflush( stdout );

	void *handle = dlopen( lib_name, RTLD_LAZY | RTLD_LOCAL );

	if ( !handle ) {
		auto loadResult = dlerror();
		std::cerr << "ERROR: " << loadResult << std::endl
		          << std::flush;
		exit( 1 );
	} else {
		fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s, handle: %p\n", LOG_PREFIX_STR, "OK", "Loaded Module", lib_name, handle );
		fflush( stdout );
	}

	return handle;
}

// ----------------------------------------------------------------------

static bool load_library_persistent( const char *lib_name ) {

	// We persistently load symbols for libraries upon which our plugins depend -
	// and make sure these are loaded with the NO_DELETE flag so that dependent
	// libraries will not be reloaded if a module which uses the library is unloaded.
	//
	// This is necessary since with linux linking against a library does not mean
	// its symbols are actually loaded, symbols are loaded lazily by default,
	// which means they are only loaded when the library is first used by the module
	// against which the library was linked.

	// FIXME: what we expect: if a library is already loaded, we should get a valid handle
	// what we get: always nullptr

	void *lib_handle = dlopen( lib_name, RTLD_NOLOAD | RTLD_GLOBAL | RTLD_NODELETE );
	if ( !lib_handle ) {
		lib_handle = dlopen( lib_name, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE );
		if ( !lib_handle ) {
			auto loadResult = dlerror();
			fprintf( stderr, "[ %-20.20s ] %10s %-20s: %-50s, result: %s\n", LOG_PREFIX_STR, "ERROR", "Load Library", lib_name, loadResult );
			fflush( stderr );
			exit( 1 );
		} else {
			fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s, handle: %p\n", LOG_PREFIX_STR, "", "Keep Library", lib_name, lib_handle );
			fflush( stdout );
		}
	}
	return ( lib_handle != nullptr );
}

// ----------------------------------------------------------------------

static pal_api_loader_o *instance_create( const char *path_ ) {
	pal_api_loader_o *tmp = new pal_api_loader_o{};
	tmp->mPath            = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void instance_destroy( pal_api_loader_o *obj ) {
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
		std::cerr << LOG_PREFIX_STR "ERROR: " << dlerror() << std::endl;
		return false;
	}
	// Initialize the API. This means telling the API to populate function
	// pointers inside the struct which we are passing as parameter.
	fprintf( stderr, "[ %-20.20s ] %10s %-20s: %s\n", LOG_PREFIX_STR, "", "Register Module", register_api_fun_name );

	( *fptr )( api_interface );
	return true;
}

// ----------------------------------------------------------------------

bool pal_register_api_loader_i( pal_api_loader_i *api ) {
	api->create                = instance_create;
	api->destroy               = instance_destroy;
	api->load                  = load;
	api->register_api          = register_api;
	api->loadLibraryPersistent = load_library_persistent;
	return true;
};

// ----------------------------------------------------------------------
// LINUX: these methods are to audit runtime dyanmic library linking and loading.
//
// To enable, start app with environment variable `LD_AUDIT` set to path of
// libpal_api_loader.so:
//
//		EXPORT LD_AUDIT=./modules/libpal_api_loader.so

extern "C" unsigned int
la_version( unsigned int version ) {
	std::cout << "\t AUDIT: loaded auditing interface" << std::endl;
	std::cout << std::flush;
	return version;
}

extern "C" unsigned int
la_objclose( uintptr_t *cookie ) {
	std::cout << "\t AUDIT: objclose: " << std::hex << cookie << std::endl;
	std::cout << std::flush;
	return 0;
}

extern "C" void
la_activity( uintptr_t *cookie, unsigned int flag ) {
	printf( "\t AUDIT: la_activity(): cookie = %p; flag = %s\n", cookie,
	        ( flag == LA_ACT_CONSISTENT ) ? "LA_ACT_CONSISTENT" : ( flag == LA_ACT_ADD ) ? "LA_ACT_ADD" : ( flag == LA_ACT_DELETE ) ? "LA_ACT_DELETE" : "???" );
	std::cout << std::flush;
};

extern "C" unsigned int
la_objopen( struct link_map *map, Lmid_t lmid, uintptr_t *cookie ) {
	printf( "\t AUDIT: la_objopen(): loading \"%s\"; lmid = %s; cookie=%p\n",
	        map->l_name,
	        ( lmid == LM_ID_BASE ) ? "LM_ID_BASE" : ( lmid == LM_ID_NEWLM ) ? "LM_ID_NEWLM" : "???",
	        cookie );
	std::cout << std::flush;
	return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}

extern "C" char *
la_objsearch( const char *name, uintptr_t *cookie, unsigned int flag ) {
	printf( "\t AUDIT: la_objsearch(): name = %s; cookie = %p", name, cookie );
	printf( "; flag = %s\n",
	        ( flag == LA_SER_ORIG ) ? "LA_SER_ORIG" : ( flag == LA_SER_LIBPATH ) ? "LA_SER_LIBPATH" : ( flag == LA_SER_RUNPATH ) ? "LA_SER_RUNPATH" : ( flag == LA_SER_DEFAULT ) ? "LA_SER_DEFAULT" : ( flag == LA_SER_CONFIG ) ? "LA_SER_CONFIG" : ( flag == LA_SER_SECURE ) ? "LA_SER_SECURE" : "???" );

	return const_cast<char *>( name );
}
