#ifndef GUARD_TEST_APP_H
#define GUARD_TEST_APP_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_workbench_app_api( void *api );

struct workbench_app_o;

// clang-format off
struct workbench_app_api {

	static constexpr auto id      = "workbench_app";
	static constexpr auto pRegFun = register_workbench_app_api;

	struct workbench_app_interface_t {
		workbench_app_o * ( *create                   )();
		void         ( *destroy                  )( workbench_app_o *self );
		bool         ( *update                   )( workbench_app_o *self );

		void         ( *process_ui_events ) (workbench_app_o* self);

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	workbench_app_interface_t workbench_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace workbench_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<workbench_app_api>( true );
#else
const auto api = Registry::addApiStatic<workbench_app_api>();
#endif

static const auto &workbench_app_i = api -> workbench_app_i;

} // namespace workbench_app

class WorkbenchApp : NoCopy, NoMove {

	workbench_app_o *self;

  public:
	WorkbenchApp()
	    : self( workbench_app::workbench_app_i.create() ) {
	}

	bool update() {
		return workbench_app::workbench_app_i.update( self );
	}

	~WorkbenchApp() {
		workbench_app::workbench_app_i.destroy( self );
	}

	static void initialize() {
		workbench_app::workbench_app_i.initialize();
	}

	static void terminate() {
		workbench_app::workbench_app_i.terminate();
	}
};

#endif
