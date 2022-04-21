#ifndef GUARD_imgui_example_app_H
#define GUARD_imgui_example_app_H
#endif

#include <stdint.h>
#include "le_core.h"

struct imgui_example_app_o;

// clang-format off
struct imgui_example_app_api {

	struct imgui_example_app_interface_t {
		imgui_example_app_o * ( *create               )();
		void         ( *destroy                  )( imgui_example_app_o *self );
		bool         ( *update                   )( imgui_example_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	imgui_example_app_interface_t imgui_example_app_i;
};
// clang-format on

LE_MODULE( imgui_example_app );
LE_MODULE_LOAD_DEFAULT( imgui_example_app );

#ifdef __cplusplus

namespace imgui_example_app {
static const auto& api                 = imgui_example_app_api_i;
static const auto& imgui_example_app_i = api -> imgui_example_app_i;
} // namespace imgui_example_app

class ImguiExampleApp : NoCopy, NoMove {

	imgui_example_app_o* self;

  public:
	ImguiExampleApp()
	    : self( imgui_example_app::imgui_example_app_i.create() ) {
	}

	bool update() {
		return imgui_example_app::imgui_example_app_i.update( self );
	}

	~ImguiExampleApp() {
		imgui_example_app::imgui_example_app_i.destroy( self );
	}

	static void initialize() {
		imgui_example_app::imgui_example_app_i.initialize();
	}

	static void terminate() {
		imgui_example_app::imgui_example_app_i.terminate();
	}
};

#endif
