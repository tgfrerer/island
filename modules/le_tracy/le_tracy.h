#ifndef GUARD_le_tracy_H
#define GUARD_le_tracy_H

#include "le_core.h"

/* Low-Overhead Profiling via Tracy
 *
 * Profiling is only enabled (and Tracy only compiled) if you add the
 * following compiler definition into your topmost CMakeLists.txt file:
 *
 * 	add_compile_definitions( TRACY_ENABLE )
 *
 * To enable passing log messages to tracy, add LE_TRACY_ENABLE_LOG(-1)
 * to where you initialize your main app.
 *
 * Every module which uses tracy must load the tracy library, you
 * must do this via an explicit library load, by adding
 *
 * 	#ifdef LE_LOAD_TRACING_LIBRARY
 *		LE_LOAD_TRACING_LIBRARY
 * 	#endif
 *
 * To where you initialize this module's api pointers in its cpp file.
 *
 */

#if defined( PLUGINS_DYNAMIC ) and defined( TRACY_ENABLE )
#	define LE_LOAD_TRACING_LIBRARY \
		le_core_load_library_persistently( "libTracyClient.so" )
#else
#	define LE_LOAD_TRACING_LIBRARY
#endif

#if defined( TRACY_ENABLE )
#	define LE_TRACY_ENABLE_LOG( l ) \
		le_tracy::api->le_tracy_i.enable_log( l )
#else
#	define LE_TRACY_ENABLE_LOG( l ) \
		( l )
#endif

// We break our rule here which says that header files are not allowed
// to include other header files.

// this is because this header file will only be included from cpp files
// anyway and we want to have an unique point at which we control what
// gets included for tracy and which defines etc are set.

//----------------------------------------------------------------------
// here, we essentially mirror the .hpp file that is provided with Tracy
// but we make some changes to make tracing slightly more generic,

#include "3rdparty/tracy/public/tracy/Tracy.hpp"

//----------------------------------------------------------------------

// we allow ourselves a tracy context object so that we can store
// any auxiliary information associated with tracing in one place.
// this includes a subscriber to the logger, if we so wish

struct le_tracy_o;

// clang-format off
struct le_tracy_api {
	
	struct le_tracy_interface_t {
		void            ( * enable_log                ) ( uint32_t log_messages_mask);
		
	};
	
	le_tracy_interface_t       le_tracy_i;
};
// clang-format on

LE_MODULE( le_tracy );
LE_MODULE_LOAD_DEFAULT( le_tracy );

#ifdef __cplusplus

namespace le_tracy {
static const auto& api        = le_tracy_api_i;
static const auto& le_tracy_i = api->le_tracy_i;
} // namespace le_tracy

#	if ( WIN32 ) and defined( PLUGINS_DYNAMIC ) and defined( TRACY_ENABLE )
#		pragma comment( lib, "modules/TracyClient.lib" )
#	endif

#endif // __cplusplus

#endif
