#include "le_parameter_store.h"
#include "le_core.h"
#include <unordered_map>
#include "assert.h"
#include <string>
#include <fstream>    // for file loading
#include <iostream>   // for file loading
#include <filesystem> // to test whether file exists
#include <vector>

#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include "3rdparty/jsmn/jsmn.h"

using Type = le_parameter_store_api::Type;

static const char *PARAMETER_TYPE_AS_STRING[ 5 ] = {
    "Unknown",
    "Float",
    "U32",
    "I32",
    "Bool",
};

struct le_parameter_o {
	le_parameter_store_api::Type type;
	union Data {
		float    as_float[ 3 ];
		uint32_t as_u32[ 3 ];
		int32_t  as_i32[ 3 ];
		bool     as_bool[ 3 ];
		char     as_bytes[ 4 ][ 3 ];
	};
	Data value; // value, range_min, range_max
};

static float *le_parameter_set_float( le_parameter_o *self, float val, float val_min = 0.f, float val_max = 1.f ) {
	if ( nullptr == self ) {
		return nullptr;
	}
	self->type                = Type::eFloat;
	self->value.as_float[ 0 ] = val;
	self->value.as_float[ 1 ] = val_min;
	self->value.as_float[ 2 ] = val_max;
	return &self->value.as_float[ 0 ];
}
static uint32_t *le_parameter_set_u32( le_parameter_o *self, uint32_t val, uint32_t val_min = 0, uint32_t val_max = UINT32_MAX ) {
	if ( nullptr == self ) {
		return nullptr;
	}
	self->type              = Type::eU32;
	self->value.as_u32[ 0 ] = val;
	self->value.as_u32[ 1 ] = val_min;
	self->value.as_u32[ 2 ] = val_max;
	return &self->value.as_u32[ 0 ];
}
static int32_t *le_parameter_set_i32( le_parameter_o *self, int32_t val, int32_t val_min = INT32_MIN, int32_t val_max = INT32_MAX ) {
	if ( nullptr == self ) {
		return nullptr;
	}
	self->type              = Type::eI32;
	self->value.as_i32[ 0 ] = val;
	self->value.as_i32[ 1 ] = val_min;
	self->value.as_i32[ 2 ] = val_max;
	return &self->value.as_i32[ 0 ];
}
static bool *le_parameter_set_bool( le_parameter_o *self, bool val ) {
	if ( nullptr == self ) {
		return nullptr;
	}
	self->type               = Type::eBool;
	self->value.as_bool[ 0 ] = val;
	self->value.as_bool[ 1 ] = false;
	self->value.as_bool[ 2 ] = true;
	return &self->value.as_bool[ 0 ];
}
// Note: getters will return nullptr if type does not match internal parameter type.
static float *le_parameter_as_float( le_parameter_o *self ) {
	if ( nullptr == self || self->type != Type::eFloat ) {
		return nullptr;
	}
	return &self->value.as_float[ 0 ];
}
static uint32_t *le_parameter_as_u32( le_parameter_o *self ) {
	if ( nullptr == self || self->type != Type::eU32 ) {
		return nullptr;
	}
	return &self->value.as_u32[ 0 ];
}
static int32_t *le_parameter_as_i32( le_parameter_o *self ) {
	if ( nullptr == self || self->type != Type::eI32 ) {
		return nullptr;
	}
	return &self->value.as_i32[ 0 ];
}
static bool *le_parameter_as_bool( le_parameter_o *self ) {
	if ( nullptr == self || self->type != Type::eBool ) {
		return nullptr;
	}
	return &self->value.as_bool[ 0 ];
}
static void le_parameter_set_type( le_parameter_o *self, Type type ) {
	if ( nullptr == self ) {
		return;
	}
	self->type = type;
}
static Type le_parameter_get_type( le_parameter_o *self ) {
	if ( nullptr == self ) {
		return Type::eUnknown;
	}
	return self->type;
}

