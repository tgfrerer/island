#ifndef GUARD_mesh_generator_example_app_H
#define GUARD_mesh_generator_example_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_mesh_generator_example_app_api( void *api );

struct mesh_generator_example_app_o;

// clang-format off
struct mesh_generator_example_app_api {

	static constexpr auto id      = "mesh_generator_example_app";
	static constexpr auto pRegFun = register_mesh_generator_example_app_api;

	struct mesh_generator_example_app_interface_t {
		mesh_generator_example_app_o * ( *create              )();
		void         ( *destroy                  )( mesh_generator_example_app_o *self );
		bool         ( *update                   )( mesh_generator_example_app_o *self );

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	mesh_generator_example_app_interface_t mesh_generator_example_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace mesh_generator_example_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<mesh_generator_example_app_api>( true );
#else
const auto api = Registry::addApiStatic<mesh_generator_example_app_api>();
#endif

static const auto &mesh_generator_example_app_i = api -> mesh_generator_example_app_i;

} // namespace mesh_generator_example_app

class MeshGeneratorExampleApp : NoCopy, NoMove {

	mesh_generator_example_app_o *self;

  public:
	MeshGeneratorExampleApp()
	    : self( mesh_generator_example_app::mesh_generator_example_app_i.create() ) {
	}

	bool update() {
		return mesh_generator_example_app::mesh_generator_example_app_i.update( self );
	}

	~MeshGeneratorExampleApp() {
		mesh_generator_example_app::mesh_generator_example_app_i.destroy( self );
	}

	static void initialize() {
		mesh_generator_example_app::mesh_generator_example_app_i.initialize();
	}

	static void terminate() {
		mesh_generator_example_app::mesh_generator_example_app_i.terminate();
	}
};

#endif
