#include "le_api_loader.h"

#ifdef LE_API_LOADER_IMPL_WIN32

#	pragma comment( lib, "Rstrtmgr.lib" )
#	pragma comment( lib, "ntdll.lib" )
#	include <windows.h>
#	include <RestartManager.h>
#	include <stdio.h>
#	include <winternl.h>
#	include <vector>
#	include <string>
#	include <cassert>
#	include <memory>
#	include "assert.h"
#	include <string>
#	include <iostream>

struct le_file_watcher_o;

#	define LOG_PREFIX_STR "LOADER"

// declare function pointer type to register_fun function
typedef void ( *register_api_fun_p_t )( void * );

struct le_module_loader_o {
	std::string        mApiName;
	std::string        mRegisterApiFuncName;
	std::string        mPath;
	void *             mLibraryHandle = nullptr;
	le_file_watcher_o *mFileWatcher   = nullptr;
};

bool grab_and_drop_pdb_handle( char const *path ); // ffdecl

// ----------------------------------------------------------------------

static void unload_library( void *handle_, const char *path ) {
	if ( handle_ ) {
		auto result = FreeLibrary( static_cast<HMODULE>( handle_ ) );
		fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s, handle: %p \n", LOG_PREFIX_STR, "", "Close Module", path, handle_ );

		if ( 0 == result ) {
			auto error = GetLastError();
			fprintf( stderr, "[ %-20.20s ] %10s %-20s: handle: %p, error: %ul\n", LOG_PREFIX_STR, "ERROR", "FreeLibrary", handle_, error );
		} else {
			grab_and_drop_pdb_handle( path );
			// delete_old_pdb
		}
	}
}

// ----------------------------------------------------------------------

static void *load_library( const char *lib_name ) {

	// fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s\n", LOG_PREFIX_STR, "", "Load Module", lib_name );
	// fflush( stdout );

	void *handle = LoadLibrary( lib_name );
	if ( handle == NULL ) {
		auto loadResult = GetLastError();

		fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s, result: %ul\n", LOG_PREFIX_STR, "ERROR", "Loading Module", lib_name, loadResult );
		fflush( stdout );

		exit( 1 );
	} else {
		fprintf( stdout, "[ %-20.20s ] %10s %-20s: %-50s, handle: %p\n", LOG_PREFIX_STR, "OK", "Loaded Module", lib_name, handle );
		fflush( stdout );
	}
	return handle;
}

// ----------------------------------------------------------------------

static bool load_library_persistent( const char *lib_name ) {
	return false;
}

// ----------------------------------------------------------------------

static le_module_loader_o *instance_create( const char *path_ ) {
	le_module_loader_o *tmp = new le_module_loader_o{};
	tmp->mPath              = path_;
	return tmp;
};

// ----------------------------------------------------------------------

static void instance_destroy( le_module_loader_o *obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath.c_str() );
	delete obj;
};

// ----------------------------------------------------------------------

static bool load( le_module_loader_o *obj ) {
	unload_library( obj->mLibraryHandle, obj->mPath.c_str() );
	obj->mLibraryHandle = load_library( obj->mPath.c_str() );
	return ( obj->mLibraryHandle != nullptr );
}

// ----------------------------------------------------------------------

static bool register_api( le_module_loader_o *obj, void *api_interface, const char *register_api_fun_name ) {
	// define function pointer we will use to initialise api
	register_api_fun_p_t fptr;
	// load function pointer to initialisation method

	FARPROC fp;

	fp = GetProcAddress( ( HINSTANCE )obj->mLibraryHandle, register_api_fun_name );
	if ( !fp ) {
		std::cerr << LOG_PREFIX_STR "ERROR: " << GetLastError() << std::endl;
		assert( false );
		return false;
	}
	fptr = ( register_api_fun_p_t )fp;
	( *fptr )( api_interface );
	return true;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_module_loader, p_api ) {
	auto  api                          = static_cast<le_module_loader_api *>( p_api );
	auto &loader_i                     = api->le_module_loader_i;
	loader_i.create                    = instance_create;
	loader_i.destroy                   = instance_destroy;
	loader_i.load                      = load;
	loader_i.register_api              = register_api;
	loader_i.load_library_persistently = load_library_persistent;
}

bool grab_and_drop_pdb_handle( char const *path ) {

	return true;
}

#endif