#ifndef GUARD_API_REGISTRY_HPP
#define GUARD_API_REGISTRY_HPP

#include <unordered_map>
#include "loader/ApiLoader.h"
#include <string>

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

class Registry {
	static std::unordered_map<const char *, void *> apiTable;

	template <typename T>
	inline static constexpr auto getPointerToStaticRegFun() noexcept {
		return T::pRegFun;
	}

  public:
    template <typename T>
    inline static constexpr auto getId() noexcept {
        return T::id;
    }

	template <typename T>
	static T *addApiStatic() {
		auto api = static_cast<T *>( apiTable[ getId<T>() ] );
		// We assume failed map lookup returns a pointer which is
		// initialised to be a nullptr.
		if ( api == nullptr ) {
			api = new T();
			( *getPointerToStaticRegFun<T>() )( api );
			apiTable[ getId<T>() ] = api;
		}
		return api;
	}

	template <typename T>
	static T *addApiDynamic() {

		// TODO: we need to add file watcher hook for when api gets reloaded.
		// We should be able to create a table of watched apis,
		// and iterate over all file hooks with all watched apis.

		static auto apiName_ = getId<T>();
		static auto api      = static_cast<T *>( apiTable[ apiName_ ] );

		if ( api == nullptr ) {

			static const std::string lib_path              = "./" + std::string{apiName_} + "/lib" + std::string{apiName_} + ".so";
			static const std::string lib_register_fun_name = "register_" + std::string{apiName_} + "_api";

			static pal_api_loader_i *loaderInterface = Registry::addApiStatic<pal_api_loader_i>();
			static pal_api_loader_o *loader          = loaderInterface->create( lib_path.c_str() );

			api = new T();
			loaderInterface->load( loader );
			loaderInterface->register_api( loader, api, lib_register_fun_name.c_str() );
			apiTable[ getId<T>() ] = api;
		} else {
			// todo: we should warn that this api was already added.
		}

		return api;
	}

	template <typename T>
	static T *getApi() {
		// WARNING: this will return a void* if nothing found!
		// TODO: add error checking if compiled in debug mode.
		return static_cast<T *>( apiTable[ getId<T>() ] );
	}
};

#endif // GUARD_API_REGISTRY_HPP
