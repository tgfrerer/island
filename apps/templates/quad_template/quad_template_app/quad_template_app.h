#ifndef GUARD_quad_template_app_H
#define GUARD_quad_template_app_H
#endif

#include "le_core/le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

struct quad_template_app_o;

// clang-format off
struct quad_template_app_api {

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

LE_MODULE(quad_template_app);
LE_MODULE_LOAD_DEFAULT(quad_template_app);

#ifdef __cplusplus

namespace quad_template_app {
static const auto &api = quad_template_app_api_i;
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
