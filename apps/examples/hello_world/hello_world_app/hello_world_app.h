#ifndef GUARD_hello_world_app_H
#define GUARD_hello_world_app_H
#endif

#include <stdint.h>
#include "le_core.h"

struct hello_world_app_o;

// clang-format off
struct hello_world_app_api {

	struct hello_world_app_interface_t {
		hello_world_app_o * ( *create              )();
		void         ( *destroy                  )( hello_world_app_o *self );
		bool         ( *update                   )( hello_world_app_o *self );

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	hello_world_app_interface_t hello_world_app_i;
};
// clang-format on

LE_MODULE( hello_world_app );
LE_MODULE_LOAD_DEFAULT( hello_world_app );

#ifdef __cplusplus

namespace hello_world_app {
static const auto& api               = hello_world_app_api_i;
static const auto& hello_world_app_i = api -> hello_world_app_i;
} // namespace hello_world_app

class HelloWorldApp : NoCopy, NoMove {

	hello_world_app_o* self;

  public:
	HelloWorldApp()
	    : self( hello_world_app::hello_world_app_i.create() ) {
	}

	bool update() {
		return hello_world_app::hello_world_app_i.update( self );
	}

	~HelloWorldApp() {
		hello_world_app::hello_world_app_i.destroy( self );
	}

	static void initialize() {
		hello_world_app::hello_world_app_i.initialize();
	}

	static void terminate() {
		hello_world_app::hello_world_app_i.terminate();
	}
};

#endif
