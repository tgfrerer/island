#ifndef GUARD_screenshot_example_app_H
#define GUARD_screenshot_example_app_H
#endif

#include "le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

struct screenshot_example_app_o;

// clang-format off
struct screenshot_example_app_api {

	struct screenshot_example_app_interface_t {
		screenshot_example_app_o * ( *create               )();
		void         ( *destroy                  )( screenshot_example_app_o *self );
		bool         ( *update                   )( screenshot_example_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	screenshot_example_app_interface_t screenshot_example_app_i;
};
// clang-format on

LE_MODULE( screenshot_example_app );
LE_MODULE_LOAD_DEFAULT( screenshot_example_app );

#ifdef __cplusplus

namespace screenshot_example_app {
static const auto& api                 = screenshot_example_app_api_i;
static const auto& screenshot_example_app_i = api -> screenshot_example_app_i;
} // namespace screenshot_example_app

class ScreenshotExampleApp : NoCopy, NoMove {

	screenshot_example_app_o* self;

  public:
	ScreenshotExampleApp()
	    : self( screenshot_example_app::screenshot_example_app_i.create() ) {
	}

	bool update() {
		return screenshot_example_app::screenshot_example_app_i.update( self );
	}

	~ScreenshotExampleApp() {
		screenshot_example_app::screenshot_example_app_i.destroy( self );
	}

	static void initialize() {
		screenshot_example_app::screenshot_example_app_i.initialize();
	}

	static void terminate() {
		screenshot_example_app::screenshot_example_app_i.terminate();
	}
};

#endif
