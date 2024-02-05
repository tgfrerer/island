#ifndef GUARD_le_timebase_H
#define GUARD_le_timebase_H

/*
 * Timebase defines a canonical time basis for animations, and to measure time durations.
 *
 * Timebase needs to be updated once per frame update, and can then be queried read-only
 * throughout any methods called from within the same frame update.
 *
 * If you call update with a delta value, timebase's internal clock is advanced by this
 * value, interpreted as a duration given in `le::Ticks`.
 *
 * If you omit a value, or update timebase with value 0, then the system steady clock will
 * be used to calculate the delta duration since the last update.
 *
 * ----------------------------------------------------------------------
 *
 * Internally, all time durations are expressed in le::Ticks, of which fit
 * LE_TIME_TICKS_PER_SECOND into one second.
 *
 * By default, we are using microsecond (1/1'000'000) resolution.
 *
 * You can override this by defining LE_TIME_TICKS_PER_SECOND in the master
 * CMakeLists.txt file, with the directive:
 *
 * add_compile_definitions( LE_TIME_TICKS_PER_SECOND=1000 )
 *
 * Note that if you use a resolution that is too coarse, the smoothness of your
 * animations will suffer.
 *
 * ----------------------------------------------------------------------
 *
 * You can cast le::Tick back to durations by including:
 *
 * NOTE: You *must* include <chrono> before you include `le_timebase_ticks_type.h`
 *

   #include <chrono>
   #include "private/le_timebase/le_timebase_ticks_type.h"

 *
 * ----------------------------------------------------------------------
 *
 * The following shows how to cast le::Ticks to seconds, for example:
 *

    le::Ticks my_ticks( app->timebase.getTicksSinceLastFrame() );  // first  convert from raw ticks count to le::Ticks
    float delta_seconds = std::chrono::duration_cast<std::chrono::duration<float>>( my_ticks ).count(); // then convert from le::Ticks to seconds

 *
 *
 * Note that a conversion to a non-rational (floating-point-type) duration will be lossy,
 * as are all floating point operations.
 *
 * You can of course duration_cast to any type of std::chrono::duration.
 *
 * If you want to enforce a fixed time interval:
 *

    constexpr auto USE_FIXED_TIME_INTERVAL = true;

    if ( USE_FIXED_TIME_INTERVAL ) {
        self->timebase.update(
            std::chrono::duration_cast<le::Ticks>(
                std::chrono::duration<float, std::ratio<1, 60 * 1>>( 1 ) )
                .count() );
    } else {
        self->timebase.update();
    }

*/

#include <stdint.h>
#include "le_core.h"

struct le_timebase_o;

#ifndef LE_TIME_TICKS_PER_SECOND
#	define LE_TIME_TICKS_PER_SECOND 1000000
#endif

// clang-format off
struct le_timebase_api {

	struct le_timebase_interface_t {

		le_timebase_o* ( *create )();
		void ( *destroy )( le_timebase_o* self );
		void ( *update )( le_timebase_o* self, uint64_t fixed_interval_ticks ); /// set fixed_interval_ticks to 0 for clock-based interval
		void ( *reset )( le_timebase_o* self );

		uint64_t ( *get_current_ticks )( le_timebase_o* self );
		uint64_t ( *get_ticks_since_last_frame )( le_timebase_o* self );
	};

	le_timebase_interface_t le_timebase_i;
};
// clang-format on

LE_MODULE( le_timebase );
LE_MODULE_LOAD_DEFAULT( le_timebase );

#ifdef __cplusplus

namespace le_timebase {
static const auto& api           = le_timebase_api_i;
static const auto& le_timebase_i = api->le_timebase_i;

} // namespace le_timebase

class LeTimebase : NoCopy, NoMove {

	le_timebase_o* self;

  public:
	LeTimebase()
	    : self( le_timebase::le_timebase_i.create() ) {
	}

	~LeTimebase() {
		le_timebase::le_timebase_i.destroy( self );
	}

	void update( uint64_t delta_ticks = 0 ) {
		le_timebase::le_timebase_i.update( self, delta_ticks );
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
