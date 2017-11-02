#ifndef GUARD_API_REGISTRY_HPP
#define GUARD_API_REGISTRY_HPP

#include <unordered_map>

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

  public:
    template <typename T>
    inline static constexpr const char *getId() noexcept {
        return T::id;
    }

	template <typename T>
	inline static T *addApi() {
		auto api               = new T();
		apiTable[ getId<T>() ] = api;
		return api;
	}

	template <typename T>
	inline static T *getApi() {
		// WARNING: this will return a void* if nothing found!
		// TODO: add error checking if compiled in debug mode.
		return static_cast<T *>( apiTable[ getId<T>() ] );
	}
};

#endif // GUARD_API_REGISTRY_HPP
