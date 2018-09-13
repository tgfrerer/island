#include "simple_module.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "iostream"
#include "iomanip"

struct simple_module_o {
	uint64_t counter = 0;
};

// ----------------------------------------------------------------------

static simple_module_o *simple_module_create() {
	auto self = new simple_module_o();
	return self;
}

// ----------------------------------------------------------------------

static void simple_module_destroy( simple_module_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void simple_module_update( simple_module_o *self ) {
	static int counter = 0;
	if ( counter % 1200 == 0 ) {
		std::cout << "hello world : " << std::dec << self->counter << std::endl
		          << std::flush;
	}
	counter++;
	self->counter++;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_simple_module_api( void *api ) {
	auto &simple_module_i = static_cast<simple_module_api *>( api )->simple_module_i;

	simple_module_i.create  = simple_module_create;
	simple_module_i.destroy = simple_module_destroy;
	simple_module_i.update  = simple_module_update;
}
