#ifndef GUARD_API_LOADER_H
#define GUARD_API_LOADER_H

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

#ifdef __cplusplus
extern "C" {
#endif // end __cplusplus

struct pal_api_loader_i;
struct pal_api_loader_o;
struct pal_file_watcher_i;
struct pal_file_watcher_o;

bool pal_register_api_loader_i( pal_api_loader_i *api );

// clang-format off
struct pal_api_loader_i {

	static constexpr auto id      = "pal_api_loader";
	static constexpr auto pRegFun = pal_register_api_loader_i;

	pal_api_loader_o * ( *create )               ( const char *path_ );
	void               ( *destroy )              ( pal_api_loader_o *obj );
	bool               ( *register_api )         ( pal_api_loader_o *obj, void *api_interface, const char *api_registry_name );
	bool               ( *load )                 ( pal_api_loader_o *obj );
	bool               (* loadLibraryPersistent) (const char* libName_);
};
// clang-format on

// ----------------------------------------------------------------------

#ifdef __cplusplus
} // end extern "C"
#endif // end __cplusplus,

#endif // GUARD_API_LOADER_H
