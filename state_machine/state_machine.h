#ifndef GUARD_STATE_MACHINE_H
#define GUARD_STATE_MACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_state_machine_api( void *api );

enum class State : uint8_t {
	eInitial,
	eGreen,
	eYellow,
	eBlink,
	eRed,
};

// declare state object
struct TrafficLight {
	State currentState = State::eInitial;
};

// declare interface
struct pal_state_machine_i {
	TrafficLight *( *create )( );
	void ( *destroy )( TrafficLight * );
	const State &( *get_state )( TrafficLight *instance );
	void ( *next_state )( TrafficLight *instance );
	void ( *reset_state )( TrafficLight *instance );
	const char *( *get_state_as_string )( TrafficLight *instance );
};

#ifdef __cplusplus
}
#endif

#endif
