#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"
#include "le_renderer/private/le_renderer_resource.h"


// #include "le_backend_vk/le_backend_vk.h"

// #include <cstring>
#include <iostream>
#include <iomanip>

struct le_resource_o{
	le::ResourceInfo info;
};

static le_resource_o* resource_create(const le::ResourceInfo& info_){
	auto self = new le_resource_o();
	self->info = info_;
	return self;
}

static void resource_destroy(le_resource_o* self){
	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_renderer_resource_api( void *api_ ) {

	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_resource_i     = le_renderer_api_i->le_resource_i;

	le_resource_i.create  = resource_create;
	le_resource_i.destroy = resource_destroy;

}
