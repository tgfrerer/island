#ifndef GUARD_STATE_MACHINE_H
#define GUARD_STATE_MACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum class State : uint8_t {
	eInitial,
	eGreen,
	eYellow,
	eBlink,
	eRed,
};

void register_traffic_light_api( void *api );

struct pal_traffic_light_i;
struct pal_traffic_light_o;

// declare interface
struct pal_traffic_light_i {

	static constexpr auto id      = "traffic_light";
	static constexpr auto pRegFun = register_traffic_light_api;

	pal_traffic_light_o * ( *create )             ( pal_traffic_light_i * );
	void                  ( *destroy )            ( pal_traffic_light_o * );
	const State &         ( *get_state )          ( pal_traffic_light_o * );
	void                  ( *step )               ( pal_traffic_light_o * );
	void                  ( *reset_state )        ( pal_traffic_light_o * );
	const char *          ( *get_state_as_string )( pal_traffic_light_o * );
};

// declare state machine object
struct pal_traffic_light_o {
	pal_traffic_light_i *vtable;
	State                currentState = State::eInitial;
};

#ifdef __cplusplus

// ----------------------------------------------------------------------

namespace pal {

class TrafficLight {
	pal_traffic_light_i *const interface;
	pal_traffic_light_o *const obj;

  public:
	TrafficLight( pal_traffic_light_i *interface_ )
	    : interface( interface_ )
	    , obj( interface->create( interface ) ) {
	}

	~TrafficLight() {
		obj->vtable->destroy( obj );
	}

	void step() {
		obj->vtable->step( obj );
	}

	const char *getStateAsString() {
		return obj->vtable->get_state_as_string( obj );
	}
};
} // namespace pal

} // extern "C"
#endif

#endif // GUARD_STATE_MACHINE_H
