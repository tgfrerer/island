#include "pal_backend_vk/pal_backend_vk.h"
#include "pal_api_loader/ApiRegistry.hpp"
#include <iostream>
#include <iomanip>

struct pal_backend_o;

extern pal_backend_o *create( pal_backend_vk_api * );      // defined in instance_vk.cpp
extern void           destroy( pal_backend_o * );          // defined in instance_vk.cpp
extern void           post_reload_hook( pal_backend_o * ); // defined in instance_vk.cpp

// ----------------------------------------------------------------------

void register_pal_backend_vk_api( void *api_ ) {
	auto pal_backend_vk              = static_cast<pal_backend_vk_api *>( api_ );
	pal_backend_vk->create           = create;
	pal_backend_vk->destroy          = destroy;
	pal_backend_vk->post_reload_hook = post_reload_hook;

	if ( pal_backend_vk->cBackend != nullptr ) {
		pal_backend_vk->post_reload_hook( pal_backend_vk->cBackend );
	}

	Registry::loadLibraryPersistent( "libvulkan.so" );
}
