#include "pal_api_loader/ApiRegistry.hpp"
#include "le_backend_vk/private/le_backend_private.h"

#include <iostream>
#include <iomanip>

// ----------------------------------------------------------------------

void register_le_backend_vk_api( void *api_ ) {
	auto  le_backend_vk          = static_cast<le_backend_vk_api *>( api_ );
	auto &le_backend_instance_vk = le_backend_vk->instance_i;

	le_backend_instance_vk.create           = instance_create;
	le_backend_instance_vk.destroy          = instance_destroy;
	le_backend_instance_vk.post_reload_hook = post_reload_hook;
	le_backend_instance_vk.get_VkInstance   = instance_get_VkInstance;

	if ( le_backend_vk->cUniqueInstance != nullptr ) {
		le_backend_instance_vk.post_reload_hook( le_backend_vk->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
