#ifndef GUARD_le_log_H
#define GUARD_le_log_H

#include "le_core/le_core.h"

struct le_log_channel_o;
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

    le_log_channel_o *( * get_channel )(const char *name);

    struct le_log_channel_interface_t {

        void ( *set_level  )(le_log_channel_o *module, Level level);

        void ( *debug      )(const le_log_channel_o *module, const char *msg, ...);
        void ( *info       )(const le_log_channel_o *module, const char *msg, ...);
        void ( *warn       )(const le_log_channel_o *module, const char *msg, ...);
        void ( *error      )(const le_log_channel_o *module, const char *msg, ...);

    };

    le_log_channel_interface_t   le_log_channel_i;
};
// clang-format on

LE_MODULE( le_log );
LE_MODULE_LOAD_DEFAULT( le_log );

#ifdef __cplusplus

namespace le {

static const auto &api              = le_log_api_i;
static const auto &le_log_channel_i = api -> le_log_channel_i;

class Log {
	le_log_channel_o *channel;

  public:
	using Level = le_log_api::Level;
	Log()
	    : channel( api->get_channel( nullptr ) ) {
	}

	Log( le_log_channel_o *channel_ )
	    : channel( channel_ ) {
	}

	Log( char const *channel_name )
	    : channel( api->get_channel( nullptr ) ) {
	}

	inline void set_level( const Level &level ) {
		le_log_channel_i.set_level( channel, level );
	}

	template <class... Args>
	inline void debug( const char *msg, Args &&...args ) {
		le_log_channel_i.debug( channel, msg, static_cast<Args &&>( args )... );
	}

	template <class... Args>
	inline void info( const char *msg, Args &&...args ) {
		le_log_channel_i.info( channel, msg, static_cast<Args &&>( args )... );
	}

	template <class... Args>
	inline void warn( const char *msg, Args &&...args ) {
		le_log_channel_i.warn( channel, msg, static_cast<Args &&>( args )... );
	}

	template <class... Args>
	inline void error( const char *msg, Args &&...args ) {
		le_log_channel_i.error( channel, msg, static_cast<Args &&>( args )... );
	}
};
// ----------------------------------------------------------------------

static inline void log_set_level( const Log::Level &level ) {
	le_log_channel_i.set_level( nullptr, level );
}

template <typename... Args>
static inline void log_debug( const char *msg, Args &&...args ) {
	le_log_channel_i.debug( nullptr, msg, static_cast<Args &&>( args )... );
}

template <typename... Args>
static inline void log_info( const char *msg, Args &&...args ) {
	le_log_channel_i.info( nullptr, msg, static_cast<Args &&>( args )... );
}

template <typename... Args>
static inline void log_warn( const char *msg, Args &&...args ) {
	le_log_channel_i.warn( nullptr, msg, static_cast<Args &&>( args )... );
}

template <typename... Args>
static inline void log_error( const char *msg, Args &&...args ) {
	le_log_channel_i.error( nullptr, msg, static_cast<Args &&>( args )... );
}

} // namespace le

#endif // __cplusplus

#endif
