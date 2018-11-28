#ifndef GUARD_lensflare_app_H
#define GUARD_lensflare_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_lensflare_app_api( void *api );

struct lensflare_app_o;

// clang-format off
struct lensflare_app_api {

	static constexpr auto id      = "lensflare_app";
	static constexpr auto pRegFun = register_lensflare_app_api;

	struct lensflare_app_interface_t {
		lensflare_app_o * ( *create              )();
		void         ( *destroy                  )( lensflare_app_o *self );
		bool         ( *update                   )( lensflare_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	lensflare_app_interface_t lensflare_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace lensflare_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<lensflare_app_api>( true );
#else
const auto api = Registry::addApiStatic<lensflare_app_api>();
#endif

static const auto &lensflare_app_i = api -> lensflare_app_i;

} // namespace lensflare_app

class LensflareApp : NoCopy, NoMove {

	lensflare_app_o *self;

  public:
	LensflareApp()
	    : self( lensflare_app::lensflare_app_i.create() ) {
	}

	bool update() {
		return lensflare_app::lensflare_app_i.update( self );
	}

	~LensflareApp() {
		lensflare_app::lensflare_app_i.destroy( self );
	}

	static void initialize() {
		lensflare_app::lensflare_app_i.initialize();
	}

	static void terminate() {
		lensflare_app::lensflare_app_i.terminate();
	}
};

#endif
