#include "pal_backend_vk/private/backend_private.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include <iostream>
#include <iomanip>

// ----------------------------------------------------------------------

void register_pal_backend_vk_api( void *api_ ) {
	auto  pal_backend_vk          = static_cast<pal_backend_vk_api *>( api_ );
	auto &pal_backend_instance_vk = pal_backend_vk->instance_i;

	pal_backend_instance_vk.create           = instance_create;
	pal_backend_instance_vk.destroy          = instance_destroy;
	pal_backend_instance_vk.post_reload_hook = post_reload_hook;
	pal_backend_instance_vk.get_VkInstance   = instance_get_VkInstance;

	if ( pal_backend_vk->cUniqueInstance != nullptr ) {
		pal_backend_instance_vk.post_reload_hook( pal_backend_vk->cUniqueInstance );
	}

	Registry::loadLibraryPersistent( "libvulkan.so" );
}
