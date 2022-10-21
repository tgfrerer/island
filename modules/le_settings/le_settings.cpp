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

static void le_settings_list_all_settings( le_settings_o* self ) {

	// create a local copy of global settings
	le_core_copy_settings_entries( &self->current_settings );

	static auto logger = le::Log( "settings" );

	// this should have copied all settings to our current settings
	enum SettingType {
		eInt,
		eUint32_t,
		eInt32_t,
		eStdString,
		eBool,
	};

	static const std::unordered_map<uint64_t, SettingType> type_lookup = {
	    { hash_64_fnv1a_const( "uint32_t" ), SettingType::eUint32_t },
	    { hash_64_fnv1a_const( "int32_t" ), SettingType::eInt32_t },
	    { hash_64_fnv1a_const( "int" ), SettingType::eInt },
	    { hash_64_fnv1a_const( "std::string" ), SettingType::eStdString },
	    { hash_64_fnv1a_const( "bool" ), SettingType::eBool },
	};

	for ( auto& s : self->current_settings.map ) {
		auto it = type_lookup.find( s.second.type_hash );
		if ( it != type_lookup.end() ) {

			int name_len = s.second.name.rfind( "__" );

			switch ( it->second ) {
			case ( SettingType::eBool ):
				logger.info( "setting '%.*s' type: '%s', value: '%d'", name_len, s.second.name.c_str(), "bool", *( ( bool* )s.second.p_opj ) );
				break;
			case ( SettingType::eInt ):
				logger.info( "setting '%.*s' type: '%s', value: '%d'", name_len, s.second.name.c_str(), "int", *( ( int* )s.second.p_opj ) );
				break;
			case ( SettingType::eStdString ):
				logger.info( "setting '%.*s' type: '%s', value: '%s'", name_len, s.second.name.c_str(), "std::string", ( ( std::string* )s.second.p_opj )->c_str() );
				break;
			default:
				break;
			}
		} else {
			logger.warn( "setting '%30s' has unknown type.", s.second.name.c_str() );
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
}
