#ifndef GUARD_API_LOADER_H
#define GUARD_API_LOADER_H

/*

The pal api loader lets us load apis which obey the following protocol:

A library *must* declare, and *must*, in its translation unit, define a method:

    void register_api(void *api);

This method may  be called by users of the API to populate a struct with
function pointers through which the methods which the api exposes may be called.

This method must accept a pointer to struct of the api's implementation type -
this type *must* be declared in the api's header file, and is effectively a
table of function pointers which, together, declare the api.

*/

#ifdef __cplusplus
extern "C" {
#endif // end __cplusplus

struct Loader;

struct api_loader_i {
	Loader *( *create )( const char *path_ );
	void ( *destroy )( Loader *obj );

	bool ( *register_api )( Loader *obj, void *api_interface, const char *api_registry_name );
	bool ( *load )( Loader *obj );
};

bool register_api_loader_i( api_loader_i *api );

#ifdef __cplusplus
}
#endif // end __cplusplus, end extern "C"

#endif
