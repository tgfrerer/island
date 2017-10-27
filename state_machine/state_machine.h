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

struct pal_state_machine_i;
struct pal_state_machine_o;

// declare interface
struct pal_state_machine_i {
	pal_state_machine_o * ( *create )             ( pal_state_machine_i * );
	void                  ( *destroy )            ( pal_state_machine_o * );
	const State &         ( *get_state )          ( pal_state_machine_o * );
	void                  ( *next_state )         ( pal_state_machine_o * );
	void                  ( *reset_state )        ( pal_state_machine_o * );
	const char *          ( *get_state_as_string )( pal_state_machine_o * );
};

// declare state machine object
struct pal_state_machine_o {
	pal_state_machine_i *       vtable;
	State currentState = State::eInitial;
};


#ifdef __cplusplus

// ----------------------------------------------------------------------

namespace pal{

class StateMachine {
	pal_state_machine_i * const interface;
	pal_state_machine_o * const obj;

public:
	StateMachine( pal_state_machine_i *interface_)
	    : interface( interface_ )
	    , obj( interface->create(interface) ){
	}

	~StateMachine(){
		obj->vtable->destroy(obj);
	}

	void nextState(){
		obj->vtable->next_state(obj);
	}

	const char* getStateAsString(){
		return obj->vtable->get_state_as_string(obj);
	}
};
} // namespace pal

} // extern "C"
#endif

#endif // GUARD_STATE_MACHINE_H
