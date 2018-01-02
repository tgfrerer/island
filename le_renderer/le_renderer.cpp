#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"

#include <iostream>
#include <iomanip>

struct le_renderer_o{
//	le::Device mBackend;
};


static le_renderer_o* renderer_create(){
	auto obj = new le_renderer_o();
	return obj;
}

static void renderer_destroy(le_renderer_o* self){
	delete self;
}

// ----------------------------------------------------------------------

void register_le_renderer_api( void *api_ ) {
	auto  le_renderer_api_i  = static_cast<le_renderer_api *>( api_ );
	auto &le_renderer_i = le_renderer_api_i->le_renderer_i;


	le_renderer_i.create           = renderer_create;
	le_renderer_i.destroy          = renderer_destroy;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
