#include "le_core.h"
#include "le_hash_util.h"

#include "le_log.h"
#include "le_console.h"

#include <mutex>
#include <thread>
#include <deque>

#include <errno.h>
#include <string.h>

#include <memory>
#include <vector>
#include <iostream>
#include <fstream>

#include "private/le_console/le_console_types.h"
#include "private/le_console/le_console_server.h"

extern void le_console_server_register_api( void* api ); // in le_console_server
/*

Hot-reloading is more complicated for this module.

What we are doing is that we use a static Raii object to detect if the module is being destroyed,
in which case we use this Raii object to first remove us as a listener from the log object, and
then we end and join the server thread.

once that is done, this module can be safely hot-reloaded.

another thing we must do is that we want to restart the server once that the module has been reloaded.
we can do this by adding a singleton object and thereby detecting the state of play.

Ideally we find out whether we are hot-reloading or actually closing, and we close the network
connection only when we are really closing. that way we can keep clients connected during the
hot-reloading process.


is this a general problem with hot-reloading and multi-threading?
- quite possibly, we need to look more into this.

*/

static void**                  PP_CONSOLE_SINGLETON  = nullptr; // set via registration method
static le_console_server_api_t le_console_server_api = {};

static le_console_o* produce_console() {
	return static_cast<le_console_o*>( *PP_CONSOLE_SINGLETON );
}

// ----------------------------------------------------------------------
static void logger_callback( char* chars, uint32_t num_chars, void* user_data ) {
	static std::string msg;

	auto self = ( le_console_o* )user_data;

	auto lock = std::unique_lock( self->messages_mtx );

	while ( self->messages.size() >= 20 ) {
		self->messages.pop_front();
	} // make sure there are at most 19 lines waiting

	self->messages.emplace_back( chars, num_chars );
}

// ----------------------------------------------------------------------

// if we initialize an object from this and store it static,
// the destructor will get called when this module is being unloaded
// this allows us to remove ourselves from listeners before the listener
// gets destroyed.
class LeLogSubscriber : NoCopy {
  public:
	explicit LeLogSubscriber( le_console_o* console )
	    : handle( le_log::api->add_subscriber( logger_callback, console, ~uint32_t( 0 ) ) ){};
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
static std::unique_ptr<LeLogSubscriber>& le_console_produce_log_subscriber( le_console_o* console = nullptr ) {
	static std::unique_ptr<LeLogSubscriber> LOG_SUBSCRIBER = {};

	if ( nullptr == LOG_SUBSCRIBER.get() && console != nullptr ) {
		LOG_SUBSCRIBER = std::make_unique<LeLogSubscriber>( console );
	}
	return LOG_SUBSCRIBER;
}

// ----------------------------------------------------------------------

// we want to start the server on-demand.
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

	self->wants_log_subscriber = true;         // Set flag so that when reloading, we know that subscriber needs to be activated
	le_console_produce_log_subscriber( self ); // Registers subscriber in case subscriber is not yet registered

	self->server = le_console_server_api.create( self ); // destroy server
	le_console_server_api.start( self->server );         // start server
	le_console_produce_server_watcher( self );           // Implicitly starts server thread

	return true;
}

// ----------------------------------------------------------------------

static bool le_console_server_stop() {
	static auto logger = le::Log( LOG_CHANNEL );

	le_console_o* self = produce_console();

	if ( le_console_produce_log_subscriber().get() ) {
		logger.info( "* Unregistering Log subscriber ..." );
		le_console_produce_log_subscriber().reset( nullptr ); // Force the subscriber to be de-registered.
		self->wants_log_subscriber = false;                   // Set flag so that we know that subscriber was de-activated explicitly
	}

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

static le_console_o* le_console_create() {
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

	auto& log_callbacks_i                    = api->log_callbacks_i;
	log_callbacks_i.push_chars_callback_addr = ( void* )logger_callback;

	// Load function pointers for private server object
	le_console_server_register_api( &le_console_server_api );

	PP_CONSOLE_SINGLETON = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_console_singleton" ) );

	if ( *PP_CONSOLE_SINGLETON ) {
		// if a console already exists, this is a sign that this module has been
		// reloaded - in which case we want to re-register the subscriber to the log.
		le_console_o* console = produce_console();
		if ( console->wants_log_subscriber ) {
			le_console_produce_log_subscriber( console );
		}
		if ( console->server ) {
			le_console_produce_server_watcher( console );
		}
	}
}
