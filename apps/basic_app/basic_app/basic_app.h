#ifndef GUARD_basic_app_H
#define GUARD_basic_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_basic_app_api( void *api );

struct basic_app_o;

// clang-format off
struct basic_app_api {

	static constexpr auto id      = "basic_app";
	static constexpr auto pRegFun = register_basic_app_api;

	struct basic_app_interface_t {
		basic_app_o * ( *create                   )();
		void         ( *destroy                  )( basic_app_o *self );
		bool         ( *update                   )( basic_app_o *self );

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	basic_app_interface_t basic_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace basic_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<basic_app_api>( true );
#else
const auto api = Registry::addApiStatic<basic_app_api>();
#endif

static const auto &basic_app_i = api -> basic_app_i;

} // namespace basic_app

class BasicApp : NoCopy, NoMove {

	basic_app_o *self;

  public:
	BasicApp()
	    : self( basic_app::basic_app_i.create() ) {
	}

	bool update() {
		return basic_app::basic_app_i.update( self );
	}

	~BasicApp() {
		basic_app::basic_app_i.destroy( self );
	}

	static void initialize() {
		basic_app::basic_app_i.initialize();
	}

	static void terminate() {
		basic_app::basic_app_i.terminate();
	}
};

#endif
