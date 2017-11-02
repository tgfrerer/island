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

struct api_loader_o;
struct file_watcher_i;
struct file_watcher_o;

struct api_loader_i {
	api_loader_o *( *create )( const char *path_ );
	void ( *destroy )( api_loader_o *obj );

	bool ( *register_api )( api_loader_o *obj, void *api_interface, const char *api_registry_name );
	bool ( *register_static_api )( const char* api_name, void (*register_api_fun_p)(void*), void* api_interface );
	bool ( *load )( api_loader_o *obj );

	struct file_watcher {
		file_watcher_i* interface;
		file_watcher_o* watcher;
	} watcher;
};

bool register_api_loader_i( api_loader_i *api );


// ----------------------------------------------------------------------

#ifdef __cplusplus

namespace pal{

class ApiLoader {
	api_loader_i *  loaderInterface        = nullptr;
	api_loader_o *  loader                 = nullptr;
	void *          api                    = nullptr;
	const char *    api_register_fun_name  = nullptr;
	file_watcher_i *file_watcher_interface = nullptr;
	file_watcher_o *file_watcher           = nullptr;

	static bool loadLibraryCallback( void *userData ) {
		auto self = reinterpret_cast<ApiLoader *>( userData );
		self->loaderInterface->load( self->loader );
		return self->loaderInterface->register_api( self->loader, self->api, self->api_register_fun_name );
	}

  public:
	ApiLoader( api_loader_i *loaderInterface_, void *apiInterface_, const char *libpath_, const char *api_register_fun_name_ )
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
