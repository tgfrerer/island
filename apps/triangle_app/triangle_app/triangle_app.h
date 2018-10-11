#ifndef GUARD_triangle_app_H
#define GUARD_triangle_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_triangle_app_api( void *api );

struct triangle_app_o;

// clang-format off
struct triangle_app_api {

	static constexpr auto id      = "triangle_app";
	static constexpr auto pRegFun = register_triangle_app_api;

	struct triangle_app_interface_t {
		triangle_app_o * ( *create                   )();
		void         ( *destroy                  )( triangle_app_o *self );
		bool         ( *update                   )( triangle_app_o *self );

		void         ( *key_callback             )( void *user_data, int key, int scancode, int action, int mods );
		void         ( *character_callback       )( void *user_data, unsigned int codepoint );
		void         ( *cursor_position_callback )( void *user_data, double xpos, double ypos );
		void         ( *cursor_enter_callback    )( void *user_data, int entered );
		void         ( *mouse_button_callback    )( void *user_data, int button, int action, int mods );
		void         ( *scroll_callback          )( void *user_data, double xoffset, double yoffset );

		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	triangle_app_interface_t triangle_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace triangle_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<triangle_app_api>( true );
#else
const auto api = Registry::addApiStatic<triangle_app_api>();
#endif

static const auto &triangle_app_i = api -> triangle_app_i;

} // namespace triangle_app

class TriangleApp : NoCopy, NoMove {

	triangle_app_o *self;

  public:
	TriangleApp()
	    : self( triangle_app::triangle_app_i.create() ) {
	}

	bool update() {
		return triangle_app::triangle_app_i.update( self );
	}

	~TriangleApp() {
		triangle_app::triangle_app_i.destroy( self );
	}

	static void initialize() {
		triangle_app::triangle_app_i.initialize();
	}

	static void terminate() {
		triangle_app::triangle_app_i.terminate();
	}
};

#endif
