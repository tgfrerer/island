#define LE_MODULE_UNREGISTER_EXPLICIT
#include "le_core.h"
#include "le_hash_util.h"
#include "le_log.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <string>
#include <string.h> // for memcpy

#ifdef _MSC_VER
#	include <intrin.h> // for debugbreak()
#else
#	include <csignal> // for std::raise
#endif

struct le_log_channel_o {
	std::string name = "DEFAULT";
#if defined( LE_LOG_LEVEL )
	std::atomic_int log_level = LE_LOG_LEVEL;
#else
	std::atomic_int log_level = LE_LOG_LEVEL_INFO;
#endif
	uint64_t line_hashes[ le_log_api::MAX_NUM_LINES_TO_FILTER ];

	size_t hashes_capacity     = le_log_api::DEFAULT_NUM_LINES_TO_FILTER; // number of line hashes that we use for filter context
	size_t hashes_begin        = 0;
	size_t hashes_end          = 0;
	size_t hash_count_elements = 0;

	std::mutex channel_filter_mutex; // mutex protecting num_lines_to_filter and line_hashes
};

struct subscriber_entry {
	uint64_t                             unique_id           = 0;
	le_log_api::fn_subscriber_push_chars push_chars          = nullptr;
	void*                                user_data           = nullptr;
	uint32_t                             log_level_flag_mask = 0; // mask for which log levels to accept data for this subscriber
};

struct le_log_context_o {
	le_log_channel_o                                   channel_default;
	std::unordered_map<std::string, le_log_channel_o*> channels;
	std::mutex                                         channels_mtx;
	std::vector<subscriber_entry>                      subscribers;
	std::mutex                                         subscribers_mtx;
	uint64_t                                           subscriber_id_next = 1; // ever-increasing number, Note that we start handing out subscriber ids at 1, so that 0 can stand for no subscriber

	uint64_t default_cout_handle = 0;
	uint64_t default_cerr_handle = 0;
};

static le_log_context_o* ctx = nullptr;

// ----------------------------------------------------------------------

static le_log_channel_o* le_log_channel_default() {
	return &ctx->channel_default;
}

// ----------------------------------------------------------------------

static le_log_channel_o* le_log_get_module( const char* name ) {
	if ( !name || !name[ 0 ] ) {
		return le_log_channel_default();
	}

	std::scoped_lock g( ctx->channels_mtx );
	if ( ctx->channels.find( name ) == ctx->channels.end() ) {
		auto module           = new le_log_channel_o();
		module->name          = name;
		ctx->channels[ name ] = module;
		return module;
	}
	return ctx->channels[ name ];
}

// ----------------------------------------------------------------------

static void le_log_set_level( le_log_channel_o* channel, LeLog::Level level ) {
	if ( !channel ) {
		channel = le_log_channel_default();
	}
	channel->log_level = static_cast<std::underlying_type<LeLog::Level>::type>( level );
}

// ----------------------------------------------------------------------

static const char* le_log_level_name( LeLog::Level level ) {
	switch ( level ) {
	case LeLog::Level::eDebug:
		return "DEBUG";
	case LeLog::Level::eInfo:
		return "INFO";
	case LeLog::Level::eWarn:
		return "\x1b[38;5;220mWARN\x1b[0m   ";
	case LeLog::Level::eError:
		return "\x1b[38;5;209mERROR\x1b[0m  ";
	}
	return "";
}

static void le_log_set_filter_num_lines( le_log_channel_o* channel, size_t num_lines ) {
	auto lock                    = std::scoped_lock( channel->channel_filter_mutex );

	size_t previous_capacity = channel->hashes_capacity;
	channel->hashes_capacity = std::min( le_log_api::MAX_NUM_LINES_TO_FILTER, num_lines );
	if ( previous_capacity != channel->hashes_capacity ) {
		// reset ring buffer iterators if capacity has changed.
		channel->hashes_begin        = 0;
		channel->hashes_end          = 0;
		channel->hash_count_elements = 0;
	}
}