// ----------------------------------------------------------------------
struct le_parameter_store_o {
	std::unordered_map<std::string, le_parameter_o> store;
};

// ----------------------------------------------------------------------

static le_parameter_o *le_parameter_store_get_parameter( le_parameter_store_o *self, char const *name ) {
	auto it = self->store.find( name );
	if ( it == self->store.end() ) {
		return nullptr;
	}
	return &it->second;
}

// ----------------------------------------------------------------------
// Search for given parameter, based on its address.
// return parameter name if found, nullptr if not found.
static char const *le_parameter_store_get_name( le_parameter_store_o *self, le_parameter_o *param ) {
	auto it = self->store.begin();
	for ( ; it != self->store.end(); it++ ) {
		if ( &it->second == param ) {
			break;
		}
	}
	if ( it == self->store.end() ) {
		return nullptr;
	}
	return it->first.c_str();
}

// ----------------------------------------------------------------------

static le_parameter_o *le_parameter_store_add_parameter( le_parameter_store_o *self, char const *name ) {
	return &self->store[ name ];
}

// ----------------------------------------------------------------------

void print_entry( FILE *file, std::string const &name, le_parameter_o const &param ) {
	fprintf( file, "\n\t\"%s\": {", name.c_str() );
	fprintf( file, "\n\t\t\"type\":\"%s\",", PARAMETER_TYPE_AS_STRING[ param.type ] );

	switch ( param.type ) {
	case Type::eFloat: {
		fprintf( file, "\n\t\t\"value\":\"%.7g\",", param.value.as_float[ 0 ] );
		fprintf( file, "\n\t\t\"min_value\":\"%.7g\",", param.value.as_float[ 1 ] );
		fprintf( file, "\n\t\t\"max_value\":\"%.7g\"", param.value.as_float[ 2 ] );
		break;
	}
	case Type::eU32: {
		fprintf( file, "\n\t\t\"value\":\"%u\",", param.value.as_u32[ 0 ] );
		fprintf( file, "\n\t\t\"min_value\":\"%u\",", param.value.as_u32[ 1 ] );
		fprintf( file, "\n\t\t\"max_value\":\"%u\"", param.value.as_u32[ 2 ] );
		break;
	}
	case Type::eI32: {
		fprintf( file, "\n\t\t\"value\":\"%i\",", param.value.as_i32[ 0 ] );
		fprintf( file, "\n\t\t\"min_value\":\"%i\",", param.value.as_i32[ 1 ] );
		fprintf( file, "\n\t\t\"max_value\":\"%i\"", param.value.as_i32[ 2 ] );
		break;
	}
	case Type::eBool: {
		fprintf( file, "\n\t\t\"value\":\"%s\",", param.value.as_bool[ 0 ] ? "True" : "False" );
		fprintf( file, "\n\t\t\"min_value\":\"%s\",", param.value.as_bool[ 1 ] ? "True" : "False" );
		fprintf( file, "\n\t\t\"max_value\":\"%s\"", param.value.as_bool[ 2 ] ? "True" : "False" );
		break;
	}
	default:
		assert( false && "cannot serialize parameter of type eUnknown" );
		break;
	}

	fputs( "\n\t}", file );
}

// ----------------------------------------------------------------------

static inline bool le_parameter_set_value_from_string_view( le_parameter_o &tmp_param, std::string_view const &value_str, size_t value_idx ) {

	if ( value_str.empty() ) {
		return false;
	}

	switch ( tmp_param.type ) {
	case Type::eBool: {
		tmp_param.value.as_bool[ value_idx ] = ( value_str[ 0 ] == 't' || value_str[ 0 ] == 'T' );
		return true;
	}
	case Type::eFloat: {
		tmp_param.value.as_float[ value_idx ] = strtof( value_str.data(), nullptr );
		return true;
	}
	case Type::eI32: {
		tmp_param.value.as_i32[ value_idx ] = atoi( value_str.data() );
		return true;
	}
	case Type::eU32: {
		tmp_param.value.as_u32[ value_idx ] = std::abs( atol( value_str.data() ) );
		return true;
	}
	case Type::eUnknown: // fall-through
	default:
		return false;
	}
};
// ----------------------------------------------------------------------

