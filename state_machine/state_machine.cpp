#include "state_machine/state_machine.h"
#include <type_traits>

// ----------------------------------------------------------------------

static TrafficLight *create( ) {
	TrafficLight *newObj = new TrafficLight{};
	return newObj;
}

// ----------------------------------------------------------------------

static void destroy( TrafficLight *obj ) {
	delete ( obj );
}

// ----------------------------------------------------------------------

static const State &get_state( TrafficLight *instance ) {
	return instance->currentState;
}

// ----------------------------------------------------------------------

static void nextState( TrafficLight *instance ) {
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

static void resetState( TrafficLight *instance ) {
	instance->currentState = State::eInitial;
}

// ----------------------------------------------------------------------

static const char *get_state_as_string( TrafficLight *instance ) {
	const char *names[ 5 ] = {"Initial", " Green", "Yellow", "\t\tBlink", "Red"};

	auto index =
	    static_cast< std::underlying_type< decltype( instance->currentState ) >::type >( instance->currentState );

	return names[ index ];
}

// ----------------------------------------------------------------------

void register_state_machine_api( void *api ) {
	auto a                 = static_cast< pal_state_machine_i * >( api );
	a->create              = create;
	a->destroy             = destroy;
	a->get_state           = get_state;
	a->next_state          = nextState;
	a->reset_state         = resetState;
	a->get_state_as_string = get_state_as_string;
}

// ----------------------------------------------------------------------
