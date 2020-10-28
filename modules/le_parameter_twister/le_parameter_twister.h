#ifndef GUARD_le_parameter_twister_H
#define GUARD_le_parameter_twister_H

#include "le_core/le_core.h"

struct le_parameter_twister_o;
struct le_parameter_o;

// clang-format off
struct le_parameter_twister_api {

	struct le_parameter_twister_interface_t {

		le_parameter_twister_o *    ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_parameter_twister_o* self );
		void                 ( * update                   ) ( le_parameter_twister_o* self );

        void (*add_parameter)(le_parameter_twister_o* self, le_parameter_o* param, uint8_t encoder_id);

	};

	le_parameter_twister_interface_t       le_parameter_twister_i;
};
// clang-format on

LE_MODULE( le_parameter_twister );
LE_MODULE_LOAD_DEFAULT( le_parameter_twister );

#ifdef __cplusplus

namespace le_parameter_twister {
static const auto &api                    = le_parameter_twister_api_i;
static const auto &le_parameter_twister_i = api -> le_parameter_twister_i;
} // namespace le_parameter_twister

class LeParameterTwister : NoCopy, NoMove {

	le_parameter_twister_o *self;

  public:
	LeParameterTwister()
	    : self( le_parameter_twister::le_parameter_twister_i.create() ) {
	}

	~LeParameterTwister() {
		le_parameter_twister::le_parameter_twister_i.destroy( self );
	}

	void update() {
		le_parameter_twister::le_parameter_twister_i.update( self );
	}

	void add_parameter( le_parameter_o *param, uint8_t encoder_id ) {
		le_parameter_twister::le_parameter_twister_i.add_parameter( self, param, encoder_id );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif
