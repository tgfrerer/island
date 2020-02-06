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
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_timebase_o;
struct le_duration_t;

#define LE_TIME_TICKS_PER_SECOND 12000

void register_le_timebase_api( void *api );

// clang-format off
struct le_timebase_api {
	static constexpr auto id      = "le_timebase";
	static constexpr auto pRegFun = register_le_timebase_api;

	struct le_timebase_interface_t {

		le_timebase_o *    ( * create                   ) ( bool use_fixed_update_interval, uint64_t fixed_ticks_per_update );
		void               ( * destroy                  ) ( le_timebase_o* self );
		void               ( * update                   ) ( le_timebase_o* self );
		void               ( * reset                    ) ( le_timebase_o* self );

		uint64_t (*get_current_ticks)(le_timebase_o* self);
		uint64_t (*get_ticks_since_last_frame)(le_timebase_o* self);

	};

	le_timebase_interface_t       le_timebase_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_timebase {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_timebase_api>( true );
#	else
const auto api = Registry::addApiStatic<le_timebase_api>();
#	endif

static const auto &le_timebase_i = api -> le_timebase_i;

} // namespace le_timebase

class LeTimebase : NoCopy, NoMove {

	le_timebase_o *self;

  public:
	LeTimebase( bool use_fixed_update_interval = false, uint64_t fixed_ticks_per_update = ( LE_TIME_TICKS_PER_SECOND / 60 ) )
	    : self( le_timebase::le_timebase_i.create( use_fixed_update_interval, fixed_ticks_per_update ) ) {
	}

	~LeTimebase() {
		le_timebase::le_timebase_i.destroy( self );
	}

	void update() {
		le_timebase::le_timebase_i.update( self );
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
