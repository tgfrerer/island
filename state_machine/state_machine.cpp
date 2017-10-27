#include "state_machine/state_machine.h"
#include <type_traits>

// ----------------------------------------------------------------------

static pal_state_machine_o *create() {
	pal_state_machine_o *newObj = new pal_state_machine_o{};
	return newObj;
}

// ----------------------------------------------------------------------

static void destroy( pal_state_machine_o *obj ) {
	delete ( obj );
}

// ----------------------------------------------------------------------

static const State &get_state( pal_state_machine_o *instance ) {
	return instance->currentState;
}

// ----------------------------------------------------------------------

static void nextState( pal_state_machine_o *instance ) {
	auto &s = instance->currentState;

	switch ( s ) {
	case State::eInitial:
		s = State::eGreen;
	    break;
	case State::eGreen:
		s = State::eYellow;
	    break;
	case State::eYellow:
		s = State::eBlink;
	    break;
	case State::eBlink:
		s = State::eRed;
	    break;
	case State::eRed:
		s = State::eGreen;
	    break;
	}
}

// ----------------------------------------------------------------------

static void resetState( pal_state_machine_o *instance ) {
	instance->currentState = State::eInitial;
}

// ----------------------------------------------------------------------

static const char *get_state_as_string( pal_state_machine_o *instance ) {
	const char *names[ 5 ] = {"Initial", "Green", "Yellow", "\tBlink", "Red"};

	auto index =
	    static_cast<std::underlying_type<decltype( instance->currentState )>::type>( instance->currentState );

	return names[ index ];
}

// ----------------------------------------------------------------------

void register_state_machine_api( void *api ) {
	auto a                 = static_cast<pal_state_machine_i *>( api );
	a->create              = create;
	a->destroy             = destroy;
	a->get_state           = get_state;
	a->next_state          = nextState;
	a->reset_state         = resetState;
	a->get_state_as_string = get_state_as_string;
}

// ----------------------------------------------------------------------
