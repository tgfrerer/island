#include "le_settings.h"
#include "le_core.h"
#include "le_hash_util.h"
#include "le_log.h"

#include <string>
#include <unordered_map>

// The main issue with this architecture is that settings will still compete for the same
// pointer target.

// Ideas for this module:
// we can load settings from a file - any matching settings values get overwritten / set
//
enum SettingType {
	eInt       = hash_64_fnv1a_const( "int" ),
	eUint32_t  = hash_64_fnv1a_const( "uint32_t" ),
	eInt32_t   = hash_64_fnv1a_const( "int32_t" ),
	eStdString = hash_64_fnv1a_const( "std::string" ),
	eBool      = hash_64_fnv1a_const( "bool" ),
};

#include "private/le_core/le_settings_private_types.inl"

// ----------------------------------------------------------------------

static void le_setting_set( const char* setting_name, const char* setting_value ) {
	static auto logger = le::Log( "settings" );
	bool        result = le_core_update_setting_value( setting_name, setting_value );
	if ( false == result ) {
		logger.warn( "Could not update setting '%s' value to '%s'", setting_name, setting_value );
	}
}

// ----------------------------------------------------------------------

static void le_settings_list_all_settings() {
	le_settings_map_t current_settings;

	// create a local copy of global settings
	le_core_copy_settings_entries( &current_settings );

	static auto logger = le::Log( "settings" );

	// this should have copied all settings to our current settings

	for ( auto& s : current_settings.map ) {

		switch ( s.second.type_hash ) {
		case ( SettingType::eBool ):
			logger.info( "setting '%s' type: '%s', value: '%d'", s.second.name.c_str(), "bool", *( ( bool* )s.second.p_opj ) );
			break;
		case ( SettingType::eInt ):
			logger.info( "setting '%s' type: '%s', value: '%d'", s.second.name.c_str(), "int", *( ( int* )s.second.p_opj ) );
			break;
		case ( SettingType::eStdString ):
			logger.info( "setting '%s' type: '%s', value: '%s'", s.second.name.c_str(), "std::string", ( ( std::string* )s.second.p_opj )->c_str() );
			break;
		default:
			logger.warn( "setting '%30s' has unknown type.", s.second.name.c_str() );
			break;
		}
	}

	// do something with self
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_settings, api ) {
	auto& le_settings_i = static_cast<le_settings_api*>( api )->le_settings_i;

	le_settings_i.list_all_settings = le_settings_list_all_settings;
	le_settings_i.setting_set       = le_setting_set;
}
