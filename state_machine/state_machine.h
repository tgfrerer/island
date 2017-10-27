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

// declare state machine object
struct pal_state_machine_o {
	State currentState = State::eInitial;
};

// declare interface
struct pal_state_machine_i {
	pal_state_machine_o *( *create )();
	void ( *destroy )( pal_state_machine_o * );
	const State &( *get_state )( pal_state_machine_o *instance );
	void ( *next_state )( pal_state_machine_o *instance );
	void ( *reset_state )( pal_state_machine_o *instance );
	const char *( *get_state_as_string )( pal_state_machine_o *instance );
};




#ifdef __cplusplus
}
#endif

#endif // GUARD_STATE_MACHINE_H
