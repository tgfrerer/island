#ifndef GUARD_lut_grading_example_app_H
#define GUARD_lut_grading_example_app_H
#endif

#include "le_core.h"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

struct lut_grading_example_app_o;

// clang-format off
struct lut_grading_example_app_api {

	struct lut_grading_example_app_interface_t {
		lut_grading_example_app_o * ( *create               )();
		void         ( *destroy                  )( lut_grading_example_app_o *self );
		bool         ( *update                   )( lut_grading_example_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	lut_grading_example_app_interface_t lut_grading_example_app_i;
};
// clang-format on

LE_MODULE( lut_grading_example_app );
LE_MODULE_LOAD_DEFAULT( lut_grading_example_app );

#ifdef __cplusplus

namespace lut_grading_example_app {
static const auto& api                       = lut_grading_example_app_api_i;
static const auto& lut_grading_example_app_i = api -> lut_grading_example_app_i;
} // namespace lut_grading_example_app

class LutGradingExampleApp : NoCopy, NoMove {

	lut_grading_example_app_o* self;

  public:
	LutGradingExampleApp()
	    : self( lut_grading_example_app::lut_grading_example_app_i.create() ) {
	}

	bool update() {
		return lut_grading_example_app::lut_grading_example_app_i.update( self );
	}

	~LutGradingExampleApp() {
		lut_grading_example_app::lut_grading_example_app_i.destroy( self );
	}

	static void initialize() {
		lut_grading_example_app::lut_grading_example_app_i.initialize();
	}

	static void terminate() {
		lut_grading_example_app::lut_grading_example_app_i.terminate();
	}
};

#endif
