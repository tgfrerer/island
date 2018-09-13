#ifndef GUARD_simple_module_H
#define GUARD_simple_module_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct simple_module_o;

void register_simple_module_api( void *api );

// clang-format off
struct simple_module_api {
	static constexpr auto id      = "simple_module";
	static constexpr auto pRegFun = register_simple_module_api;

	struct simple_module_interface_t {

		simple_module_o * ( * create                   ) ( );
		void              ( * destroy                  ) ( simple_module_o* self );
		void              ( * update                   ) ( simple_module_o* self );

	};

	simple_module_interface_t       simple_module_i;

};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace simple_module {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<simple_module_api>( true );
#	else
const auto api = Registry::addApiStatic<simple_module_api>();
#	endif

static const auto &simple_module_i = api -> simple_module_i;

} // namespace simple_module

class SimpleModule : NoCopy, NoMove {

	simple_module_o *self;

  public:
	SimpleModule()
	    : self( simple_module::simple_module_i.create() ) {
	}

	~SimpleModule() {
		simple_module::simple_module_i.destroy( self );
	}

	void update() {
		simple_module::simple_module_i.update( self );
	}
};

#endif // #ifdef __cplusplus

#endif
