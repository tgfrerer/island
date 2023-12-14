#include "le_timebase.h"

#include <chrono>
#include "private/le_timebase/le_timebase_ticks_type.h"

using TimeTicks = std::chrono::time_point<std::chrono::steady_clock, le::Ticks>;

struct le_timebase_o {

	TimeTicks now;                          // time point at update()
	TimeTicks initial_time;                 // time point at reset()
	le::Ticks ticks_before_update;          // number of total ticks passed up until last update
	le::Ticks ticks_before_previous_update; // number of total ticks passed up until previous update
};

// ----------------------------------------------------------------------

static void le_timebase_reset( le_timebase_o* self ) {

	self->ticks_before_update          = {}; // should reset to zero
	self->ticks_before_previous_update = {}; // should reset to zero

	self->now = std::chrono::round<le::Ticks>( std::chrono::steady_clock::now() );

	self->initial_time = self->now;
}

// ----------------------------------------------------------------------

static le_timebase_o* le_timebase_create() {
	auto self = new le_timebase_o();
	le_timebase_reset( self );
	return self;
}

// ----------------------------------------------------------------------

static void le_timebase_destroy( le_timebase_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_timebase_update( le_timebase_o* self, uint64_t delta_ticks ) {

	self->ticks_before_previous_update = self->ticks_before_update;

	if ( delta_ticks ) {
		self->ticks_before_update += le::Ticks( delta_ticks );
		self->now = self->initial_time + self->ticks_before_update;
	} else {
		self->now                 = std::chrono::round<le::Ticks>( std::chrono::steady_clock::now() );
		self->ticks_before_update = self->now - self->initial_time;
	}
}

// ----------------------------------------------------------------------

static uint64_t le_timebase_get_current_ticks( le_timebase_o* self ) {
	return self->ticks_before_update.count();
}

// ----------------------------------------------------------------------

static uint64_t le_timebase_get_ticks_since_last_frame( le_timebase_o* self ) {
	return ( self->ticks_before_update - self->ticks_before_previous_update ).count();
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_timebase, api ) {
	auto& le_timebase_i                      = static_cast<le_timebase_api*>( api )->le_timebase_i;
	le_timebase_i.get_current_ticks          = le_timebase_get_current_ticks;
	le_timebase_i.get_ticks_since_last_frame = le_timebase_get_ticks_since_last_frame;
	le_timebase_i.reset                      = le_timebase_reset;
	le_timebase_i.create                     = le_timebase_create;
	le_timebase_i.destroy                    = le_timebase_destroy;
	le_timebase_i.update                     = le_timebase_update;
}
