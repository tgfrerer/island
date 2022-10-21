#ifndef GUARD_le_log_H
#define GUARD_le_log_H

#include "le_core.h"

#define LE_LOG_LEVEL_DEBUG ( 1 << 0 )
#define LE_LOG_LEVEL_INFO ( 1 << 1 )
#define LE_LOG_LEVEL_WARN ( 1 << 2 )
#define LE_LOG_LEVEL_ERROR ( 1 << 4 )

#ifdef NDEBUG
//
// If we're compiling for Release, the minimum log level is automatically set
// so that anything below `LE_LOG_LEVEL` is a no-op. The default value for
// `LE_LOG_LEVEL` is 2, which means only Warnings and Errors will be
// processed and potentially displayed with applications compiled using the
// Release target.
//
// If you want to explicitly override `LE_LOG_LEVEL`, do can this in you app's
// CMakeLists.txt file, by adding the directive:
//
// `add_compile_definitions( LE_LOG_LEVEL=0 )`
//
// The above sets `LE_LOG_LEVEL` explicitly to `0` and therefore forces logging
// for all messages even in Release mode. Override `LE_LOG_LEVEL` to `5` to
// explicitly and globally disable all logging.

#	ifndef LE_LOG_LEVEL
#		define LE_LOG_LEVEL LE_LOG_LEVEL_WARN
#	endif

#endif

struct le_log_channel_o;
struct le_log_context_o;

// clang-format off
struct le_log_api {

	enum class Level : uint32_t {
		eDebug = LE_LOG_LEVEL_DEBUG,
		eInfo  = LE_LOG_LEVEL_INFO,
		eWarn  = LE_LOG_LEVEL_WARN,
		eError = LE_LOG_LEVEL_ERROR,
	};


    // callback signature for a log printout event subscriber.
    // You must make a local copy of the chars array  
    typedef void (*fn_subscriber_push_chars)(char* chars, uint32_t num_chars, void * user_data); // user_data will be set to owner on callback


    // Callback to trigger when printing log.
    // debug_level_mask is logical or of LE_LOG_LEVEL_[DEBUG|INFO|WARN|ERROR] that this callback subscribes to.
    //
    // Returns a unique 64 bit handle which you can use to subsequently remove a subscriber
    uint64_t (*add_subscriber)(fn_subscriber_push_chars subscriber, void* user_data, uint32_t debug_level_mask);
    // 
    void (*remove_subscriber)(uint64_t handle);

    le_log_channel_o *( * get_channel )(const char *name);

    struct le_log_channel_interface_t {

        // Set the log level for a given channel - Messages below the given level will be ignored. 
        void ( *set_level  )(le_log_channel_o *channel, Level level);

        void ( *debug )(const le_log_channel_o *channel, const char *msg, ...);
        void ( *info  )(const le_log_channel_o *channel, const char *msg, ...);
        void ( *warn  )(const le_log_channel_o *channel, const char *msg, ...);
        void ( *error )(const le_log_channel_o *channel, const char *msg, ...);

    };

    le_log_channel_interface_t   le_log_channel_i;
};
// clang-format on

LE_MODULE( le_log );
LE_MODULE_LOAD_DEFAULT( le_log );

#ifdef __cplusplus

namespace le_log {

static const auto& api              = le_log_api_i;
static const auto& le_log_channel_i = api->le_log_channel_i;
} // namespace le_log

class LeLog {
	le_log_channel_o* channel;

  public:
	using Level = le_log_api::Level;
	LeLog()
	    : channel( le_log::api->get_channel( nullptr ) ) {
	}

	LeLog( le_log_channel_o* channel_ )
	    : channel( channel_ ) {
	}

	LeLog( char const* channel_name )
	    : channel( le_log::api->get_channel( channel_name ) ) {
	}

	inline void set_level( const Level& level ) {
		le_log::le_log_channel_i.set_level( channel, level );
	}

	template <class... Args>
	inline void debug( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_DEBUG
		le_log::le_log_channel_i.debug( channel, msg, static_cast<Args&&>( args )... );
#	endif
	}

	template <class... Args>
	inline void info( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_INFO
		le_log::le_log_channel_i.info( channel, msg, static_cast<Args&&>( args )... );
#	endif
	}

	template <class... Args>
	inline void warn( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_WARN
		le_log::le_log_channel_i.warn( channel, msg, static_cast<Args&&>( args )... );
#	endif
	}

	template <class... Args>
	inline void error( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_ERROR
		le_log::le_log_channel_i.error( channel, msg, static_cast<Args&&>( args )... );
#	endif
	}

	le_log_channel_o* getChannel() {
		return channel;
	}
};

namespace le {
using Log = LeLog;
}

// ----------------------------------------------------------------------

static inline void le_log_set_level( const LeLog::Level& level ) {
	le_log::le_log_channel_i.set_level( nullptr, level );
}

template <typename... Args>
static inline void le_log_debug( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_DEBUG
	le_log::le_log_channel_i.debug( nullptr, msg, static_cast<Args&&>( args )... );
#	endif
}

template <typename... Args>
static inline void le_log_info( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_INFO
	le_log::le_log_channel_i.info( nullptr, msg, static_cast<Args&&>( args )... );
#	endif
}

template <typename... Args>
static inline void le_log_warn( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_WARN
	le_log::le_log_channel_i.warn( nullptr, msg, static_cast<Args&&>( args )... );
#	endif
}

template <typename... Args>
static inline void le_log_error( const char* msg, Args&&... args ) {
#	if ( !defined NDEBUG ) || LE_LOG_LEVEL <= LE_LOG_LEVEL_ERROR
	le_log::le_log_channel_i.error( nullptr, msg, static_cast<Args&&>( args )... );
#	endif
}

#endif // __cplusplus

#endif
