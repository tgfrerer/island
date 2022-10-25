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

struct LeSettingEntry {
	std::string name;
	uint64_t    type_hash; // unique hash based on textual representation of type name. This is not perfect (no type aliasing possible), but should work with basic types
	void*       p_opj;     // pointer that may be set by the setter of this setting - it is their responsibility to delete this object
};

struct le_settings_map_t {
	std::unordered_map<uint64_t, LeSettingEntry> map;
};

struct le_settings_o {
	le_settings_map_t current_settings;
};

// ----------------------------------------------------------------------

static le_settings_o* le_settings_create() {
	auto self = new le_settings_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_settings_destroy( le_settings_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_settings_set( const char* setting_name, const char* setting_value ) {
	static auto logger = le::Log( "settings" );

	le_settings_map_t current_settings;
	le_core_copy_settings_entries( &current_settings );

	// find setting with given name
	uint64_t name_hash     = hash_64_fnv1a( setting_name );
	auto     found_setting = current_settings.map.find( name_hash );

	void** setting = le_core_produce_setting_entry( setting_name, nullptr );

	if ( found_setting != current_settings.map.end() ) {
		switch ( found_setting->second.type_hash ) {
		case SettingType::eBool:
			*( bool* )( *setting ) = bool( std::strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eUint32_t:
			*( uint32_t* )( *setting ) = bool( strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eInt32_t:
			*( int32_t* )( *setting ) = int32_t( strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eInt:
			*( int* )( *setting ) = int( strtoul( setting_value, nullptr, 10 ) );
			break;
		case SettingType::eStdString:
			*( std::string* )( *setting ) = std::string( setting_value );
			break;
		default:
			logger.warn( "Cannot set setting of unknown type." );
			break;
		}
	} else {
		logger.warn( "Could not find setting '%s' - cannot set it to value '%s'", setting_name, setting_value );
	}
}

// ----------------------------------------------------------------------

static void le_settings_list_all_settings( le_settings_o* self ) {

	// create a local copy of global settings
	le_core_copy_settings_entries( &self->current_settings );

	static auto logger = le::Log( "settings" );

	// this should have copied all settings to our current settings

	for ( auto& s : self->current_settings.map ) {

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

	le_settings_i.create            = le_settings_create;
	le_settings_i.destroy           = le_settings_destroy;
	le_settings_i.list_all_settings = le_settings_list_all_settings;
	le_settings_i.setting_set       = le_settings_set;
}
