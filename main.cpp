#include "accum/accumulator.h"
#include "loader/ApiLoader.h"
#include "state_machine/state_machine.h"
#include <chrono>
#include <cstdlib>
#include <experimental/filesystem>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <string>
// ----------------------------------------------------------------------

// ----------------------------------------------------------------------

namespace std {
namespace filesystem {
using namespace experimental::filesystem;
}
}

int main(int argc, char const *argv[]) {

  //  std::filesystem::file_time_type lib_last_write{};

  int result = 0;

  pal_accum_i accum{};
  pal_state_machine_i stateMachineApi{};

  //  lib_last_write =
  //      std::filesystem::last_write_time("./accum/libaccumulator.so");

  auto apiLoaderAccum =
      std::make_unique<pal::ApiLoader>("./accum/libaccumulator.so");

  // ----------| invariant: api registered successfully
  apiLoaderAccum->register_api(&accum);

  auto apiLoaderStateMachine =
      std::make_unique<pal::ApiLoader>("./state_machine/libstate_machine.so");

  apiLoaderStateMachine->register_api(&stateMachineApi);

  auto trafficLight = stateMachineApi.createState();

  bool appShouldLoop = true;
  do {

	//    result = 0;

	//    // call function from instantiated api
	//    result = accum.add_two(result);
	//    result = accum.sub_three(result);
	//	std::cout << accum.get_text() << std::endl;
	//	std::cout << "the result is: " << std::dec << result << std::endl;

	std::cout << "Current state machine state: "
	          << stateMachineApi.get_state_as_string(trafficLight) << std::endl;

	char i = 0;
    std::cin >> i;

	switch (i) {
	case 'l': {
		std::cout << "reloading accumulator library" << std::endl;

	  apiLoaderAccum->reload();
	  apiLoaderAccum->register_api(&accum);
	  break;
	}
	case 'a': {
		std::cout << "reloading state machine library" << std::endl;
	  pal_state_machine_i tmp;

	  apiLoaderStateMachine->reload();
	  apiLoaderStateMachine->register_api(&tmp);
	  break;
	}
	case 'r': {
		stateMachineApi.reset_state(trafficLight);
	  break;
	}
	case 'i': {
		stateMachineApi.next_state(trafficLight);
	  break;
	}
	case 'q':
		appShouldLoop = false;
	  break;
	default:
	  break;
	};

  } while (appShouldLoop);

  std::cout << "and with this, goodbye." << std::endl;

  // main needs to load the dynamic library and then watch the dynamic library
  // file - if there is change the library needs to be re-loaded.

  return 0;
}
