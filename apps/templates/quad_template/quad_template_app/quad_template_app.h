#ifndef GUARD_quad_template_app_H
#define GUARD_quad_template_app_H
#endif

#include <stdint.h>
#include "le_core/le_core.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_quad_template_app_api( void *api );

struct quad_template_app_o;

// clang-format off
struct quad_template_app_api {

	static constexpr auto id      = "quad_template_app";
	static constexpr auto pRegFun = register_quad_template_app_api;

	struct quad_template_app_interface_t {
		quad_template_app_o * ( *create               )();
		void         ( *destroy                  )( quad_template_app_o *self );
		bool         ( *update                   )( quad_template_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	quad_template_app_interface_t quad_template_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace quad_template_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<quad_template_app_api>( true );
#else
const auto api = Registry::addApiStatic<quad_template_app_api>();
#endif

static const auto &quad_template_app_i = api -> quad_template_app_i;

} // namespace quad_template_app

class QuadTemplateApp : NoCopy, NoMove {

	quad_template_app_o *self;

  public:
	QuadTemplateApp()
	    : self( quad_template_app::quad_template_app_i.create() ) {
	}

	bool update() {
		return quad_template_app::quad_template_app_i.update( self );
	}

	~QuadTemplateApp() {
		quad_template_app::quad_template_app_i.destroy( self );
	}

	static void initialize() {
		quad_template_app::quad_template_app_i.initialize();
	}

	static void terminate() {
		quad_template_app::quad_template_app_i.terminate();
	}
};

#endif
