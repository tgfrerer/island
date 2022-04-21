#ifndef GUARD_asterisks_app_H
#define GUARD_asterisks_app_H
#endif

#include "le_core.h"

struct asterisks_app_o;

// clang-format off
struct asterisks_app_api {

	struct asterisks_app_interface_t {
		asterisks_app_o * ( *create               )();
		void         ( *destroy                  )( asterisks_app_o *self );
		bool         ( *update                   )( asterisks_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	asterisks_app_interface_t asterisks_app_i;
};
// clang-format on

LE_MODULE( asterisks_app );
LE_MODULE_LOAD_DEFAULT( asterisks_app );

#ifdef __cplusplus

namespace asterisks_app {
static const auto& api             = asterisks_app_api_i;
static const auto& asterisks_app_i = api -> asterisks_app_i;
} // namespace asterisks_app

class AsterisksApp : NoCopy, NoMove {

	asterisks_app_o* self;

  public:
	AsterisksApp()
	    : self( asterisks_app::asterisks_app_i.create() ) {
	}

	bool update() {
		return asterisks_app::asterisks_app_i.update( self );
	}

	~AsterisksApp() {
		asterisks_app::asterisks_app_i.destroy( self );
	}

	static void initialize() {
		asterisks_app::asterisks_app_i.initialize();
	}

	static void terminate() {
		asterisks_app::asterisks_app_i.terminate();
	}
};

#endif
