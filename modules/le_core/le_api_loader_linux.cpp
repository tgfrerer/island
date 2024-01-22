#include "le_api_loader.h"

#ifdef LE_API_LOADER_IMPL_LINUX

#	include <dlfcn.h>

#	include "assert.h"
#	include <string>
#	include <iostream>
#	include "le_log.h"
#	include <cstdarg>
#	include <stdio.h>

struct le_file_watcher_o;

#	define LOG_PREFIX_STR "loader"

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void* );

struct le_module_loader_o {
	std::string        mApiName;
	std::string        mRegisterApiFuncName;
	std::string        mPath;
	void*              mLibraryHandle = nullptr;
	le_file_watcher_o* mFileWatcher   = nullptr;
};

// ----------------------------------------------------------------------

static le_log_channel_o* get_logger( le_log_channel_o** logger ) {
	// First, initialise logger to nullptr so that we can test against this when the logger module gets loaded.
	*logger = nullptr;
	// Next call will initialise logger by calling into this library.
	*logger = le_log::api->get_channel( LOG_PREFIX_STR );
	return *logger;
};

static le_log_channel_o* logger = get_logger( &logger );

// ----------------------------------------------------------------------

static void log_printf( FILE* f_out, const char* msg, ... ) {
	fprintf( f_out, "[ %-35s ] ", LOG_PREFIX_STR );
	va_list arglist;
	va_start( arglist, msg );
	vfprintf( f_out, msg, arglist );
	va_end( arglist );
	fprintf( f_out, "\n" );
	fflush( f_out );
}

template <typename... Args>
static void log_debug( const char* msg, Args&&... args ) {
	if ( logger && le_log::le_log_channel_i.info ) {
		le_log::le_log_channel_i.debug( logger, msg, std::move( args )... );
	} else {
#	if defined( LE_LOG_LEVEL ) && ( LE_LOG_LEVEL <= LE_LOG_DEBUG )
		log_printf( stdout, msg, args... );
#	endif
	}
}

template <typename... Args>
static void log_info( const char* msg, Args&&... args ) {
	if ( logger && le_log::le_log_channel_i.info ) {
		le_log::le_log_channel_i.info( logger, msg, std::move( args )... );
	} else {
		log_printf( stdout, msg, args... );
	}
}

// ----------------------------------------------------------------------
template <typename... Args>
static void log_error( const char* msg, Args&&... args ) {
	if ( logger && le_log::le_log_channel_i.error ) {
		le_log::le_log_channel_i.error( logger, msg, std::move( args )... );
	} else {
		log_printf( stderr, msg, args... );
	}
}

// ----------------------------------------------------------------------

static void unload_library( void* handle_, const char* path ) {
	if ( handle_ ) {
		auto result = dlclose( handle_ );

		// we must detect whether the module that was unloaded was the logger module -
		// in which case we can't log using the logger module.

		log_debug( "[%-10s] %-20s: %-50s, handle: %p ", "OK", "Close Module", path, handle_ );

		if ( result ) {
			auto error = dlerror();
			log_error( "%10s %-20s: handle: %p, error: %s", "ERROR", "dlclose", handle_, error );
		}

		auto handle = dlopen( path, RTLD_NOLOAD );
		if ( handle ) {
			log_error( "ERROR dlclose: '%s'', handle: %p staying resident", path, ( void* )handle );
		}
	}
}

// ----------------------------------------------------------------------

static void* load_library( const char* lib_name ) {

	void* handle = dlopen( lib_name, RTLD_LAZY | RTLD_LOCAL );

	if ( !handle ) {
		auto loadResult = dlerror();
		log_error( "FATAL ERROR: %s", loadResult );
		exit( 1 );
	} else {
		log_info( "[%-10s] %-20s: %-50s, handle: %p", "OK", "Loaded Module", lib_name, handle );
	}
	return handle;
}

// ----------------------------------------------------------------------

static void* load_library_persistent( const char* lib_name ) {
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
	void* lib_handle = dlopen( lib_name, RTLD_NOLOAD | RTLD_GLOBAL | RTLD_NODELETE );
	if ( !lib_handle ) {
		lib_handle = dlopen( lib_name, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE );
		if ( !lib_handle ) {
			auto loadResult = dlerror();
			log_error( "[%-10s] %-20s: %-50s, result: %s", "ERROR", "Load Library", lib_name, loadResult );
			exit( 1 );
		} else {
			log_debug( "[%-10s] %-20s: %-50s, handle: %p", "OK", "Keep Library", lib_name, lib_handle );
		}
	}
	return lib_handle;
}

// ----------------------------------------------------------------------

static le_module_loader_o* instance_create( const char* path_ ) {
	le_module_loader_o* tmp = new le_module_loader_o{};
	tmp->mPath              = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void instance_destroy( le_module_loader_o* obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath.c_str() );
	delete obj;
};

// ----------------------------------------------------------------------

static bool load( le_module_loader_o* obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath.c_str() );
	obj->mLibraryHandle = load_library( obj->mPath.c_str() );
	return ( obj->mLibraryHandle != nullptr );
}

// ----------------------------------------------------------------------

static bool register_api( le_module_loader_o* obj, void* api_interface, const char* register_api_fun_name ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;
	// load function pointer to initialisation method
	fptr = reinterpret_cast<register_api_fun_p_t>( dlsym( obj->mLibraryHandle, register_api_fun_name ) );
	if ( !fptr ) {
		log_error( "ERROR: '%s'", dlerror() );
		assert( false );
		return false;
	}
	// Initialize the API. This means telling the API to populate function
	// pointers inside the struct which we are passing as parameter.
	log_debug( "Register Module: '%s'", register_api_fun_name );

	( *fptr )( api_interface );
	return true;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_module_loader, p_api ) {
	auto  api                          = static_cast<le_module_loader_api*>( p_api );
	auto& loader_i                     = api->le_module_loader_i;
	loader_i.create                    = instance_create;
	loader_i.destroy                   = instance_destroy;
	loader_i.load                      = load;
	loader_i.register_api              = register_api;
	loader_i.load_library_persistently = load_library_persistent;
}

/*

  Debugging Hot-realoading issues:

  In order to debug hot-reloading or linker issues on Linux it can be useful to use rtld-audit.
  See rtld-audit (7) manpage.

  See README_hotreload_debugging.md

*/

#endif