// ----------------------------------------------------------------------
// Filter log messages so that messages which are repeated within a
// per-channel window are discarded.
static bool filter_current_message( le_log_channel_o* channel, std::string const& buffer ) {

	if constexpr ( le_log_api::MAX_NUM_LINES_TO_FILTER ) {
		if ( channel->hashes_capacity > 0 ) {

			auto lock = std::scoped_lock( channel->channel_filter_mutex );

			// Num_lines_to_filter may have changed between the
			// last check and acquisition of the mutex.
			if ( channel->hashes_capacity == 0 ) {
				return true;
			}

			auto& hashes = channel->line_hashes;

			// This is optimised for the most common case:
			// Messages are not repeating.
			uint64_t const h = hash_64_fnv1a( buffer.c_str() );

			for ( size_t i = 0; i != channel->hash_count_elements; i++ ) {
				if ( hashes[ ( channel->hashes_begin + i ) % channel->hashes_capacity ] == h ) {
					// We found an identical hash, this means that we have printed
					// this line within the last n lines.
					return false;
				}
			}

			// If the element has not been seen before, we add it
			// to our list of lines that we have seen;

			if ( channel->hash_count_elements < channel->hashes_capacity ) {
				// buffer can still grow
				hashes[ channel->hashes_end ] = h;
				channel->hashes_end           = ( channel->hashes_end + 1 ) % channel->hashes_capacity;
				channel->hash_count_elements++;
			} else {
				// buffer is at capacity - we must shift begin and end by one element
				hashes[ channel->hashes_end ] = h;
				channel->hashes_end           = ( channel->hashes_end + 1 ) % channel->hashes_capacity;
				channel->hashes_begin         = ( channel->hashes_end + 1 ) % channel->hashes_capacity;
			}
		}
	}

	return true;
}

// ----------------------------------------------------------------------

// this method needs to be thread-safe!
// its' very likely that multiple threads want to write to this at the same time.
static void le_log_printf( le_log_channel_o* channel, LeLog::Level level, const char* msg, va_list args ) {

	if ( !channel ) {
		channel = le_log_channel_default();
	}

	if ( int( level ) < channel->log_level ) {
		return;
	}

	// Thread-safe region follows
	{
		static std::mutex print_mtx;
		auto              lock = std::scoped_lock( print_mtx ); // lock protecting this whole function

		static size_t      num_bytes_buffer_1 = 16;
		static size_t      num_bytes_buffer_2 = 0;
		static std::string buffer( num_bytes_buffer_1, '\0' );

		do {
			buffer.resize( num_bytes_buffer_1 + 1 );
			num_bytes_buffer_1 = snprintf( buffer.data(), buffer.size(), "[ %-25s | %-7s ] ", channel->name.c_str(), le_log_level_name( level ) );
			num_bytes_buffer_1++;
		} while ( num_bytes_buffer_1 > buffer.size() );

		num_bytes_buffer_1--; // remove last \0 byte

		// We must store state of va_args as this may get changed as a side-effect of a call to vsnprintf()
		{
			va_list old_args;
			va_copy( old_args, args );

			do {
				va_list args;
				va_copy( args, old_args );
				buffer.resize( num_bytes_buffer_1 + num_bytes_buffer_2 );
				num_bytes_buffer_2 = vsnprintf( buffer.data() + num_bytes_buffer_1, buffer.size() - num_bytes_buffer_1, msg, args );
				num_bytes_buffer_2++; // make space for final \0 byte
				va_end( args );
			} while ( num_bytes_buffer_1 + num_bytes_buffer_2 > buffer.size() );

			va_end( old_args );
		}
		num_bytes_buffer_2--; // remove last \0 byte

		if ( filter_current_message( channel, buffer ) ) {

			auto subscribers_lock = std::scoped_lock( ctx->subscribers_mtx );
			for ( auto& s : ctx->subscribers ) {
				// call back subscribers iff they have matching log level flags set in their mask
				// careful - if there is a call within the callback to the log itself
				// then we may end up with a deadlock.
				// FIXME: make sure that we don't end up with a deadlock.
				if ( uint32_t( level ) & s.log_level_flag_mask ) {
					s.push_chars( buffer.data(), num_bytes_buffer_1 + num_bytes_buffer_2, s.user_data );
				}
			} // end thread-safe region
		}
	}
}

// ----------------------------------------------------------------------

template <LeLog::Level level>
static void le_log_implementation( le_log_channel_o* channel, const char* msg, ... ) {
	va_list arglist;
	va_start( arglist, msg );
	le_log_printf( channel, level, msg, arglist );
	va_end( arglist );
// Additionally, trigger a breakpoint if we have encountered an error, in case we're running in debug mode.
#ifndef NDEBUG
	// Raise a breakpoint on ERROR in case we're running in debug mode.
	if ( level == LeLog::Level::eError ) {
#	ifdef _MSC_VER
		__debugbreak(); // see: https://docs.microsoft.com/en-us/cpp/intrinsics/debugbreak?view=msvc-170
#	else
		std::raise( SIGINT );
#	endif
	}
#endif
}

