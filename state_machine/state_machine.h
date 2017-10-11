#ifndef GUARD_STATE_MACHINE_H
#define GUARD_STATE_MACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_api(void *api);

enum class State : uint8_t {
  eInitial,
  eGreen,
  eOrange,
  eRed,
};

// declare state object
struct traffic_light_o {
  State currentState = State::eInitial;
};

// declare interface
struct pal_state_machine_i {
  traffic_light_o *(*createState)();
  void (*destroyState)(traffic_light_o *);
  const State &(*get_state)(traffic_light_o *instance);
  void (*next_state)(traffic_light_o *instance);
  void (*reset_state)(traffic_light_o *instance);
  const char *(*get_state_as_string)(traffic_light_o *instance);
};

#ifdef __cplusplus
}
#endif

#endif
