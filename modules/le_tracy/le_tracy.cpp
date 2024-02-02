#include "le_tracy.h"
#include "le_core.h"
#include "le_hash_util.h"
#include "le_log.h"
#include <mutex>
#include "cassert"
#include <memory>

struct le_tracy_o {
	uint32_t log_level_mask = 0;
};

static le_tracy_o* produce_tracy() {
	static std::mutex mtx;
	auto              lock = std::unique_lock( mtx );

	auto& le_tracy_singleton = const_cast<le_tracy_api*>( le_tracy::api )->le_tracy_singleton;

	if ( le_tracy_singleton == nullptr ) {
		le_tracy_singleton = new le_tracy_o();
	}

	return le_tracy_singleton;
}

// ----------------------------------------------------------------------
// This method may not log via le::log, because otherwise there is a
// chance of a deadlock.
static void logger_callback( char const* chars, uint32_t num_chars, void* user_data ) {

	// issue commands for tracy to log messages
	TracyMessage( chars, num_chars );
}

// ----------------------------------------------------------------------

// If we initialize an object from this and store it static,
// then the destructor will get called when this module is being unloaded
// this allows us to remove ourselves from listeners before the listener
// gets destroyed.
class LeLogSubscriber : NoCopy {
  public:
	explicit LeLogSubscriber( uint32_t log_level_mask )
	    : handle( le_log::api->add_subscriber( logger_callback, nullptr, log_level_mask ) ) {
		static auto logger = le::Log( "le_tracy" );
		logger.debug( "Adding new Log subscriber for le_tracy with mask 0x%x", log_level_mask );
	}
	~LeLogSubscriber() {
		static auto logger = le::Log( "le_tracy" );
		logger.debug( "Removing Log subscriber" );
		// we must remove the subscriber because it may get called before we have a chance to change the callback address -
		// even callback forwarding won't help, because the reloader will log- and this log event will happen while the
		// current module is not yet loaded, which means there is no valid code to run for the subscriber.
		le_log::api->remove_subscriber( handle );
	};

  private:
	uint64_t handle;
};

// we use a method like this so that we can intercept the moment the subscriber gets unloaded
// when this library gets unloaded - in which case we want to be sure to unregister this
// particular subscriber.
// it will get registered again once it gets requested for the first time.
std::unique_ptr<LeLogSubscriber>& produce_log_subscriber() {
	static std::unique_ptr<LeLogSubscriber> subscriber;
	return subscriber;
}

// ----------------------------------------------------------------------

static void le_tracy_update_subscriber( uint32_t log_level_mask ) {
	// we must reset / or re-create a subscriber.
	if ( log_level_mask == 0 ) {
		// remove subscriber if we are not listening to any messages
		produce_log_subscriber().reset();
	} else {
		// add a new subscriber - this will implicitly remove the previous one if there
		// previously has been a subscriber.
		auto& subscriber = produce_log_subscriber();
		subscriber       = std::make_unique<LeLogSubscriber>( log_level_mask );
	}
}

static void le_tracy_enable_log( uint32_t log_level_mask ) {

#ifdef TRACY_ENABLE

	// this might initialise a tracy object in case none did previously exist.
	le_tracy_o* self = produce_tracy();

	if ( self->log_level_mask != log_level_mask ) {
		le_tracy_update_subscriber( log_level_mask );
	}

	self->log_level_mask = log_level_mask;
#else
	auto logger = le::Log( "le_tracy" );
	logger.warn( "Tracy is not enabled - enable tracy by adding compile definition `TRACY_ENABLE` to your project cmake file." );
#endif
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_tracy, api ) {
	auto& le_tracy_i = static_cast<le_tracy_api*>( api )->le_tracy_i;

	le_tracy_i.enable_log = le_tracy_enable_log;

#ifdef LE_LOAD_TRACING_LIBRARY
	LE_LOAD_TRACING_LIBRARY;
#endif

#ifdef TRACY_ENABLE

	if ( le_tracy::api->le_tracy_singleton ) {
		// if a tracy instance already exists, this is a sign that this module has been
		// reloaded - in which case we want to re-register the subscriber to the log,
		le_tracy_update_subscriber( le_tracy::api->le_tracy_singleton->log_level_mask );
	}
#endif
}
