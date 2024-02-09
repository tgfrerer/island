#ifndef GUARD_exr_decode_example_app_H
#define GUARD_exr_decode_example_app_H
#endif

#include <stdint.h>
#include "le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

void register_exr_decode_example_app_api( void* api );

struct exr_decode_example_app_o;

// clang-format off
struct exr_decode_example_app_api {

	static constexpr auto id      = "exr_decode_example_app";
	static constexpr auto pRegFun = register_exr_decode_example_app_api;

	struct exr_decode_example_app_interface_t {
		exr_decode_example_app_o * ( *create               )();
		void         ( *destroy                  )( exr_decode_example_app_o *self );
		bool         ( *update                   )( exr_decode_example_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	exr_decode_example_app_interface_t exr_decode_example_app_i;
};
// clang-format on

LE_MODULE( exr_decode_example_app );
LE_MODULE_LOAD_DEFAULT( exr_decode_example_app );

#ifdef __cplusplus
namespace exr_decode_example_app {

static const auto& api = exr_decode_example_app_api_i;

static const auto& exr_decode_example_app_i = api -> exr_decode_example_app_i;

} // namespace exr_decode_example_app

class ExrDecodeExampleApp : NoCopy, NoMove {

	exr_decode_example_app_o* self;

  public:
	ExrDecodeExampleApp()
	    : self( exr_decode_example_app::exr_decode_example_app_i.create() ) {
	}

	bool update() {
		return exr_decode_example_app::exr_decode_example_app_i.update( self );
	}

	~ExrDecodeExampleApp() {
		exr_decode_example_app::exr_decode_example_app_i.destroy( self );
	}

	static void initialize() {
		exr_decode_example_app::exr_decode_example_app_i.initialize();
	}

	static void terminate() {
		exr_decode_example_app::exr_decode_example_app_i.terminate();
	}
};

#endif