static bool le_parameter_store_load_from_file( le_parameter_store_o *self, char const *file_path ) {

	// ----------

	static auto find_children =
	    []( std::vector<jsmntok_t>::iterator const begin,
	        std::vector<jsmntok_t>::iterator const end,
	        std::vector<jsmntok_t>::iterator const parent,
	        std::vector<int> &                     children )
	    -> bool {
		children.clear();

		int num_children = parent->size;
		int parent_idx   = parent - begin;

		int found_children = 0;

		for ( auto it = parent + 1; found_children != num_children && it != end; it++ ) {
			if ( it->parent == parent_idx ) {
				children.push_back( it - begin );
				found_children++;
			}
		}

		return !children.empty();
	};

	// ----------

	static auto get_token_string =
	    []( std::string const &                    contents,
	        std::vector<jsmntok_t>::iterator const it,
	        int                                    expected_children )
	    -> std::string_view {
		auto token_str = std::string_view( contents.data() + it->start, it->end - it->start );

		if ( it->size != expected_children ) {
			std::cout << "Warning [JSON parser]: token string must a have " << std::dec << expected_children << " children'" << token_str << "'" << std::endl;
			return {};
		}

		if ( it->type != JSMN_STRING ) {
			std::cout << "Warning [JSON parser]: token string must be of type STRING'" << token_str << "'" << std::endl;
			return {};
		}

		return token_str;
	};

	// ----------

	if ( false == std::filesystem::exists( file_path ) ) {
		std::cerr << "File '" << file_path << "' not found." << std::endl;
		return false;
	};

	// ----------

	std::ifstream file( file_path, std::ios::in | std::ios::ate );

	size_t file_sz = file.tellg();
	file.seekg( 0, std::ios::beg );

	std::string contents;
	contents.resize( file_sz );
	file.readsome( contents.data(), file_sz );
	file.close();

	// Use jsmn to parse file contents

	std::vector<jsmntok_t> tokens;
	jsmn_parser            parser{};

	jsmn_init( &parser );

	// Calling parse() with nullptr for tokens gets the number of required tokens.
	int n_elements = jsmn_parse( &parser, contents.c_str(), contents.size(), nullptr, 0 );
	if ( n_elements < 1 ) {
		std::cerr << "ERROR [json parser]: No tokens found in file: '" << file_path << "'";
		return false;
	}

	tokens.resize( n_elements );
	jsmn_init( &parser ); // We must reset parser

	n_elements = jsmn_parse( &parser, contents.c_str(), contents.size(), tokens.data(), tokens.size() );

	if ( n_elements < 1 || tokens[ 0 ].type != JSMN_OBJECT ) {
		std::cerr << "ERROR [json parser]: Expected object as first element in file: '" << file_path << "'";
		return false;
	}

	/*

    For a parameter file, we expect the following structure:

    + (1) top-level object
        + (0..n) string (parameter name)
            + object (parameter container)
               + string ("type"     ): string, one of ["U32"|"I32"|"Float"|"Bool"]            
               + string ("value"    ): string based on type, 'True|False' in case of Bool
               + string ("min_value"): string based on type, 'True|False' in case of Bool
               + string ("max_value"): string based on type, 'True|False' in case of Bool

	*/

	// Instead of depth-first, we want to go breadth-first. For this, we use the parent property
	// to make sure to iterate only over direct children of a token.

	std::vector<int> parameters;
	std::vector<int> properties;

	find_children( tokens.begin(), tokens.end(), tokens.begin(), parameters );

	// `children` now contains indices for children of the main object

	for ( auto &param_idx : parameters ) {

		std::string_view parameter_name( contents.c_str() + tokens[ param_idx ].start,
		                                 tokens[ param_idx ].end - tokens[ param_idx ].start );

		// Each parameter must have a string object with n children
		//
		// The string value gives us the name of the parameter.
		if ( tokens[ param_idx ].type != JSMN_STRING ) {
			std::cout << "Warning [JSON parser]: token must be of string type: '" << parameter_name << "'" << std::endl;
			continue;
		}

		// Parameter must have exactly one object, and it must be of type Object
		if ( tokens[ param_idx ].size != 1 ) {
			std::cout << "Warning [JSON parser]: parameter token must a hold a single object'" << parameter_name << "'" << std::endl;
			continue;
		}

		if ( tokens[ param_idx + 1 ].type != JSMN_OBJECT ) {
			std::cout << "Warning [JSON parser]: parameter container object must be of type OBJECT: '" << parameter_name << "'" << std::endl;
			continue;
		}

		// -------| Invariant: Param has an object with children:
		//                     This must be the next token.

		le_parameter_o tmp_param{};

		find_children( tokens.begin(), tokens.end(), tokens.begin() + param_idx + 1, properties );

		// We must first figure out which one of the property tokens
		// holds the "type" property.

		for ( auto const prop : properties ) {

			auto prop_str = get_token_string( contents, tokens.begin() + prop, 1 );

			if ( prop_str == "type" ) {

				auto value_str = get_token_string( contents, tokens.begin() + prop + 1, 0 );

				if ( value_str == "U32" ) {
					tmp_param.type              = Type::eU32;
					tmp_param.value.as_u32[ 0 ] = 0;
					tmp_param.value.as_u32[ 1 ] = 0;
					tmp_param.value.as_u32[ 2 ] = std::numeric_limits<uint32_t>::max();

				} else if ( value_str == "I32" ) {
					tmp_param.type              = Type::eI32;
					tmp_param.value.as_i32[ 0 ] = 0;
					tmp_param.value.as_i32[ 1 ] = std::numeric_limits<int32_t>::min();
					tmp_param.value.as_i32[ 2 ] = std::numeric_limits<int32_t>::max();
				} else if ( value_str == "Float" ) {
					tmp_param.type                = Type::eFloat;
					tmp_param.value.as_float[ 0 ] = 0.f;
					tmp_param.value.as_float[ 1 ] = 0.f;
					tmp_param.value.as_float[ 2 ] = 1.f;
				} else if ( value_str == "Bool" ) {
					tmp_param.type               = Type::eBool;
					tmp_param.value.as_bool[ 0 ] = false;
					tmp_param.value.as_bool[ 1 ] = false;
					tmp_param.value.as_bool[ 2 ] = true;
				};

				break;
			}
		}

		if ( tmp_param.type == Type::eUnknown ) {
			std::cout << "Error [JSON parser]: Parameter must specify type. Choose one of: I32|U32|Float|Bool: '" << parameter_name << "'" << std::endl;
			break;
		}

		bool success = true;
		for ( auto const &prop : properties ) {

			auto prop_str = get_token_string( contents, tokens.begin() + prop, 1 );

			if ( prop_str.empty() ) {
				continue;
			}

			// ---------| prop_str is not empty, and token has exactly one child

			if ( prop_str == "type" ) {
				continue; // We've already dealt with this property in the first pass.
			}

			// ---------| prop_str is not "type"

			auto value_str = get_token_string( contents, tokens.begin() + prop + 1, 0 );

			if ( value_str.empty() ) {
				continue;
			}

			//			std::cout << "\tproperty: '" << prop_str << "', ";
			//			std::cout << "value: '" << value_str << "'" << std::endl;

			if ( prop_str == "value" ) {
				success &= le_parameter_set_value_from_string_view( tmp_param, value_str, 0 );
				if ( false == success ) {
					std::cout << "Error [JSON parser]: unknown type: '" << prop_str << "'" << std::endl;
					break;
				}
			} else if ( prop_str == "min_value" ) {
				success &= le_parameter_set_value_from_string_view( tmp_param, value_str, 1 );
				if ( false == success ) {
					std::cout << "Error [JSON parser]: unknown type: '" << prop_str << "'" << std::endl;
					break;
				}
			} else if ( prop_str == "max_value" ) {
				success &= le_parameter_set_value_from_string_view( tmp_param, value_str, 2 );
				if ( false == success ) {
					std::cout << "Error [JSON parser]: unknown type: '" << prop_str << "'" << std::endl;
					break;
				}
			}
		}

		if ( success ) {
			auto store_param = le_parameter_store_add_parameter( self, std::string( parameter_name ).c_str() );
			std::cout << "Setting parameter: '" << parameter_name << "'" << std::endl;
			if ( store_param->type != tmp_param.type && store_param->type != Type::eUnknown ) {
				std::cout << "WARNING: Parameter store: type mismatch. Parameter type was given as: "
				          << PARAMETER_TYPE_AS_STRING[ uint( tmp_param.type ) ]
				          << ", but this does not match existing parameter type: "
				          << PARAMETER_TYPE_AS_STRING[ uint( store_param->type ) ]
				          << "." << std::endl;
				break;
			}
			*store_param = tmp_param;
		}
	}

	return true;
}

