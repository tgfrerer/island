#include "le_parameter_store.h"
#include "le_core/le_core.h"
#include <unordered_map>
#include <string>

// Implement load from file: jsmn
// Implement store to json -- we write the serialiser ourselves. we just need to make sure it generates valid json.

using Type = le_parameter_store_api::Type;

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

static float *le_parameter_set_float( le_parameter_o *self, float val ) {
	if ( nullptr == self ) {
		return nullptr;
	}
	self->type                = Type::eFloat;
	self->value.as_float[ 0 ] = val;
	self->value.as_float[ 1 ] = 0.0;
	self->value.as_float[ 2 ] = 1.f;
	return &self->value.as_float[ 0 ];
}
static uint32_t *le_parameter_set_u32( le_parameter_o *self, uint32_t val ) {
	if ( nullptr == self ) {
		return nullptr;
	}
	self->type              = Type::eU32;
	self->value.as_u32[ 0 ] = val;
	self->value.as_u32[ 1 ] = 0;
	self->value.as_u32[ 2 ] = UINT32_MAX;
	return &self->value.as_u32[ 0 ];
}
static int32_t *le_parameter_set_i32( le_parameter_o *self, int32_t val ) {
	if ( nullptr == self ) {
		return nullptr;
	}
	self->type              = Type::eI32;
	self->value.as_i32[ 0 ] = val;
	self->value.as_i32[ 1 ] = INT32_MIN;
	self->value.as_i32[ 2 ] = INT32_MAX;
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

	le_parameter_store_i.add_parameter = le_parameter_store_add_parameter;
	le_parameter_store_i.get_parameter = le_parameter_store_get_parameter;
	le_parameter_store_i.get_name      = le_parameter_store_get_name;
}
