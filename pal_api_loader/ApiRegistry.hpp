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

#ifdef __cplusplus
#	define ISL_API_ATTR extern "C"
#else
#	define ISL_API_ATTR
#endif

struct pal_api_loader_i;
struct pal_api_loader_o;

#ifdef __cplusplus
extern "C" {
#endif

ISL_API_ATTR void *pal_registry_get_api( const char *id );
ISL_API_ATTR void  pal_registry_set_api( const char *id, void *api );

#ifdef __cplusplus
} // extern "C"

class Registry {

	template <typename T>
	inline static constexpr auto getPointerToStaticRegFun() noexcept {
		return T::pRegFun;
	}

	static bool loaderCallback( const char *, void * );

	struct CallbackParams {
		pal_api_loader_i *loaderInterface;
		pal_api_loader_o *loader;
		void *            api;
		const char *      lib_register_fun_name;
	};

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

	static struct dynamic_api_info_o *create_dynamic_api_info( const char *id );
	static const char *               dynamic_api_info_get_module_path( const struct dynamic_api_info_o * );
	static const char *               dynamic_api_info_get_modules_dir( const struct dynamic_api_info_o * );
	static const char *               dynamic_api_info_get_register_fun_name( const struct dynamic_api_info_o * );
	static void                       destroy_dynamic_api_info( dynamic_api_info_o * );

	static int addWatch( const char *watchedPath, CallbackParams &settings );

	template <typename T>
	inline static constexpr auto getId() noexcept {
		return T::id;
	}

  public:
	template <typename T>
	static T *getApi() {
		return static_cast<T *>( pal_registry_get_api( getId<T>() ) );
	}

	template <typename T>
	static T *addApiStatic( bool force = false ) {
		static auto api = getApi<T>();
		// We assume failed map lookup returns a pointer which is
		// initialised to be a nullptr.
		if ( api == nullptr || force == true ) {
			// in case the api is a sub-module, it must be forced to update
			// as the main library from which it comes will have changed.
			if ( api != nullptr ) {
				delete api;
			}
			api = new T();
			( *getPointerToStaticRegFun<T>() )( api ); // < call registration function on api (this fills in the api's function pointers)
			pal_registry_set_api( getId<T>(), api );   // < store api in registry lookup table
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

		constexpr static auto apiName = getId<T>();
		static auto           api     = getApi<T>();

		if ( api == nullptr ) {

			static dynamic_api_info_o *info = create_dynamic_api_info( getId<T>() ); // note: we never destroy this object.

			static pal_api_loader_i *loaderInterface = getLoaderInterface();
			static pal_api_loader_o *loader          = createLoader( loaderInterface, dynamic_api_info_get_module_path( info ) );

			api = new T();
			loadApi( loaderInterface, loader );
			registerApi( loaderInterface, loader, api, dynamic_api_info_get_register_fun_name( info ) );

			pal_registry_set_api( getId<T>(), api );

			// ----
			if ( shouldWatchForAutoReload ) {
				static CallbackParams callbackParams = {loaderInterface, loader, api, dynamic_api_info_get_register_fun_name( info )};
				// TODO: We keep watchId static so that a watch is only created once per type T.
				// ideally, if we ever wanted to be able to remove watches, we'd keep the watchIds in a
				// table, similar to the apiTable.
				static int watchId = addWatch( dynamic_api_info_get_module_path( info ), callbackParams );
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

#ifndef LE_DEFINE_HANDLE_GUARD
#	define LE_DEFINE_HANDLE( object ) typedef struct object##_T *object;
#	define LE_DEFINE_HANDLE_GUARD
#endif

#endif // GUARD_API_REGISTRY_HPP
