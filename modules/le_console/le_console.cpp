#include "le_core.h"
#include "le_hash_util.h"

#include "le_log.h"
#include "le_console.h"
#include "le_settings.h"

#include <mutex>
#include <thread>
#include <deque>

#include <errno.h>
#include <string.h>

#include <memory>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_map>

#include "private/le_console/le_console_types.h"
#include "private/le_console/le_console_server.h"

// Translation between winsock and posix sockets
#ifdef _WIN32
#	define strtok_reentrant strtok_s
#else
#	define strtok_reentrant strtok_r
#endif

extern void le_console_server_register_api( void* api ); // in le_console_server

/*

Ideas for this module:

we want to have more than one console - perhaps one console per connection.

Right now we have a situation where we have a console that is always-on,
and subscribes to log messages as soon as its server starts.

Any connections to the console receive the same answer - there is only one channel for
input and one channel for output. All connections feed their commands into the same
channel for commands.

commands get processed when explicitly requested.

*

A console is something that can subscribe to messages from the log for example,
but most importantly it can read commands from a socket and write responses back to that socket.

*/

static void**                  PP_CONSOLE_SINGLETON  = nullptr; // set via registration method
static le_console_server_api_t le_console_server_api = {};

static le_console_o* produce_console() {
	return static_cast<le_console_o*>( *PP_CONSOLE_SINGLETON );
}

// ----------------------------------------------------------------------
// this method may not call le::log logging, because otherwise there is a
// chance of a deadlock.
static void logger_callback( char const* chars, uint32_t num_chars, void* user_data ) {
	static std::string msg;

	auto connection = ( le_console_o::connection_t* )user_data;
	connection->channel_out.post( std::string( chars, num_chars ) );
}

// ----------------------------------------------------------------------

// If we initialize an object from this and store it static,
// then the destructor will get called when this module is being unloaded
// this allows us to remove ourselves from listeners before the listener
// gets destroyed.
class LeLogSubscriber : NoCopy {
  public:
	explicit LeLogSubscriber( le_console_o::connection_t* connection )
	    : handle( le_log::api->add_subscriber( logger_callback, connection, connection->log_level_mask ) ) {
	}
	~LeLogSubscriber() {
		// we must remove the subscriber because it may get called before we have a chance to change the callback address -
		// even callback forwarding won't help, because the reloader will log- and this log event will happen while the
		// current module is not yet loaded, which means there is no valid code to run for the subscriber.
		le_log::api->remove_subscriber( handle );
	};

  private:
	uint64_t handle;
};

// ----------------------------------------------------------------------
// We use this convoluted construction of a unique_ptr around a Raii class,
// so that we can detect when a module is being unloaded - in which case
// the unique_ptr will call the destructor on LeLogSubscriber.
//
// We must un-register the subscriber in case that this module is reloaded,
// because the loader itself might log, and this logging call may trigger
// a call onto an address which has just been unloaded...
//
// Even callback-forwarding won't help, because the call will happen in the
// liminal stage where this module has been unloaded, and its next version
// has not yet been loaded.
//
static std::unordered_map<uint32_t, std::unique_ptr<LeLogSubscriber>>& le_console_produce_log_subscribers() {
	static std::unordered_map<uint32_t, std::unique_ptr<LeLogSubscriber>> LOG_SUBSCRIBER = {};

	return LOG_SUBSCRIBER;
}

// ----------------------------------------------------------------------

// We want to start the server on-demand.
// if the module gets unloaded, the server thread needs to be first stopped
// if the module gets reloaded, the server thread needs to be resumed
class ServerWatcher : NoCopy {
  public:
	ServerWatcher( le_console_o* console_ )
	    : console( console_ ) {
		if ( console && console->server ) {
			le_console_server_api.start_thread( console->server );
		}
	};
	~ServerWatcher() {
		if ( console && console->server ) {
			// we must stop server
			le_console_server_api.stop_thread( console->server );
		}
	};

  private:
	le_console_o* console = nullptr;
};

// ----------------------------------------------------------------------

static std::unique_ptr<ServerWatcher>& le_console_produce_server_watcher( le_console_o* console = nullptr ) {
	static std::unique_ptr<ServerWatcher> SERVER_WATCHER = {};

	if ( nullptr == SERVER_WATCHER.get() && console != nullptr ) {
		SERVER_WATCHER = std::make_unique<ServerWatcher>( console );
	}
	return SERVER_WATCHER;
}
// ----------------------------------------------------------------------

static bool le_console_server_start() {
	static auto logger = le::Log( LOG_CHANNEL );

	le_console_o* self = produce_console();
	logger.info( "* Starting Server..." );

	self->server = le_console_server_api.create( self ); // destroy server
	le_console_server_api.start( self->server );         // start server
	le_console_produce_server_watcher( self );           // Implicitly starts server thread

	return true;
}

// ----------------------------------------------------------------------

static bool le_console_server_stop() {
	static auto logger = le::Log( LOG_CHANNEL );

	le_console_o* self = produce_console();

	logger.info( "Unregistering Log subscribers" );
	le_console_produce_log_subscribers().clear();

	if ( self->server ) {
		logger.info( "* Stopping server..." );
		le_console_produce_server_watcher().reset( nullptr ); // explicitly call destructor on server watcher - this will join the server thread
		le_console_server_api.stop( self->server );           // start server
		le_console_server_api.destroy( self->server );        // destroy server
		self->server = nullptr;
	}

	return true;
}

