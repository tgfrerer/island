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

#include "private/le_core/le_settings_private_types.inl"

// ----------------------------------------------------------------------

static bool le_setting_set( const char* setting_name, const char* setting_value ) {

	// Find entry in map - if we can find it, attempt to set it
	if ( nullptr == setting_name ) {
		return false;
	}

	// ---------- invariant: name is valid

	auto found_setting = le_core_get_setting_entry( setting_name );

	if ( found_setting != nullptr ) {
		void* setting = found_setting->p_opj;
		switch ( found_setting->type_hash ) {
		case SettingType::eBool:
			*( bool* )( setting ) = bool( std::strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eUint32_t:
			*( uint32_t* )( setting ) = bool( strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eInt32_t:
			*( int32_t* )( setting ) = int32_t( strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eInt:
			*( int* )( setting ) = int( strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eStdString:
			*( std::string* )( setting ) = std::string( setting_value );
			break;
		default:
			return false;
			break;
		}
	}
	return true;
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
