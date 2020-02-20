#ifndef GUARD_API_REGISTRY_HPP
#define GUARD_API_REGISTRY_HPP

/*

  Registry is a global, canonical, table of apis, indexed through their type.

  The registry may be included from any compilation unit / .cpp file,
  in order to get hold of the current function pointers to api methods.

  Indexing by type works via lookup of a `static const char* id`
  field, which each api must provide. As this is effectively
  an immutable pointer to a string literal, the pointer address
  will be available for the duration of the program, and it is assumed
  to be unique.

*/

#include <stdint.h>
#include <cstddef> // for size_t
#include <assert.h>

#ifdef __cplusplus
#	define ISL_API_ATTR extern "C"
#else
#	define ISL_API_ATTR
#endif

#include "hash_util.h"

struct pal_api_loader_i;
struct pal_api_loader_o;

#ifdef __cplusplus
extern "C" {
#endif

ISL_API_ATTR void *pal_registry_get_api( uint64_t id, const char *debugName );

// creates
ISL_API_ATTR void *pal_registry_create_api( uint64_t id, size_t apiStructSize, const char *debugName );

ISL_API_ATTR void        update_argument_name_table( const char *source, uint64_t value );
ISL_API_ATTR char const *get_argument_name_from_hash( uint64_t value );

#ifdef __cplusplus
} // extern "C"

class Registry {

	template <typename T>
	inline static constexpr auto getPointerToStaticRegFun() noexcept {
		return T::pRegFun;
	}

	static bool loaderCallback( const char *, void * );

	struct callback_params_o; //ffdecl.

	// We need these loader-related methods so we can keep this header file opaque,
	// i.e. we don't want to include ApiLoader.h in this header file - as this header
	// file will get included by lots of other headers.
	//
	// This is messy, I know, but since the templated method addApiDynamic must live
	// in the header file, these methods must be declared here, too.

	static pal_api_loader_i *getLoaderInterface();
	static pal_api_loader_o *createLoader( pal_api_loader_i *loaderInterface, const char *libPath_ );
	static void              loadApi( pal_api_loader_i *loaderInterface, pal_api_loader_o *loader );
	static void              registerApi( pal_api_loader_i *loaderInterface, pal_api_loader_o *loader, void *api, const char *api_register_fun_name );
	static void              loadLibraryPersistently( pal_api_loader_i *loaderInterface, const char *libName_ );

	static callback_params_o *        create_callback_params( pal_api_loader_i *loaderInterface, pal_api_loader_o *loader, void *api, const char *lib_register_fun_name );
	static struct dynamic_api_info_o *create_dynamic_api_info( const char *id );

	static const char *dynamic_api_info_get_module_path( const struct dynamic_api_info_o * );
	static const char *dynamic_api_info_get_modules_dir( const struct dynamic_api_info_o * );
	static const char *dynamic_api_info_get_register_fun_name( const struct dynamic_api_info_o * );
	static void        destroy_dynamic_api_info( dynamic_api_info_o * );

	static int addWatch( const char *watchedPath, callback_params_o *settings );

	template <typename T>
	inline static constexpr auto getId() noexcept {
		return T::id;
	}

  public:
	template <typename T>
	static constexpr T *getApi() noexcept {
		return static_cast<T *>( pal_registry_get_api( hash_64_fnv1a_const( getId<T>() ), getId<T>() ) );
	}

	template <typename T>
	static T *addApiStatic( bool force = false ) {
		static auto api = getApi<T>();
		// We assume failed map lookup returns a pointer which is
		// initialised to be a nullptr.
		if ( api == nullptr || force == true ) {
			api = static_cast<T *>( pal_registry_create_api( hash_64_fnv1a_const( getId<T>() ), sizeof( T ), getId<T>() ) ); // < store api in registry lookup table
			( *getPointerToStaticRegFun<T>() )( api );                                                                       // < call registration function on api (this fills in the api's function pointers)
		}
		return api;
	}

	template <typename T>
	static T *addApiDynamic( bool shouldWatchForAutoReload = false ) {

		// Because this is a templated function, there will be
		// static memory allocated for each type this function will get
		// fleshed out with.
		// We want this, as we use the addresses of these static variables
		// for the life-time of the application.

		// Get pointer to api
		// - returns nullptr if pointer was not yet not set
		//
		T *api = getApi<T>();

		if ( api == nullptr ) {

			dynamic_api_info_o *info                  = create_dynamic_api_info( getId<T>() );
			auto                api_module_path       = dynamic_api_info_get_module_path( info );
			auto                api_register_fun_name = dynamic_api_info_get_register_fun_name( info );

			pal_api_loader_i *loaderInterface = getLoaderInterface();
			pal_api_loader_o *loader          = createLoader( loaderInterface, api_module_path );

			//api = new T(); // Allocate memory for api interface object. NOTE: we "leak" this on purpose - this is where the api interface lives.

			// Important to store api back to table here *before* calling loadApi, as loadApi might recursively add other apis
			// which would have the effect of allocating more than one copy of the api
			//
			api = static_cast<T *>( pal_registry_create_api( hash_64_fnv1a_const( getId<T>() ), sizeof( T ), getId<T>() ) );

			loadApi( loaderInterface, loader );
			registerApi( loaderInterface, loader, api, api_register_fun_name );

			// ----
			if ( shouldWatchForAutoReload ) {
				callback_params_o *callbackParams = create_callback_params( loaderInterface, loader, api, api_register_fun_name );
				auto               watchId        = addWatch( api_module_path, callbackParams );
			}

		} else {
			// TODO: we should warn that this api was already added.
		}

		return api;
	}

	static void pollForDynamicReload();

	static void loadLibraryPersistently( const char *libName_ ) {
		static pal_api_loader_i *loader = getLoaderInterface();
		loadLibraryPersistently( loader, libName_ );
	}
};

// ---------- utilities

struct NoCopy {

	NoCopy() = default;

	// copy assignment operator
	NoCopy &operator=( const NoCopy &rhs ) = delete;

	// copy constructor
	NoCopy( const NoCopy &rhs ) = delete;

  protected:
	~NoCopy() = default;
};

struct NoMove {

	NoMove() = default;

	// move assignment operator
	NoMove &operator=( NoMove &&rhs ) = delete;

	// move constructor
	NoMove( const NoMove &&rhs ) = delete;

  protected:
	~NoMove() = default;
};

#endif // __cplusplus

//#ifndef LE_DEFINE_HANDLE_GUARD
//#	define LE_DEFINE_HANDLE( object ) typedef struct object##_T *object;
//#	define LE_DEFINE_HANDLE_GUARD
//#endif

#endif // GUARD_API_REGISTRY_HPP
