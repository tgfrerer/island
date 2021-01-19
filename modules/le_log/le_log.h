#ifndef GUARD_le_log_H
#define GUARD_le_log_H

#include "le_core/le_core.h"

struct le_log_module_o;
struct le_log_context_o;

// clang-format off
struct le_log_api {

	enum class Level : uint8_t {
		DEBUG = 0,
		INFO  = 1,
		WARN  = 2,
		ERROR = 3
	};

    le_log_context_o* context = nullptr;

    struct le_log_module_interface_t {
        le_log_module_o *( *get_module )(const char *name);

        void ( *set_level  )(le_log_module_o *module, Level level);

        void ( *debug      )(const le_log_module_o *module, const char *msg, ...);
        void ( *info       )(const le_log_module_o *module, const char *msg, ...);
        void ( *warn       )(const le_log_module_o *module, const char *msg, ...);
        void ( *error      )(const le_log_module_o *module, const char *msg, ...);

    };

    le_log_module_interface_t   le_log_module_i;
};
// clang-format on

LE_MODULE( le_log );
LE_MODULE_LOAD_DEFAULT( le_log );

#ifdef __cplusplus

namespace le_log {

using Level = le_log_api::Level;

static const auto &api      = le_log_api_i;
static const auto &le_log_i = api -> le_log_module_i;

static void set_level( const Level &level ) {
	api->le_log_module_i.set_level( nullptr, level );
}

template <class... Args>
inline void debug( const char *msg, Args &&...args ) {
	api->le_log_module_i.debug( nullptr, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void info( const char *msg, Args &&...args ) {
	api->le_log_module_i.info( nullptr, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void warn( const char *msg, Args &&...args ) {
	api->le_log_module_i.warn( nullptr, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void error( const char *msg, Args &&...args ) {
	api->le_log_module_i.error( nullptr, msg, static_cast<Args &&>( args )... );
}

// --------------------------------------------------

le_log_module_o *get_module( const char *name ) {
	return api->le_log_module_i.get_module( name );
}

static void set_level( le_log_module_o *module, const Level &level ) {
	api->le_log_module_i.set_level( module, level );
}

template <class... Args>
inline void debug( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->le_log_module_i.debug( module, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void info( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->le_log_module_i.info( module, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void warn( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->le_log_module_i.warn( module, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void error( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->le_log_module_i.error( module, msg, static_cast<Args &&>( args )... );
}

} // namespace le_log

#endif // __cplusplus

#endif
