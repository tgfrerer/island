#ifndef GUARD_hello_world_app_H
#define GUARD_hello_world_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_hello_world_app_api( void *api );

struct hello_world_app_o;

// clang-format off
struct hello_world_app_api {

	static constexpr auto id      = "hello_world_app";
	static constexpr auto pRegFun = register_hello_world_app_api;

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

#ifdef __cplusplus
} // extern "C"

namespace hello_world_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<hello_world_app_api>( true );
#else
const auto api = Registry::addApiStatic<hello_world_app_api>();
#endif

static const auto &hello_world_app_i = api -> hello_world_app_i;

} // namespace hello_world_app

class HelloWorldApp : NoCopy, NoMove {

	hello_world_app_o *self;

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
