#ifndef GUARD_test_file_watch_app_H
#define GUARD_test_file_watch_app_H
#endif

#include "le_core/le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

struct test_file_watch_app_o;

// clang-format off
struct test_file_watch_app_api {

	struct test_file_watch_app_interface_t {
		test_file_watch_app_o * ( *create               )();
		void         ( *destroy                  )( test_file_watch_app_o *self );
		bool         ( *update                   )( test_file_watch_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	test_file_watch_app_interface_t test_file_watch_app_i;
};
// clang-format on

LE_MODULE( test_file_watch_app );
LE_MODULE_LOAD_DEFAULT( test_file_watch_app );

#ifdef __cplusplus

namespace test_file_watch_app {
static const auto &api                 = test_file_watch_app_api_i;
static const auto &test_file_watch_app_i = api -> test_file_watch_app_i;
} // namespace test_file_watch_app

class TestFileWatchApp : NoCopy, NoMove {

	test_file_watch_app_o *self;

  public:
	TestFileWatchApp()
	    : self( test_file_watch_app::test_file_watch_app_i.create() ) {
	}

	bool update() {
		return test_file_watch_app::test_file_watch_app_i.update( self );
	}

	~TestFileWatchApp() {
		test_file_watch_app::test_file_watch_app_i.destroy( self );
	}

	static void initialize() {
		test_file_watch_app::test_file_watch_app_i.initialize();
	}

	static void terminate() {
		test_file_watch_app::test_file_watch_app_i.terminate();
	}
};

#endif
