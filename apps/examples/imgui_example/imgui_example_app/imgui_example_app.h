#ifndef GUARD_imgui_example_app_H
#define GUARD_imgui_example_app_H
#endif

#include <stdint.h>
#include "le_core/le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_imgui_example_app_api( void *api );

struct imgui_example_app_o;

// clang-format off
struct imgui_example_app_api {

	static constexpr auto id      = "imgui_example_app";
	static constexpr auto pRegFun = register_imgui_example_app_api;

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

#ifdef __cplusplus
} // extern "C"

namespace imgui_example_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<imgui_example_app_api>( true );
#else
const auto api = Registry::addApiStatic<imgui_example_app_api>();
#endif

static const auto &imgui_example_app_i = api -> imgui_example_app_i;

} // namespace imgui_example_app

class ImguiExampleApp : NoCopy, NoMove {

	imgui_example_app_o *self;

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
