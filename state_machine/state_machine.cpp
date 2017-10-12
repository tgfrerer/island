#include "state_machine/state_machine.h"
#include <type_traits>

// ----------------------------------------------------------------------

traffic_light_o *createState( ) {
	traffic_light_o *newState = new traffic_light_o( );
	return newState;
}

// ----------------------------------------------------------------------

void destroyState( traffic_light_o *state ) {
	delete ( state );
}

// ----------------------------------------------------------------------

const State &get_state( traffic_light_o *instance ) {
	return instance->currentState;
}

// ----------------------------------------------------------------------

void nextState( traffic_light_o *instance ) {
	auto &s = instance->currentState;

	switch ( s ) {
	case State::eInitial:
		s = State::eGreen;
	    break;

	case State::eGreen:
		s = State::eOrange;
	    break;
	case State::eOrange:
		s = State::eRed;
	    break;
	case State::eRed:
		s = State::eGreen;
	    break;
	}
}

// ----------------------------------------------------------------------

void resetState( traffic_light_o *instance ) {
	instance->currentState = State::eInitial;
}

// ----------------------------------------------------------------------

const char *get_state_as_string( traffic_light_o *instance ) {
	const char *names[ 4 ] = {"initial", "green", "orange", "red"};

	auto index =
	    static_cast< std::underlying_type< decltype( instance->currentState ) >::type >( instance->currentState );

	return names[ index ];
}

// ----------------------------------------------------------------------

void register_api( void *api ) {
	auto a                 = static_cast< pal_state_machine_i * >( api );
	a->createState         = createState;
	a->destroyState        = destroyState;
	a->get_state           = get_state;
	a->next_state          = nextState;
	a->reset_state         = resetState;
	a->get_state_as_string = get_state_as_string;
}

// ----------------------------------------------------------------------
