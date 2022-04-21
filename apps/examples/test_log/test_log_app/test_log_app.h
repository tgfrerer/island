#ifndef GUARD_test_log_app_H
#define GUARD_test_log_app_H
#endif

#include "le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

struct test_log_app_o;

// clang-format off
struct test_log_app_api {

	struct test_log_app_interface_t {
		test_log_app_o * ( *create               )();
		void         ( *destroy                  )( test_log_app_o *self );
		bool         ( *update                   )( test_log_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	test_log_app_interface_t test_log_app_i;
};
// clang-format on

LE_MODULE( test_log_app );
LE_MODULE_LOAD_DEFAULT( test_log_app );

#ifdef __cplusplus

namespace test_log_app {
static const auto& api            = test_log_app_api_i;
static const auto& test_log_app_i = api -> test_log_app_i;
} // namespace test_log_app

class TestLogApp : NoCopy, NoMove {

	test_log_app_o* self;

  public:
	TestLogApp()
	    : self( test_log_app::test_log_app_i.create() ) {
	}

	bool update() {
		return test_log_app::test_log_app_i.update( self );
	}

	~TestLogApp() {
		test_log_app::test_log_app_i.destroy( self );
	}

	static void initialize() {
		test_log_app::test_log_app_i.initialize();
	}

	static void terminate() {
		test_log_app::test_log_app_i.terminate();
	}
};

#endif
