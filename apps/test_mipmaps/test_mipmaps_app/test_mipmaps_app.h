#ifndef GUARD_test_mipmaps_app_H
#define GUARD_test_mipmaps_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_test_mipmaps_app_api( void *api );

struct test_mipmaps_app_o;

// clang-format off
struct test_mipmaps_app_api {

	static constexpr auto id      = "test_mipmaps_app";
	static constexpr auto pRegFun = register_test_mipmaps_app_api;

	struct test_mipmaps_app_interface_t {
		test_mipmaps_app_o * ( *create                   )();
		void         ( *destroy                  )( test_mipmaps_app_o *self );
		bool         ( *update                   )( test_mipmaps_app_o *self );
		
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	test_mipmaps_app_interface_t test_mipmaps_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace test_mipmaps_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<test_mipmaps_app_api>( true );
#else
const auto api = Registry::addApiStatic<test_mipmaps_app_api>();
#endif

static const auto &test_mipmaps_app_i = api -> test_mipmaps_app_i;

} // namespace test_mipmaps_app

class TestMipmapsApp : NoCopy, NoMove {

	test_mipmaps_app_o *self;

  public:
	TestMipmapsApp()
	    : self( test_mipmaps_app::test_mipmaps_app_i.create() ) {
	}

	bool update() {
		return test_mipmaps_app::test_mipmaps_app_i.update( self );
	}

	~TestMipmapsApp() {
		test_mipmaps_app::test_mipmaps_app_i.destroy( self );
	}

	static void initialize() {
		test_mipmaps_app::test_mipmaps_app_i.initialize();
	}

	static void terminate() {
		test_mipmaps_app::test_mipmaps_app_i.terminate();
	}
};

#endif
