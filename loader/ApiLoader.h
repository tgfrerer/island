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

struct pal_api_loader_o;
struct pal_file_watcher_i;
struct pal_file_watcher_o;

struct pal_api_loader_i {
	static constexpr auto id = "pal_api_loader";
	pal_api_loader_o *( *create )( const char *path_ );
	void ( *destroy )( pal_api_loader_o *obj );

	bool ( *register_api )( pal_api_loader_o *obj, void *api_interface, const char *api_registry_name );
	bool ( *register_static_api )( void ( *register_api_fun_p )( void * ), void *api_interface );
	bool ( *load )( pal_api_loader_o *obj );

	struct file_watcher {
		pal_file_watcher_i *interface;
		pal_file_watcher_o *watcher;
	} watcher;
};

bool pal_register_api_loader_i( pal_api_loader_i *api );

// ----------------------------------------------------------------------

#ifdef __cplusplus

namespace pal {

class ApiLoader {
	pal_api_loader_i *  loaderInterface        = nullptr;
	pal_api_loader_o *  loader                 = nullptr;
	void *              api                    = nullptr;
	const char *        api_register_fun_name  = nullptr;
	pal_file_watcher_i *file_watcher_interface = nullptr;
	pal_file_watcher_o *file_watcher           = nullptr;

	static bool loadLibraryCallback( void *userData ) {
		auto self = reinterpret_cast<ApiLoader *>( userData );
		self->loaderInterface->load( self->loader );
		return self->loaderInterface->register_api( self->loader, self->api, self->api_register_fun_name );
	}

  public:
	ApiLoader( pal_api_loader_i *loaderInterface_, void *apiInterface_, const char *libpath_, const char *api_register_fun_name_ )
	    : loaderInterface( loaderInterface_ )
	    , loader( loaderInterface->create( libpath_ ) )
	    , api( apiInterface_ )
	    , api_register_fun_name( api_register_fun_name_ ) {
	}

	bool loadLibrary();
	bool checkReload();

	~ApiLoader();
};

} // end namespace pal
} // end extern "C"
#endif // end __cplusplus,

#endif // GUARD_API_LOADER_H
