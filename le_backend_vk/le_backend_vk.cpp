#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h"
#include "le_backend_vk/private/le_device_vk.h"
#include "le_backend_vk/private/le_instance_vk.h"

// ----------------------------------------------------------------------

API_REGISTRY_ENTRY void register_le_backend_vk_api( void *api_ ) {

	register_le_device_vk_api(api_);
	register_le_instance_vk_api(api_);

	auto  le_backend_vk_i  = static_cast<le_backend_vk_api *>( api_ );
	auto &le_instance_vk_i = le_backend_vk_i->instance_i;

	if ( le_backend_vk_i->cUniqueInstance != nullptr ) {
		le_instance_vk_i.post_reload_hook( le_backend_vk_i->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
