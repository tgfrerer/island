#ifndef GUARD_quad_bezier_app_H
#define GUARD_quad_bezier_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_quad_bezier_app_api( void *api );

struct quad_bezier_app_o;

// clang-format off
struct quad_bezier_app_api {

	static constexpr auto id      = "quad_bezier_app";
	static constexpr auto pRegFun = register_quad_bezier_app_api;

	struct quad_bezier_app_interface_t {
		quad_bezier_app_o * ( *create                   )();
		void         ( *destroy                  )( quad_bezier_app_o *self );
		bool         ( *update                   )( quad_bezier_app_o *self );

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	quad_bezier_app_interface_t quad_bezier_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace quad_bezier_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<quad_bezier_app_api>( true );
#else
const auto api = Registry::addApiStatic<quad_bezier_app_api>();
#endif

static const auto &quad_bezier_app_i = api -> quad_bezier_app_i;

} // namespace quad_bezier_app

class QuadBezierApp : NoCopy, NoMove {

	quad_bezier_app_o *self;

  public:
	QuadBezierApp()
	    : self( quad_bezier_app::quad_bezier_app_i.create() ) {
	}

	bool update() {
		return quad_bezier_app::quad_bezier_app_i.update( self );
	}

	~QuadBezierApp() {
		quad_bezier_app::quad_bezier_app_i.destroy( self );
	}

	static void initialize() {
		quad_bezier_app::quad_bezier_app_i.initialize();
	}

	static void terminate() {
		quad_bezier_app::quad_bezier_app_i.terminate();
	}
};

#endif
