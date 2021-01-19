#ifndef GUARD_le_log_H
#define GUARD_le_log_H

#include "le_core/le_core.h"

namespace le_log {
enum class Level : uint8_t {
	DEBUG = 0,
	INFO  = 1,
	WARN  = 2,
	ERROR = 3
};
}

struct le_log_module_o;
struct le_log_context_o;

// clang-format off
struct le_log_api {

    void ( *set_level  )(le_log_module_o *module, le_log::Level level);

    void ( *debug      )(const le_log_module_o *module, const char *msg, ...);

    void ( *info       )(const le_log_module_o *module, const char *msg, ...);

    void ( *warn       )(const le_log_module_o *module, const char *msg, ...);

    void ( *error      )(const le_log_module_o *module, const char *msg, ...);

    le_log_module_o *( *get_module )(const char *name);

    le_log_context_o* context = nullptr;
};
// clang-format on

LE_MODULE( le_log );
LE_MODULE_LOAD_DEFAULT( le_log );

#ifdef __cplusplus

namespace le_log {
static const auto &api = le_log_api_i;

static void set_level( const Level &level ) {
	api->set_level( nullptr, level );
}

template <class... Args>
inline void debug( const char *msg, Args &&...args ) {
	api->debug( nullptr, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void info( const char *msg, Args &&...args ) {
	api->info( nullptr, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void warn( const char *msg, Args &&...args ) {
	api->warn( nullptr, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void error( const char *msg, Args &&...args ) {
	api->error( nullptr, msg, static_cast<Args &&>( args )... );
}

// --------------------------------------------------

le_log_module_o *get_module( const char *name ) {
	return api->get_module( name );
}

static void set_level( le_log_module_o *module, const Level &level ) {
	api->set_level( module, level );
}

template <class... Args>
inline void debug( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->debug( module, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void info( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->info( module, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void warn( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->warn( module, msg, static_cast<Args &&>( args )... );
}

template <class... Args>
inline void error( const le_log_module_o *module, const char *msg, Args &&...args ) {
	api->error( module, msg, static_cast<Args &&>( args )... );
}

} // namespace le_log

#endif // __cplusplus

#endif
