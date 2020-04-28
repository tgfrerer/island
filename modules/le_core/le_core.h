#ifndef GUARD_API_REGISTRY_HPP
#define GUARD_API_REGISTRY_HPP

#include <stdint.h>
#include <stddef.h> // for size_t

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

#ifdef __cplusplus
#	define ISL_API_ATTR extern "C"
#else
#	define ISL_API_ATTR
#endif

ISL_API_ATTR void  le_core_poll_for_module_reloads();
ISL_API_ATTR void *le_core_load_module_static( char const *module_name, void ( *module_reg_fun )( void * ), uint64_t api_size_in_bytes );
ISL_API_ATTR void *le_core_load_module_dynamic( char const *module_name, uint64_t api_size_in_bytes, bool should_watch );
ISL_API_ATTR bool  le_core_load_library_persistently( char const *library );

// For debug purposes

ISL_API_ATTR void        le_update_argument_name_table( const char *source, uint64_t value );
ISL_API_ATTR char const *le_get_argument_name_from_hash( uint64_t value );

// ---------- utilities

#define LE_OPAQUE_HANDLE( object ) typedef struct object##_t *object;

#define LE_MODULE_REGISTER_IMPL( x, api ) \
	ISL_API_ATTR void le_module_register_##x( void *api )

#define LE_MODULE( x )                                   \
	ISL_API_ATTR void  le_module_register_##x( void * ); \
	static char const *le_module_name_##x = #x

#define LE_MODULE_LOAD_DYNAMIC( x )                                                   \
	static x##_api const *x##_api_i = ( x##_api const * )le_core_load_module_dynamic( \
	    le_module_name_##x,                                                           \
	    sizeof( x##_api ),                                                            \
	    true )

#define LE_MODULE_LOAD_STATIC( x )                                                   \
	static x##_api const *x##_api_i = ( x##_api const * )le_core_load_module_static( \
	    le_module_name_##x,                                                          \
	    le_module_register_##x,                                                      \
	    sizeof( x##_api ) )

#ifdef PLUGINS_DYNAMIC
#	define LE_MODULE_LOAD_DEFAULT( x ) \
		LE_MODULE_LOAD_DYNAMIC( x )
#else
#	define LE_MODULE_LOAD_DEFAULT( x ) \
		LE_MODULE_LOAD_STATIC( x )
#endif

#ifdef __cplusplus

// ----------- c++ specific utilities

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

#endif // GUARD_API_REGISTRY_HPP
