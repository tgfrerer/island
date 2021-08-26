#ifndef GUARD_multi_window_example_app_H
#define GUARD_multi_window_example_app_H
#endif

#include <stdint.h>
#include "le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

void register_multi_window_example_app_api( void *api );

struct multi_window_example_app_o;

// clang-format off
struct multi_window_example_app_api {

	static constexpr auto id      = "multi_window_example_app";
	static constexpr auto pRegFun = register_multi_window_example_app_api;

	struct multi_window_example_app_interface_t {
		multi_window_example_app_o * ( *create              )();
		void         ( *destroy                  )( multi_window_example_app_o *self );
		bool         ( *update                   )( multi_window_example_app_o *self );

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	multi_window_example_app_interface_t multi_window_example_app_i;
};
// clang-format on

LE_MODULE( multi_window_example_app );
LE_MODULE_LOAD_DEFAULT( multi_window_example_app );

#ifdef __cplusplus
namespace multi_window_example_app {

static const auto &api = multi_window_example_app_api_i;

static const auto &multi_window_example_app_i = api -> multi_window_example_app_i;

} // namespace multi_window_example_app

class MultiWindowExampleApp : NoCopy, NoMove {

	multi_window_example_app_o *self;

  public:
	MultiWindowExampleApp()
	    : self( multi_window_example_app::multi_window_example_app_i.create() ) {
	}

	bool update() {
		return multi_window_example_app::multi_window_example_app_i.update( self );
	}

	~MultiWindowExampleApp() {
		multi_window_example_app::multi_window_example_app_i.destroy( self );
	}

	static void initialize() {
		multi_window_example_app::multi_window_example_app_i.initialize();
	}

	static void terminate() {
		multi_window_example_app::multi_window_example_app_i.terminate();
	}
};

#endif
