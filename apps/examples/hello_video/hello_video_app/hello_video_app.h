#ifndef GUARD_hello_video_app_H
#define GUARD_hello_video_app_H
#endif

#include "le_core/le_core.h"

struct hello_video_app_o;

// clang-format off
struct hello_video_app_api {

	struct hello_video_app_interface_t {
		hello_video_app_o * ( *create               )();
		void         ( *destroy                  )( hello_video_app_o *self );
		bool         ( *update                   )( hello_video_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	hello_video_app_interface_t hello_video_app_i;
};
// clang-format on

LE_MODULE( hello_video_app );
LE_MODULE_LOAD_DEFAULT( hello_video_app );

#ifdef __cplusplus

namespace hello_video_app {
static const auto &api            = hello_video_app_api_i;
static const auto &hello_video_app_i = api -> hello_video_app_i;
} // namespace hello_video_app

class HelloVideoApp : NoCopy, NoMove {

	hello_video_app_o *self;

  public:
	HelloVideoApp()
	    : self( hello_video_app::hello_video_app_i.create() ) {
	}

	bool update() {
		return hello_video_app::hello_video_app_i.update( self );
	}

	~HelloVideoApp() {
		hello_video_app::hello_video_app_i.destroy( self );
	}

	static void initialize() {
		hello_video_app::hello_video_app_i.initialize();
	}

	static void terminate() {
		hello_video_app::hello_video_app_i.terminate();
	}
};

#endif
