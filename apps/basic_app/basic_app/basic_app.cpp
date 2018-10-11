#include "basic_app.h"

#include "le_font/le_font.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

#include <chrono> // for nanotime
using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct basic_app_o {
	float    deltaTimeSec = 0;
	NanoTime update_start_time;
};

// ----------------------------------------------------------------------

static void initialize(){};

// ----------------------------------------------------------------------

static void terminate(){};

// ----------------------------------------------------------------------

static basic_app_o *basic_app_create() {
	auto app               = new ( basic_app_o );
	app->update_start_time = std::chrono::high_resolution_clock::now();
	return app;
}

// ----------------------------------------------------------------------

static bool basic_app_update( basic_app_o *self ) {

	{
		// update frame delta time
		auto   current_time = std::chrono::high_resolution_clock::now();
		double millis       = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>( current_time - self->update_start_time ).count();
		self->deltaTimeSec  = float( millis / 1000.0 );
		//		self->metrics.appUpdateTimes.push( millis );
		self->update_start_time = current_time;
	}

	LeFont a;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void basic_app_destroy( basic_app_o *self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_basic_app_api( void *api ) {
	auto  basic_app_api_i = static_cast<basic_app_api *>( api );
	auto &basic_app_i     = basic_app_api_i->basic_app_i;

	basic_app_i.initialize = initialize;
	basic_app_i.terminate  = terminate;

	basic_app_i.create  = basic_app_create;
	basic_app_i.destroy = basic_app_destroy;
	basic_app_i.update  = basic_app_update;
}
