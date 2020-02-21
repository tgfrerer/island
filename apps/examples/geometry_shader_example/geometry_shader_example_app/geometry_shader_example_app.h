#ifndef GUARD_geometry_shader_example_app_H
#define GUARD_geometry_shader_example_app_H
#endif

#include <stdint.h>
#include "le_core/le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_geometry_shader_example_app_api( void *api );

struct geometry_shader_example_app_o;

// clang-format off
struct geometry_shader_example_app_api {

	static constexpr auto id      = "geometry_shader_example_app";
	static constexpr auto pRegFun = register_geometry_shader_example_app_api;

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

#ifdef __cplusplus
} // extern "C"

namespace geometry_shader_example_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<geometry_shader_example_app_api>( true );
#else
const auto api = Registry::addApiStatic<geometry_shader_example_app_api>();
#endif

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
