#include "le_timebase.h"

#include <chrono>
#include "le_core/le_core.h"

using Tick     = std::chrono::duration<uint64_t, std::ratio<1, LE_TIME_TICKS_PER_SECOND>>; /// LE_TIME_TICKS_PER_SECOND ticks per second.
using NanoTime = std::chrono::time_point<std::chrono::steady_clock>;

struct le_timebase_o {

	NanoTime now;                          // time point at update()
	NanoTime initial_time;                 // time point at reset()
	Tick     ticks_before_update;          // number of total ticks passed up until last update
	Tick     ticks_before_previous_update; // number of total ticks passed up until previous update
};

static void le_timebase_reset( le_timebase_o *self ) {

	self->ticks_before_update          = {}; // should reset to zero
	self->ticks_before_previous_update = {}; // should reset to zero

	self->now = std::chrono::steady_clock::now();

	self->initial_time = self->now;
}

// ----------------------------------------------------------------------

static le_timebase_o *le_timebase_create() {
	auto self = new le_timebase_o();
	le_timebase_reset( self );
	return self;
}

// ----------------------------------------------------------------------

static void le_timebase_destroy( le_timebase_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_timebase_update( le_timebase_o *self, uint64_t fixed_interval ) {

	self->ticks_before_previous_update = self->ticks_before_update;

	if ( fixed_interval ) {
		self->ticks_before_update += Tick( fixed_interval );
		self->now = NanoTime( self->initial_time ) + std::chrono::duration_cast<NanoTime::duration>( self->ticks_before_update );
	} else {
		self->now                 = std::chrono::steady_clock::now();
		self->ticks_before_update = std::chrono::duration_cast<Tick>( self->now - self->initial_time );
	}
}

// ----------------------------------------------------------------------

static uint64_t le_timebase_get_current_ticks( le_timebase_o *self ) {
	return self->ticks_before_update.count();
}

// ----------------------------------------------------------------------

static uint64_t le_timebase_get_ticks_since_last_frame( le_timebase_o *self ) {
	return ( self->ticks_before_update - self->ticks_before_previous_update ).count();
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_timebase_api( void *api ) {
	auto &le_timebase_i                      = static_cast<le_timebase_api *>( api )->le_timebase_i;
	le_timebase_i.get_current_ticks          = le_timebase_get_current_ticks;
	le_timebase_i.get_ticks_since_last_frame = le_timebase_get_ticks_since_last_frame;
	le_timebase_i.reset                      = le_timebase_reset;
	le_timebase_i.create                     = le_timebase_create;
	le_timebase_i.destroy                    = le_timebase_destroy;
	le_timebase_i.update                     = le_timebase_update;
}