// ----------------------------------------------------------------------

static bool le_parameter_store_save_to_file( le_parameter_store_o *self, char const *file_path ) {

	auto file = fopen( file_path, "w" );

	if ( nullptr == file ) {
		assert( false && "file could not be opened for writing." );
		return false;
	}

	// ----------| invariant: file is not nullptr

	fputs( "{", file );

	auto a = self->store.cbegin();

	if ( a != self->store.cend() ) {
		print_entry( file, a->first, a->second );
		a++;
	}

	for ( ; a != self->store.cend(); a++ ) {
		fputs( ",", file );
		print_entry( file, a->first, a->second );
	}

	fputs( "\n}", file );

	fclose( file );
	return true;
}
// ----------------------------------------------------------------------

static le_parameter_store_o *le_parameter_store_create() {
	auto self = new le_parameter_store_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_parameter_store_destroy( le_parameter_store_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_parameter_store, api ) {

	// -- parameter
	auto &le_parameter_i = static_cast<le_parameter_store_api *>( api )->le_parameter_i;

	le_parameter_i.as_bool  = le_parameter_as_bool;
	le_parameter_i.as_float = le_parameter_as_float;
	le_parameter_i.as_u32   = le_parameter_as_u32;
	le_parameter_i.as_i32   = le_parameter_as_i32;

	le_parameter_i.set_bool  = le_parameter_set_bool;
	le_parameter_i.set_float = le_parameter_set_float;
	le_parameter_i.set_u32   = le_parameter_set_u32;
	le_parameter_i.set_i32   = le_parameter_set_i32;

	le_parameter_i.get_type = le_parameter_get_type;
	le_parameter_i.set_type = le_parameter_set_type;

	// -- parameter store
	auto &le_parameter_store_i = static_cast<le_parameter_store_api *>( api )->le_parameter_store_i;

	le_parameter_store_i.create  = le_parameter_store_create;
	le_parameter_store_i.destroy = le_parameter_store_destroy;

	le_parameter_store_i.add_parameter  = le_parameter_store_add_parameter;
	le_parameter_store_i.get_parameter  = le_parameter_store_get_parameter;
	le_parameter_store_i.get_name       = le_parameter_store_get_name;
	le_parameter_store_i.save_to_file   = le_parameter_store_save_to_file;
	le_parameter_store_i.load_from_file = le_parameter_store_load_from_file;
}
