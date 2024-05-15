#include "le_log.h"
#include "le_core.h"
#include "le_hash_util.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <string>

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
};

static le_log_context_o* ctx;

static le_log_channel_o* le_log_channel_default() {
	return &ctx->channel_default;
}

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

static void le_log_set_level( le_log_channel_o* channel, LeLog::Level level ) {
	if ( !channel ) {
		channel = le_log_channel_default();
	}
	channel->log_level = static_cast<std::underlying_type<LeLog::Level>::type>( level );
}

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

// this method needs to be thread-safe!
// its' very likely that multiple threads want to write to this at the same time.
static void le_log_printf( const le_log_channel_o* channel, LeLog::Level level, const char* msg, va_list args ) {

	if ( !channel ) {
		channel = le_log_channel_default();
	}

	if ( int( level ) < channel->log_level ) {
		return;
	}

	// thread-safe region follows

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

// ----------------------------------------------------------------------

template <LeLog::Level level>
static void le_log_implementation( const le_log_channel_o* channel, const char* msg, ... ) {
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

	{
		// Do not repeat message if it just has been printed.

		static uint64_t last_hash = 0;

		// This is optimised for the most common case:
		// Messages are not repeating.

		// First, we check whether the number of chars is equivalent to
		// the number of chars that we hashed the last time.

		static uint32_t last_num_chars = 0;

		if ( last_num_chars == num_chars ) {

			// Only if the number of chars matches, do calculate the hash
			// for the current message and compare it to the hash of the last
			// message.

			uint64_t current_hash = hash_64_fnv1a( chars );
			if ( current_hash == last_hash ) {
				return;
			}
			last_hash = current_hash;
		} else {
			last_hash = 0;
		}

		last_num_chars = num_chars;
	}

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
	api_add_subscriber( default_subscriber_cout, &ctx, LE_LOG_LEVEL_DEBUG | LE_LOG_LEVEL_INFO | LE_LOG_LEVEL_WARN );
	api_add_subscriber( default_subscriber_cerr, &ctx, LE_LOG_LEVEL_ERROR );
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
	le_api_channel_i.set_level = le_log_set_level;

	auto fallback_context_addr = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_log_context_fallback" ) );

	if ( *fallback_context_addr == nullptr ) {
		*fallback_context_addr = new le_log_context_o();
	}

	ctx = static_cast<le_log_context_o*>( *fallback_context_addr );

	if ( ctx->subscribers.empty() ) {
		setup_basic_cout_subscriber();
	}
}
