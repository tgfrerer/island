#ifndef GUARD_test_dependency_H
#define GUARD_test_dependency_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_test_dependency_api( void *api );

struct test_dependency_o;

// clang-format off
struct test_dependency_api {

	static constexpr auto id      = "test_dependency";
	static constexpr auto pRegFun = register_test_dependency_api;

	struct test_dependency_interface_t {
		test_dependency_o * ( *create                   )();
		void         ( *destroy                  )( test_dependency_o *self );
		bool         ( *update                   )( test_dependency_o *self );

		void         ( *key_callback             )( void *user_data, int key, int scancode, int action, int mods );
		void         ( *character_callback       )( void *user_data, unsigned int codepoint );
		void         ( *cursor_position_callback )( void *user_data, double xpos, double ypos );
		void         ( *cursor_enter_callback    )( void *user_data, int entered );
		void         ( *mouse_button_callback    )( void *user_data, int button, int action, int mods );
		void         ( *scroll_callback          )( void *user_data, double xoffset, double yoffset );

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	test_dependency_interface_t test_dependency_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace triangle_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<test_dependency_api>( true );
#else
const auto api = Registry::addApiStatic<test_dependency_api>();
#endif

static const auto &test_dependency_i = api -> test_dependency_i;

} // namespace triangle_app

class TestDependency : NoCopy, NoMove {

	test_dependency_o *self;

  public:
	TestDependency()
	    : self( triangle_app::test_dependency_i.create() ) {
	}

	bool update() {
		return triangle_app::test_dependency_i.update( self );
	}

	~TestDependency() {
		triangle_app::test_dependency_i.destroy( self );
	}

	static void initialize() {
		triangle_app::test_dependency_i.initialize();
	}

	static void terminate() {
		triangle_app::test_dependency_i.terminate();
	}
};

#endif