// ----------------------------------------------------------------------

static uint64_t api_add_subscriber( le_log_api::fn_subscriber_push_chars pfun_receive_chars, void* user_data, uint32_t mask ) {
	auto     subscribers_lock = std::scoped_lock( ctx->subscribers_mtx );
	uint64_t unique_id        = ctx->subscriber_id_next++;
	ctx->subscribers.push_back( { .unique_id = unique_id, .push_chars = pfun_receive_chars, .user_data = user_data, .log_level_flag_mask = mask } );
	return unique_id;
};

// ----------------------------------------------------------------------

static void api_remove_subscriber( uint64_t handle ) {
	auto subscribers_lock = std::scoped_lock( ctx->subscribers_mtx );

	size_t num_subscribers = ctx->subscribers.size();
	for ( size_t i = 0; i != num_subscribers; ) {
		if ( ctx->subscribers[ i ].unique_id == handle ) {
			ctx->subscribers.erase( ctx->subscribers.begin() + i );
			num_subscribers--;
			return; // we can return early here as there is only ever one subscriber
		}
		i++;
	}
}

// ----------------------------------------------------------------------

static void default_subscriber_cout( char const* chars, uint32_t num_chars, void* ) {

	fprintf( stdout, "%*s\n", num_chars, chars );
	fflush( stdout );
};

// ----------------------------------------------------------------------

static void default_subscriber_cerr( char const* chars, uint32_t num_chars, void* ) {
	fprintf( stderr, "%*s\n", num_chars, chars );
	fflush( stderr );
};

// ----------------------------------------------------------------------
// This is where we hook up the default subscribers to our logger. The
// default subscribers print to stdout and stderr.
static void setup_basic_cout_subscriber() {
	ctx->default_cout_handle = api_add_subscriber( default_subscriber_cout, &ctx, LE_LOG_LEVEL_DEBUG | LE_LOG_LEVEL_INFO | LE_LOG_LEVEL_WARN );
	ctx->default_cerr_handle = api_add_subscriber( default_subscriber_cerr, &ctx, LE_LOG_LEVEL_ERROR );
}

// ----------------------------------------------------------------------

static void reset_basic_cout_subscriber() {
	// remove default cout subscriber - as the function might have changed position on reload of this module
	if ( ctx->default_cout_handle ) {
		api_remove_subscriber( ctx->default_cout_handle );
		ctx->default_cout_handle = 0;
	}

	// remove default cerr subscriber - as the function might have changed position on reload of this module
	if ( ctx->default_cerr_handle ) {
		api_remove_subscriber( ctx->default_cerr_handle );
		ctx->default_cerr_handle = 0;
	}
}

// ----------------------------------------------------------------------
// This only gets called on final teardown
LE_MODULE_UNREGISTER_IMPL( le_log, api ) {
	reset_basic_cout_subscriber();
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_log, api ) {
	auto le_api = static_cast<le_log_api*>( api );

	le_api->get_channel       = le_log_get_module;
	le_api->add_subscriber    = api_add_subscriber;
	le_api->remove_subscriber = api_remove_subscriber;

	auto& le_api_channel_i     = le_api->le_log_channel_i;
	le_api_channel_i.debug     = le_log_implementation<LeLog::Level::eDebug>;
	le_api_channel_i.info      = le_log_implementation<LeLog::Level::eInfo>;
	le_api_channel_i.warn      = le_log_implementation<LeLog::Level::eWarn>;
	le_api_channel_i.error     = le_log_implementation<LeLog::Level::eError>;

	le_api_channel_i.set_level            = le_log_set_level;
	le_api_channel_i.set_filter_num_lines = le_log_set_filter_num_lines;

	if ( le_api->own_context == nullptr ) {
		le_api->own_context = new le_log_context_o();
	}

	ctx = le_api->own_context;

	reset_basic_cout_subscriber();

	if ( ctx->default_cout_handle == 0 && ctx->default_cerr_handle == 0 ) {
		setup_basic_cout_subscriber();
	}
}
