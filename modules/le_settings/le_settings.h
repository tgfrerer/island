#ifndef GUARD_le_settings_H
#define GUARD_le_settings_H

#include "le_core.h"

struct le_settings_o;

// clang-format off
struct le_settings_api {

	struct le_settings_interface_t {
		void                 ( * list_all_settings) ();
        bool (*setting_set)( const char* setting_name, const char* setting_value );
	};

	le_settings_interface_t       le_settings_i;
};
// clang-format on

LE_MODULE( le_settings );
LE_MODULE_LOAD_DEFAULT( le_settings );

#ifdef __cplusplus

namespace le_settings {
static const auto& api           = le_settings_api_i;
static const auto& le_settings_i = api->le_settings_i;
} // namespace le_settings

namespace le {
class Settings : NoCopy, NoMove {

  public:
	static void list() {
		le_settings::le_settings_i.list_all_settings();
	}

	static void set( char const* settings_name, char const* setting_value ) {
		le_settings::le_settings_i.setting_set( settings_name, setting_value );
	}
};
} // namespace le

#endif // __cplusplus

#endif
