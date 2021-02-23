#ifndef GUARD_API_LOADER_H
#define GUARD_API_LOADER_H

#include "le_core/le_core.h"
/*

The pal api loader lets us load apis which obey the following protocol:

A library *must* declare, and *must*, in its translation unit, define a method:

    void register_api(void *api);

This method may be called by users of the API to populate a struct with
function pointers through which the methods which the api exposes may be called.

This method must accept a pointer to struct of the api's implementation type -
this type *must* be declared in the api's header file, and is effectively a
table of function pointers which, together, declare the api.

*/

#if defined( __linux__ )
#	define LE_API_LOADER_IMPL_LINUX
#elif defined( _WIN32 )
#	define LE_API_LOADER_IMPL_WIN32
#endif

#ifdef __cplusplus
extern "C" {
#endif // end __cplusplus

struct le_module_loader_o;

// clang-format off
struct le_module_loader_api {

	struct le_module_loader_interface_t {
	le_module_loader_o * ( *create )                 ( const char *path_ );
	void               ( *destroy )                  ( le_module_loader_o *obj );
	bool               ( *register_api )             ( le_module_loader_o *obj, void *api_interface, const char *api_registry_name );
	bool               ( *load )                     ( le_module_loader_o *obj );
	bool               ( *load_library_persistently) (const char* libName_);
	};
	
	le_module_loader_interface_t le_module_loader_i;

};
// clang-format on

LE_MODULE( le_module_loader );

// ----------------------------------------------------------------------

#ifdef __cplusplus
} // end extern "C"
#endif // end __cplusplus,

#endif // GUARD_API_LOADER_H
