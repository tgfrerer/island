#ifndef GUARD_compute_example_app_H
#define GUARD_compute_example_app_H
#endif

#include <stdint.h>
#include "le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

void register_compute_example_app_api( void *api );

struct compute_example_app_o;

// clang-format off
struct compute_example_app_api {

	static constexpr auto id      = "compute_example_app";
	static constexpr auto pRegFun = register_compute_example_app_api;

	struct compute_example_app_interface_t {
		compute_example_app_o * ( *create               )();
		void         ( *destroy                  )( compute_example_app_o *self );
		bool         ( *update                   )( compute_example_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	compute_example_app_interface_t compute_example_app_i;
};
// clang-format on

LE_MODULE( compute_example_app );
LE_MODULE_LOAD_DEFAULT( compute_example_app );

#ifdef __cplusplus
namespace compute_example_app {

static const auto &api = compute_example_app_api_i;

static const auto &compute_example_app_i = api -> compute_example_app_i;

} // namespace compute_example_app

class ComputeExampleApp : NoCopy, NoMove {

	compute_example_app_o *self;

  public:
	ComputeExampleApp()
	    : self( compute_example_app::compute_example_app_i.create() ) {
	}

	bool update() {
		return compute_example_app::compute_example_app_i.update( self );
	}

	~ComputeExampleApp() {
		compute_example_app::compute_example_app_i.destroy( self );
	}

	static void initialize() {
		compute_example_app::compute_example_app_i.initialize();
	}

	static void terminate() {
		compute_example_app::compute_example_app_i.terminate();
	}
};

#endif
