#pragma once

enum SettingType : uint64_t {
	eInt       = hash_64_fnv1a_const( "int" ),
	eUint32_t  = hash_64_fnv1a_const( "uint32_t" ),
	eInt32_t   = hash_64_fnv1a_const( "int32_t" ),
	eStdString = hash_64_fnv1a_const( "std::string" ),
	eBool      = hash_64_fnv1a_const( "bool" ),
	eConstBool = hash_64_fnv1a_const( "const bool" ),
};
struct LeSettingEntry {
	std::string name;
	uint64_t    type_hash; // unique hash based on textual representation of type name. This is not perfect (no type aliasing possible), but should work with basic types
	void*       p_opj;     // pointer that may be set by the setter of this setting - it is their responsibility to delete this object
};

struct le_settings_map_t {
	std::unordered_map<uint64_t, LeSettingEntry> map; // fnv64_hash(name) -> entry
};
