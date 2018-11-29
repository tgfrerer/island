#ifndef GUARD_mipmap_example_app_H
#define GUARD_mipmap_example_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_mipmap_example_app_api( void *api );

struct mipmap_example_app_o;

// clang-format off
struct mipmap_example_app_api {

	static constexpr auto id      = "mipmap_example_app";
	static constexpr auto pRegFun = register_mipmap_example_app_api;

	struct mipmap_example_app_interface_t {
		mipmap_example_app_o * ( *create                   )();
		void         ( *destroy                  )( mipmap_example_app_o *self );
		bool         ( *update                   )( mipmap_example_app_o *self );
		
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	mipmap_example_app_interface_t mipmap_example_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace mipmap_example_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<mipmap_example_app_api>( true );
#else
const auto api = Registry::addApiStatic<mipmap_example_app_api>();
#endif

static const auto &mipmap_example_app_i = api -> mipmap_example_app_i;

} // namespace mipmap_example_app

class MipmapExampleApp : NoCopy, NoMove {

	mipmap_example_app_o *self;

  public:
	MipmapExampleApp()
	    : self( mipmap_example_app::mipmap_example_app_i.create() ) {
	}

	bool update() {
		return mipmap_example_app::mipmap_example_app_i.update( self );
	}

	~MipmapExampleApp() {
		mipmap_example_app::mipmap_example_app_i.destroy( self );
	}

	static void initialize() {
		mipmap_example_app::mipmap_example_app_i.initialize();
	}

	static void terminate() {
		mipmap_example_app::mipmap_example_app_i.terminate();
	}
};

#endif
