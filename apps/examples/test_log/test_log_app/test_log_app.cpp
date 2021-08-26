#include "test_log_app.h"
#include "le_log.h"

#include <chrono>
#include <thread>

struct test_log_app_o {
	uint64_t          frame_counter = 0;
	le_log_channel_o *logger;
};

typedef test_log_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize(){};

// ----------------------------------------------------------------------

static void app_terminate(){};

// ----------------------------------------------------------------------

static test_log_app_o *test_log_app_create() {
	auto app = new ( test_log_app_o );

	app->logger = le_log_api_i->get_channel( "app_logger" );

	return app;
}

// ----------------------------------------------------------------------

static bool test_log_app_update( test_log_app_o *self ) {

	auto logger_2 = LeLog( "logger_2" );
	logger_2.set_level( LeLog::Level::eInfo );
	logger_2.info( "Logger_2 says hello from frame: %d", self->frame_counter );
	std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

	auto app_logger = LeLog( self->logger );
	app_logger.warn( "oops a warning from the app logger" );
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	app_logger.error( "now an error even." );

	// sleep for one second
	std::this_thread::sleep_for( std::chrono::milliseconds( 400 ) );
	self->frame_counter++;

	if ( self->frame_counter > 3 ) {
		return false;
	} else {

		return true; // keep app alive
	}
}

// ----------------------------------------------------------------------

static void test_log_app_destroy( test_log_app_o *self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( test_log_app, api ) {

	auto  test_log_app_api_i = static_cast<test_log_app_api *>( api );
	auto &test_log_app_i     = test_log_app_api_i->test_log_app_i;

	test_log_app_i.initialize = app_initialize;
	test_log_app_i.terminate  = app_terminate;

	test_log_app_i.create  = test_log_app_create;
	test_log_app_i.destroy = test_log_app_destroy;
	test_log_app_i.update  = test_log_app_update;
}
