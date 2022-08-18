#ifndef GUARD_API_REGISTRY_HPP
#define GUARD_API_REGISTRY_HPP

#include <stdint.h>
#include <stddef.h> // for size_t

/*

  The core at heart is a global, canonical, table of apis,
  indexed via a hash of their name.

  The core may be included from any compilation unit / .cpp file,
  so that any module may query the api of other currently loaded
  modules.

  When a module gets reloaded, the function pointers in the corresponding
  table entry are updated, and subsequent queries to the table entry will
  return updated function pointers.

*/

#define ISL_MAKE_VERSION( major, minor, patch ) \
	( ( ( ( uint32_t )( major ) ) << 22 ) | ( ( ( uint32_t )( minor ) ) << 12 ) | ( ( uint32_t )( patch ) ) )

#define ISL_ENGINE_VERSION ISL_MAKE_VERSION( 0, 2, 0 )
#define ISL_ENGINE_NAME "Project Island"

#if defined( WIN32 ) && defined( PLUGINS_DYNAMIC )
#	ifdef DLL_CORE_EXPORTS
#		define DLL_CORE_API __declspec( dllexport )
#	else
#		define DLL_CORE_API __declspec( dllimport )
#	endif
#	define DLL_EXPORT_PREFIX __declspec( dllexport )
#else
#	define DLL_CORE_API
#	define DLL_EXPORT_PREFIX
#endif

#ifdef __cplusplus
#	define ISL_API_ATTR extern "C"
#else
#	define ISL_API_ATTR
#endif

ISL_API_ATTR DLL_CORE_API void  le_core_poll_for_module_reloads();
ISL_API_ATTR DLL_CORE_API void* le_core_load_module_static( char const* module_name, void ( *module_reg_fun )( void* ), uint64_t api_size_in_bytes );
ISL_API_ATTR DLL_CORE_API void* le_core_load_module_dynamic( char const* module_name, uint64_t api_size_in_bytes, bool should_watch );
ISL_API_ATTR DLL_CORE_API void* le_core_load_library_persistently( char const* library );

// A globally available, persistent key-pointer store. Use this to store pointers which you want to keep
// across module reloads, for example.
ISL_API_ATTR DLL_CORE_API void** le_core_produce_dictionary_entry( uint64_t key );

// For debug purposes

ISL_API_ATTR DLL_CORE_API void        le_update_argument_name_table( const char* source, uint64_t value );
ISL_API_ATTR DLL_CORE_API char const* le_get_argument_name_from_hash( uint64_t value );

// ---------- utilities

#define LE_MODULE_REGISTER_IMPL( x, api ) \
	ISL_API_ATTR void le_module_register_##x( void* api )

#define LE_MODULE( x )                                                   \
	ISL_API_ATTR DLL_EXPORT_PREFIX void le_module_register_##x( void* ); \
	static char const*                  le_module_name_##x = #x

#define LE_MODULE_LOAD_DYNAMIC( x )                                                  \
	static x##_api const* x##_api_i = ( x##_api const* )le_core_load_module_dynamic( \
	    le_module_name_##x,                                                          \
	    sizeof( x##_api ),                                                           \
	    true )

#define LE_MODULE_LOAD_STATIC( x )                                                  \
	static x##_api const* x##_api_i = ( x##_api const* )le_core_load_module_static( \
	    le_module_name_##x,                                                         \
	    le_module_register_##x,                                                     \
	    sizeof( x##_api ) )

#ifdef PLUGINS_DYNAMIC
#	define LE_MODULE_LOAD_DEFAULT( x ) \
		LE_MODULE_LOAD_DYNAMIC( x )
#else
#	define LE_MODULE_LOAD_DEFAULT( x ) \
		LE_MODULE_LOAD_STATIC( x )
#endif

// ----------------------------------------------------------------------
// Preprocessor Macro utilities
//
#define LE_OPAQUE_HANDLE( object ) typedef struct object##_t* object;

// Wrap an enum of `enum_name` in a struct with `struct_name` so
// that it can be opaquely passed around, then unwrapped.
#define LE_WRAP_ENUM_IN_STRUCT( enum_name, struct_name ) \
	struct struct_name {                                 \
		enum_name data;                                  \
		          operator const enum_name&() const {    \
			          return data;                       \
		}                                                \
		operator enum_name&() {                          \
			return data;                                 \
		}                                                \
	};                                                   \
	static_assert( sizeof( enum_name ) == sizeof( struct_name ) && "struct and wrapper must have the same size" )

// Callback forwarding is a technique for hiding target address changes
// from libraries which trigger callbacks.
// This is dependent on low-level implementation details, and therefore
// has a tendency to be fickle, and is not very nice to debug.
// For this reason, we're only enabling it on request.
//
// Read more about callback forwarding here:
// <https://poniesandlight.co.uk/reflect/callbacks_and_hot_reloading/>
//
#if !defined( NDEBUG ) && defined( __x86_64__ )
/// return: immovable function pointer which can be used as callback, even with hot-reloading.
///         calls via this pointer will be forwarded to the current address of the callback
///         funtion, without the caller noticing anything about it.
/// params: p_function_pointer: address of pointer to function which you wish to execute.
///         This secondary level of indirection is necessary so that we can pass api entries
///         as callback entries. Api entries get automatically updated when an api is reloaded.
ISL_API_ATTR void* le_core_get_callback_forwarder_addr_impl( void* p_function_pointer );
// release a function pointer forwarder
ISL_API_ATTR void le_core_release_callback_forwarder_addr_impl( void* p_function_pointer );

/// USE THIS METHOD FOR CALLBACK FORWARDING
#	define le_core_forward_callback( x ) \
		le_core_get_callback_forwarder_addr_impl( ( void* )&x )
#	define le_core_forward_callback_release( x ) \
		le_core_release_callback_forwarder_addr_impl( ( void* )x )
#else

#	define le_core_forward_callback( x ) \
		( x )
#	define le_core_forward_callback_release( x ) \
		( x )
#endif

// ----------- c++ specific utilities
#ifdef __cplusplus

struct NoCopy {

	NoCopy() = default;

	// copy assignment operator
	NoCopy& operator=( const NoCopy& rhs ) = delete;

	// copy constructor
	NoCopy( const NoCopy& rhs ) = delete;

  protected:
	~NoCopy() = default;
};

struct NoMove {

	NoMove() = default;

	// move assignment operator
	NoMove& operator=( NoMove&& rhs ) = delete;

	// move constructor
	NoMove( const NoMove&& rhs ) = delete;

  protected:
	~NoMove() = default;
};

#endif // __cplusplus

#endif // GUARD_API_REGISTRY_HPP
