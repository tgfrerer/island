#ifndef GUARD_le_tracy_H
#define GUARD_le_tracy_H

#if defined( PLUGINS_DYNAMIC ) and defined( TRACY_ENABLE )
#	define LE_LOAD_TRACING_LIBRARY \
		le_core_load_library_persistently( "libTracyClient.so" )
#else
#	define LE_LOAD_TRACING_LIBRARY
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

#endif
