#ifndef GUARD_le_parameter_store_H
#define GUARD_le_parameter_store_H

#include "le_core/le_core.h"

struct le_parameter_store_o;
struct le_parameter_o;

/*

Parameters are heap-allocated bits of data which have a fixed address for the duration of the
lifetime of parameter_store.

Because of this, pointers handed out to parameters can be kept aound for as long as the parameter
store object is alive.

Each parameter is stored as an array<paramterType, 3>.

Element [0] is the actual parameter value
Element [1] is the parameter min value
Element [2] is the parameter max value

*/

// clang-format off
struct le_parameter_store_api {

	enum Type {
		eUnknown,
		eFloat,
		eU32,
		eI32,
		eBool,
	};

    struct le_parameter_interface_t {
    
        Type (*get_type)(le_parameter_o* parameter);
        void (*set_type)(le_parameter_o* parameter, Type type);
        
        float*    (*set_float)( le_parameter_o *self, float val    );
        uint32_t* (*set_u32  )( le_parameter_o *self, uint32_t val );
        int32_t*  (*set_i32  )( le_parameter_o *self, int32_t val  );
        bool*     (*set_bool )( le_parameter_o *self, bool val     );

        float*    (*as_float)( le_parameter_o *self); // may return nullptr if wrong type 
        uint32_t* (*as_u32  )( le_parameter_o *self); // may return nullptr if wrong type 
        int32_t*  (*as_i32  )( le_parameter_o *self); // may return nullptr if wrong type 
        bool*     (*as_bool )( le_parameter_o *self); // may return nullptr if wrong type 
        
    };

	struct le_parameter_store_interface_t {

		le_parameter_store_o * ( * create  ) ( );
		void                   ( * destroy ) ( le_parameter_store_o* self );

        le_parameter_o* (*get_parameter)(le_parameter_store_o* self, char const* name); // may return nullptr, if parameter not found.
        le_parameter_o* (*add_parameter)(le_parameter_store_o* self, char const* name);

        char const *    (*get_name)(le_parameter_store_o* self, le_parameter_o* param); // may return nullptr if not found

        bool (*save_to_file)(le_parameter_store_o* self, char const * file_path);

	};

	le_parameter_interface_t       le_parameter_i;
	le_parameter_store_interface_t le_parameter_store_i;
};
// clang-format on

LE_MODULE( le_parameter_store );
LE_MODULE_LOAD_DEFAULT( le_parameter_store );

#ifdef __cplusplus

namespace le_parameter_store {
static const auto &api                  = le_parameter_store_api_i;
static const auto &le_parameter_store_i = api -> le_parameter_store_i;
static const auto &le_parameter_i       = api -> le_parameter_i;
} // namespace le_parameter_store

class LeParameter {
	le_parameter_o *self;

  public:
	LeParameter( le_parameter_o *param_ )
	    : self( param_ ){};
	LeParameter()  = delete;
	~LeParameter() = default;
	// There we go: rule of 5
	LeParameter( LeParameter const &other )
	    : self( other.self ){};
	LeParameter( LeParameter &&other )
	    : self( other.self ) {
	}
	LeParameter &operator=( LeParameter const &other ) {
		self = other.self;
		return *this;
	}
	LeParameter &operator=( LeParameter const &&other ) {
		self = other.self;
		return *this;
	}

	float *setFloat( float const &&val ) {
		return le_parameter_store::le_parameter_i.set_float( self, static_cast<float const &&>( val ) );
	}
	int32_t *setI32( int32_t const &&val ) {
		return le_parameter_store::le_parameter_i.set_i32( self, static_cast<int32_t const &&>( val ) );
	}
	uint32_t *setU32( uint32_t const &&val ) {
		return le_parameter_store::le_parameter_i.set_u32( self, static_cast<uint32_t const &&>( val ) );
	}
	bool *setBool( bool const &&val ) {
		return le_parameter_store::le_parameter_i.set_bool( self, static_cast<bool const &&>( val ) );
	}
	// ----
	float *asFloat() {
		return le_parameter_store::le_parameter_i.as_float( self );
	}
	int32_t *asI32() {
		return le_parameter_store::le_parameter_i.as_i32( self );
	}
	uint32_t *asU32() {
		return le_parameter_store::le_parameter_i.as_u32( self );
	}
	bool *asBool() {
		return le_parameter_store::le_parameter_i.as_bool( self );
	}

	le_parameter_store_api::Type getType() {
		return le_parameter_store::le_parameter_i.get_type( self );
	}

	void setType( le_parameter_store_api::Type type ) {
		le_parameter_store::le_parameter_i.set_type( self, type );
	}

	operator auto() const {
		return self;
	}
};

class LeParameterStore : NoCopy, NoMove {

	le_parameter_store_o *self;

  public:
	LeParameterStore()
	    : self( le_parameter_store::le_parameter_store_i.create() ) {
	}

	~LeParameterStore() {
		le_parameter_store::le_parameter_store_i.destroy( self );
	}

	le_parameter_o *getParameter( char const *name ) {
		return le_parameter_store::le_parameter_store_i.get_parameter( self, name );
	}

	le_parameter_o *addParameter( char const *name ) {
		return le_parameter_store::le_parameter_store_i.add_parameter( self, name );
	}

	char const *getName( le_parameter_o *param ) {
		return le_parameter_store::le_parameter_store_i.get_name( self, param );
	}

	bool saveToFile( char const *file_path ) {
		return le_parameter_store::le_parameter_store_i.save_to_file( self, file_path );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
