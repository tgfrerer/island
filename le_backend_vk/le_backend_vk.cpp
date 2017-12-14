#include "pal_api_loader/ApiRegistry.hpp"
#include "le_backend_vk/private/le_backend_private.h"

#include <iostream>
#include <iomanip>




// ----------------------------------------------------------------------

void register_le_backend_vk_api( void *api_ ) {
	auto  le_backend_vk_i  = static_cast<le_backend_vk_api *>( api_ );
	auto &le_instance_vk_i = le_backend_vk_i->instance_i;
	auto &le_device_vk_i   = le_backend_vk_i->device_i;

	le_instance_vk_i.create           = instance_create;
	le_instance_vk_i.destroy          = instance_destroy;
	le_instance_vk_i.post_reload_hook = post_reload_hook;
	le_instance_vk_i.get_vk_instance  = instance_get_vk_instance;

	le_device_vk_i.create                                  = device_create;
	le_device_vk_i.destroy                                 = device_destroy;
	le_device_vk_i.get_vk_device                           = device_get_vk_device;
	le_device_vk_i.get_vk_physical_device                  = device_get_vk_physical_device;
	le_device_vk_i.get_default_compute_queue               = device_get_default_compute_queue;
	le_device_vk_i.get_default_compute_queue_family_index  = device_get_default_compute_queue_family_index;
	le_device_vk_i.get_default_graphics_queue              = device_get_default_graphics_queue;
	le_device_vk_i.get_default_graphics_queue_family_index = device_get_default_graphics_queue_family_index;

	if ( le_backend_vk_i->cUniqueInstance != nullptr ) {
		le_instance_vk_i.post_reload_hook( le_backend_vk_i->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
