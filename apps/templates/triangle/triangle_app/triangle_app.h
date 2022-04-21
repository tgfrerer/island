#ifndef GUARD_triangle_app_H
#define GUARD_triangle_app_H
#endif

#include "le_core.h"

struct triangle_app_o;

// clang-format off
struct triangle_app_api {

	struct triangle_app_interface_t {
		triangle_app_o * ( *create               )();
		void         ( *destroy                  )( triangle_app_o *self );
		bool         ( *update                   )( triangle_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	triangle_app_interface_t triangle_app_i;
};
// clang-format on

LE_MODULE( triangle_app );
LE_MODULE_LOAD_DEFAULT( triangle_app );

#ifdef __cplusplus

namespace triangle_app {
static const auto& api            = triangle_app_api_i;
static const auto& triangle_app_i = api -> triangle_app_i;
} // namespace triangle_app

class TriangleApp : NoCopy, NoMove {

	triangle_app_o* self;

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
