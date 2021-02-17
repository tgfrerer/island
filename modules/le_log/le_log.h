#ifndef GUARD_le_log_H
#define GUARD_le_log_H

#include "le_core/le_core.h"

struct le_log_channel_o;
struct le_log_context_o;

// clang-format off
struct le_log_api {

	enum class Level : int {
		DEBUG = 0,
		INFO  = 1,
		WARN  = 2,
		ERROR = 3
	};

    le_log_context_o* context = nullptr;

    le_log_channel_o *( * get_channel )(const char *name);

    struct le_log_channel_interface_t {

        // Set the log level for a given channel - Messages below the given level will be ignored. 
        void ( *set_level  )(le_log_channel_o *channel, Level level);

        void ( *debug      )(const le_log_channel_o *channel, const char *msg, ...);
        void ( *info       )(const le_log_channel_o *channel, const char *msg, ...);
        void ( *warn       )(const le_log_channel_o *channel, const char *msg, ...);
        void ( *error      )(const le_log_channel_o *channel, const char *msg, ...);

    };

    le_log_channel_interface_t   le_log_channel_i;
};
// clang-format on

LE_MODULE( le_log );
LE_MODULE_LOAD_DEFAULT( le_log );

#ifdef __cplusplus

namespace le_log {

static const auto &api              = le_log_api_i;
static const auto &le_log_channel_i = api -> le_log_channel_i;
} // namespace le_log

class LeLog {
	le_log_channel_o *channel;

  public:
	using Level = le_log_api::Level;
	LeLog()
	    : channel( le_log::api->get_channel( nullptr ) ) {
	}

	LeLog( le_log_channel_o *channel_ )
	    : channel( channel_ ) {
	}

	LeLog( char const *channel_name )
	    : channel( le_log::api->get_channel( channel_name ) ) {
	}

	inline void set_level( const Level &level ) {
		le_log::le_log_channel_i.set_level( channel, level );
	}

	template <class... Args>
	inline void debug( const char *msg, Args &&...args ) {
		le_log::le_log_channel_i.debug( channel, msg, static_cast<Args &&>( args )... );
	}

	template <class... Args>
	inline void info( const char *msg, Args &&...args ) {
		le_log::le_log_channel_i.info( channel, msg, static_cast<Args &&>( args )... );
	}

	template <class... Args>
	inline void warn( const char *msg, Args &&...args ) {
		le_log::le_log_channel_i.warn( channel, msg, static_cast<Args &&>( args )... );
	}

	template <class... Args>
	inline void error( const char *msg, Args &&...args ) {
		le_log::le_log_channel_i.error( channel, msg, static_cast<Args &&>( args )... );
	}
};
// ----------------------------------------------------------------------

static inline void le_log_set_level( const LeLog::Level &level ) {
	le_log::le_log_channel_i.set_level( nullptr, level );
}

template <typename... Args>
static inline void le_log_debug( const char *msg, Args &&...args ) {
	le_log::le_log_channel_i.debug( nullptr, msg, static_cast<Args &&>( args )... );
}

template <typename... Args>
static inline void le_log_info( const char *msg, Args &&...args ) {
	le_log::le_log_channel_i.info( nullptr, msg, static_cast<Args &&>( args )... );
}

template <typename... Args>
static inline void le_log_warn( const char *msg, Args &&...args ) {
	le_log::le_log_channel_i.warn( nullptr, msg, static_cast<Args &&>( args )... );
}

template <typename... Args>
static inline void le_log_error( const char *msg, Args &&...args ) {
	le_log::le_log_channel_i.error( nullptr, msg, static_cast<Args &&>( args )... );
}

#endif // __cplusplus

#endif
