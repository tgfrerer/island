#ifndef GUARD_show_font_app_H
#define GUARD_show_font_app_H
#endif

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_show_font_app_api( void *api );

struct show_font_app_o;

// clang-format off
struct show_font_app_api {

	static constexpr auto id      = "show_font_app";
	static constexpr auto pRegFun = register_show_font_app_api;

	struct show_font_app_interface_t {
		show_font_app_o * ( *create               )();
		void         ( *destroy                  )( show_font_app_o *self );
		bool         ( *update                   )( show_font_app_o *self );
		void         ( *initialize               )(); // static methods
		void         ( *terminate                )(); // static methods
	};

	show_font_app_interface_t show_font_app_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace show_font_app {
#ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<show_font_app_api>( true );
#else
const auto api = Registry::addApiStatic<show_font_app_api>();
#endif

static const auto &show_font_app_i = api -> show_font_app_i;

} // namespace show_font_app

class ShowFontApp : NoCopy, NoMove {

	show_font_app_o *self;

  public:
	ShowFontApp()
	    : self( show_font_app::show_font_app_i.create() ) {
	}

	bool update() {
		return show_font_app::show_font_app_i.update( self );
	}

	~ShowFontApp() {
		show_font_app::show_font_app_i.destroy( self );
	}

	static void initialize() {
		show_font_app::show_font_app_i.initialize();
	}

	static void terminate() {
		show_font_app::show_font_app_i.terminate();
	}
};

#endif