// ----------------------------------------------------------------------

static void le_console_process_input() {

	static auto logger = le::Log( LOG_CHANNEL );

	le_console_o* self = produce_console();

	auto connections_lock = std::scoped_lock( self->connections_mutex );

	for ( auto& c : self->connections ) {

		auto& connection = c.second;

		std::string msg;

		connection->channel_in.fetch( msg );

		if ( !msg.empty() ) {
			// todo: we need to update messages

			char const* delim   = "\n\r= ";
			char*       context = nullptr;

			char* token = strtok_reentrant( msg.data(), delim, &context );

			std::vector<std::string> tokens;

			while ( token != nullptr ) {
				// le_log::le_log_channel_i.debug( logger.getChannel(), "Parsed token: [%s]", token );
				tokens.emplace_back( token );
				token = strtok_reentrant( nullptr, delim, &context );
			}

			// process tokens
			if ( !tokens.empty() ) {
				switch ( hash_32_fnv1a( tokens[ 0 ].c_str() ) ) {
				case hash_32_fnv1a_const( "settings" ):
					le_log::le_log_channel_i.info( logger.getChannel(), "Listing Settings" );
					le::Settings().list();
					break;
				case hash_32_fnv1a_const( "json" ): {
					// directly put a message onto the output buffer - without mirroring it to the console
					connection->channel_out.post( R"({ "Token": "This message should pass through unfiltered" })" );
				} break;
				case hash_32_fnv1a_const( "log_level_mask" ):
					// If you set log_level_mask to 0, this means that log messages will be mirrored to console
					// If you set log_level_mask to -1, this means that all log messages will be mirrored to console
					if ( tokens.size() == 2 ) {
						le_console_produce_log_subscribers().erase( c.first ); // Force the subscriber to be de-registered.
						connection->log_level_mask = strtol( tokens[ 1 ].c_str(), nullptr, 0 );
						if ( connection->log_level_mask > 0 ) {
							le_console_produce_log_subscribers()[ c.first ] = std::make_unique<LeLogSubscriber>( connection.get() ); // Force the subscriber to be re-registered.
							connection->wants_log_subscriber                = true;
						}
						le_log::le_log_channel_i.info( logger.getChannel(), "Updated console log level mask to 0x%x", connection->log_level_mask );
					}
					break;
				default:
					le_log::le_log_channel_i.warn( logger.getChannel(), "Did not recognise command: '%s'", tokens[ 0 ].c_str() );
					break;
				}
			}
		}
	}
}

// ----------------------------------------------------------------------

static le_console_o* le_console_create() {
	static auto logger = le::Log( LOG_CHANNEL );
	logger.set_level( le::Log::Level::eDebug );
	auto self = new le_console_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_console_destroy( le_console_o* self ) {
	static auto logger = le::Log( LOG_CHANNEL );

	// Tear-down and delete server in case there was a server
	le_console_server_stop();
	// we can do this because le_log will never be reloaded as it is part of the core.

	logger.info( "Destroying console..." );
	delete self;
}

// ----------------------------------------------------------------------

static void le_console_inc_use_count() {

	le_console_o* self = produce_console();

	if ( self == nullptr ) {
		*PP_CONSOLE_SINGLETON = le_console_create();
		self                  = produce_console();
	}

	self->use_count++;
}

// ----------------------------------------------------------------------

static void le_console_dec_use_count() {
	le_console_o* self = produce_console();
	if ( self ) {
		if ( --self->use_count == 0 ) {
			le_console_destroy( self );
			*PP_CONSOLE_SINGLETON = nullptr;
		};
	}
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_console, api_ ) {

	auto  api          = static_cast<le_console_api*>( api_ );
	auto& le_console_i = api->le_console_i;

	le_console_i.inc_use_count = le_console_inc_use_count;
	le_console_i.dec_use_count = le_console_dec_use_count;
	le_console_i.server_start  = le_console_server_start;
	le_console_i.server_stop   = le_console_server_stop;
	le_console_i.process_input = le_console_process_input;

	auto& log_callbacks_i                    = api->log_callbacks_i;
	log_callbacks_i.push_chars_callback_addr = ( void* )logger_callback;

	// Load function pointers for private server object
	le_console_server_register_api( &le_console_server_api );

	PP_CONSOLE_SINGLETON = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_console_singleton" ) );

	if ( *PP_CONSOLE_SINGLETON ) {
		// if a console already exists, this is a sign that this module has been
		// reloaded - in which case we want to re-register the subscriber to the log.
		le_console_o* console = produce_console();
		if ( console->server ) {
			le_console_produce_server_watcher( console );
		}
		if ( !console->connections.empty() ) {
			auto  lock        = std::scoped_lock( console->connections_mutex );
			auto& subscribers = le_console_produce_log_subscribers();
			for ( auto& c : console->connections ) {
				if ( c.second->wants_log_subscriber ) {
					subscribers[ c.first ] = std::make_unique<LeLogSubscriber>( c.second.get() );
				}
			}
		}
	}
}
