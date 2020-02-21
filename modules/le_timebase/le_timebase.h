#ifndef GUARD_le_timebase_H
#define GUARD_le_timebase_H

/*
 * Timebase defines a canonical time basis for animations, and to measure time durations.
 *
 * Timebase needs to be updated once per frame update, and can then be queried read-only
 * throughout any methods called from within the same frame update.
 *
 * Internally, all time durations are expressed in Ticks, of which fit
 * LE_TIME_TICKS_PER_SECOND into one second.
 *
 */

#include <stdint.h>
#include "le_core/le_core.h"

struct le_timebase_o;
struct le_duration_t;

#define LE_TIME_TICKS_PER_SECOND 12000

struct le_timebase_api {

	struct le_timebase_interface_t {

		le_timebase_o *( *create )();
		void ( *destroy )( le_timebase_o *self );
		void ( *update )( le_timebase_o *self, uint64_t fixed_interval_ticks ); /// set fixed_interval_ticks to 0 for clock-based interval
		void ( *reset )( le_timebase_o *self );

		uint64_t ( *get_current_ticks )( le_timebase_o *self );
		uint64_t ( *get_ticks_since_last_frame )( le_timebase_o *self );
	};

	le_timebase_interface_t le_timebase_i;
};
// clang-format on

LE_MODULE( le_timebase );
LE_MODULE_LOAD_DEFAULT( le_timebase );

#ifdef __cplusplus

namespace le_timebase {
static const auto &api           = le_timebase_api_i;
static const auto &le_timebase_i = api -> le_timebase_i;

} // namespace le_timebase

class LeTimebase : NoCopy, NoMove {

	le_timebase_o *self;

  public:
	LeTimebase()
	    : self( le_timebase::le_timebase_i.create() ) {
	}

	~LeTimebase() {
		le_timebase::le_timebase_i.destroy( self );
	}

	void update( uint64_t fixed_interval_ticks = 0 ) {
		le_timebase::le_timebase_i.update( self, fixed_interval_ticks );
	}

	void reset() {
		le_timebase::le_timebase_i.reset( self );
	}

	uint64_t getCurrentTicks() {
		return le_timebase::le_timebase_i.get_current_ticks( self );
	}

	uint64_t getTicksSinceLastFrame() {
		return le_timebase::le_timebase_i.get_ticks_since_last_frame( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
