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

struct pal_traffic_light_api;
struct pal_traffic_light_o;

// declare interface
struct pal_traffic_light_api {

	static constexpr auto id      = "traffic_light";
	static constexpr auto pRegFun = register_traffic_light_api;

	struct traffic_light_interface_t{
		pal_traffic_light_o * ( *create )             ( );
		void                  ( *destroy )            ( pal_traffic_light_o * );
		const State &         ( *get_state )          ( pal_traffic_light_o * );
		void                  ( *step )               ( pal_traffic_light_o * );
		void                  ( *reset_state )        ( pal_traffic_light_o * );
		const char *          ( *get_state_as_string )( pal_traffic_light_o * );
	} traffic_light_i;

};


#ifdef __cplusplus

} // extern "C"

// ----------------------------------------------------------------------

#include "registry/ApiRegistry.hpp"

namespace pal {

class TrafficLight {
	pal_traffic_light_api::traffic_light_interface_t *const interface;
	pal_traffic_light_o *const                              obj;

  public:
	TrafficLight()
	    : interface( &Registry::getApi<pal_traffic_light_api>()->traffic_light_i )
	    , obj(interface->create()) {
	}

	~TrafficLight() {
		interface->destroy( obj );
	}

	void step() {
		interface->step( obj );
	}

	const char *getStateAsString() {
		return interface->get_state_as_string( obj );
	}
};
} // namespace pal

#endif

#endif // GUARD_STATE_MACHINE_H
