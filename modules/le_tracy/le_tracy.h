#ifndef GUARD_le_tracy_H
#define GUARD_le_tracy_H

#if defined( PLUGINS_DYNAMIC ) and defined( TRACY_ENABLE )
#	define LE_LOAD_TRACING_LIBRARY \
		le_core_load_library_persistently( "libTracyClient.so" )
#else
#	define LE_LOAD_TRACING_LIBRARY
#endif

#ifdef __cplusplus

#endif // __cplusplus

#endif
