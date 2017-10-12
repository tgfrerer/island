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

struct Loader_o;

struct api_loader_i {
	Loader_o *( *create )( const char *path_ );
	void ( *destroy )( Loader_o *obj );

	bool ( *register_api )( Loader_o *obj, void *api_reg_fun );
	bool ( *load )( Loader_o *obj );
};

bool register_api_loader_i( api_loader_i *api );

#ifdef __cplusplus
}
#endif // end __cplusplus, end extern "C"

#endif
