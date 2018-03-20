#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_resource.h"
#include "le_renderer/private/le_renderer_types.h"

#include <iostream>
#include <iomanip>

struct le_resource_o{
	le::ResourceCreateInfo info;
};

static le_resource_o* resource_create(const le::ResourceCreateInfo& info_){
	auto self = new le_resource_o();
	self->info = info_;
	return self;
}

static void resource_destroy(le_resource_o* self){
	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_resource_api( void *api_ ) {

	auto  &api_i = (static_cast<le_renderer_api *>( api_ ))->le_resource_i;

	api_i.create  = resource_create;
	api_i.destroy = resource_destroy;

}
