#ifndef GUARD_hello_triangle_app_H
#define GUARD_hello_triangle_app_H
#endif

#include "le_core/le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

struct hello_triangle_app_o;

// clang-format off
struct hello_triangle_app_api {

	struct hello_triangle_app_interface_t {
		hello_triangle_app_o * ( *create               )();
		void         ( *destroy                  )( hello_triangle_app_o *self );
		bool         ( *update                   )( hello_triangle_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	hello_triangle_app_interface_t hello_triangle_app_i;
};
// clang-format on

LE_MODULE( hello_triangle_app );
LE_MODULE_LOAD_DEFAULT( hello_triangle_app );

#ifdef __cplusplus

namespace hello_triangle_app {
static const auto &api            = hello_triangle_app_api_i;
static const auto &hello_triangle_app_i = api -> hello_triangle_app_i;
} // namespace hello_triangle_app

class HelloTriangleApp : NoCopy, NoMove {

	hello_triangle_app_o *self;

  public:
	HelloTriangleApp()
	    : self( hello_triangle_app::hello_triangle_app_i.create() ) {
	}

	bool update() {
		return hello_triangle_app::hello_triangle_app_i.update( self );
	}

	~HelloTriangleApp() {
		hello_triangle_app::hello_triangle_app_i.destroy( self );
	}

	static void initialize() {
		hello_triangle_app::hello_triangle_app_i.initialize();
	}

	static void terminate() {
		hello_triangle_app::hello_triangle_app_i.terminate();
	}
};

#endif
