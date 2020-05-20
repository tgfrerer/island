#ifndef GUARD_geometry_shader_example_app_H
#define GUARD_geometry_shader_example_app_H
#endif

#include <stdint.h>
#include "le_core/le_core.h"

struct geometry_shader_example_app_o;

// clang-format off
struct geometry_shader_example_app_api {

	struct geometry_shader_example_app_interface_t {
		geometry_shader_example_app_o * ( *create              )();
		void         ( *destroy                  )( geometry_shader_example_app_o *self );
		bool         ( *update                   )( geometry_shader_example_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	geometry_shader_example_app_interface_t geometry_shader_example_app_i;
};
// clang-format on

LE_MODULE( geometry_shader_example_app );
LE_MODULE_LOAD_DEFAULT( geometry_shader_example_app );

#ifdef __cplusplus

namespace geometry_shader_example_app {
static const auto &api                           = geometry_shader_example_app_api_i;
static const auto &geometry_shader_example_app_i = api -> geometry_shader_example_app_i;

} // namespace geometry_shader_example_app

class GeometryShaderExampleApp : NoCopy, NoMove {

	geometry_shader_example_app_o *self;

  public:
	GeometryShaderExampleApp()
	    : self( geometry_shader_example_app::geometry_shader_example_app_i.create() ) {
	}

	bool update() {
		return geometry_shader_example_app::geometry_shader_example_app_i.update( self );
	}

	~GeometryShaderExampleApp() {
		geometry_shader_example_app::geometry_shader_example_app_i.destroy( self );
	}

	static void initialize() {
		geometry_shader_example_app::geometry_shader_example_app_i.initialize();
	}

	static void terminate() {
		geometry_shader_example_app::geometry_shader_example_app_i.terminate();
	}
};

#endif
