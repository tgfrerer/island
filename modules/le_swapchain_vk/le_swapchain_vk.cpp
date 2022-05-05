#include "./include/internal/le_swapchain_vk_common.h" // defines struct le_swapchain_o
#include "private/le_renderer_types.h"                 // for swapchain_settings
#include "assert.h"
// ----------------------------------------------------------------------

static void swapchain_reset( le_swapchain_o* self, const le_swapchain_settings_t* settings ) {
	self->vtable.reset( self, settings );
}

// ----------------------------------------------------------------------

static le_swapchain_o* swapchain_create( le_swapchain_vk_api::swapchain_interface_t const& interface, le_backend_o* backend, const le_swapchain_settings_t* settings ) {
	return interface.create( interface, backend, settings );
}

// ----------------------------------------------------------------------

static bool swapchain_acquire_next_image( le_swapchain_o* self, VkSemaphore_T* semaphorePresentComplete_, uint32_t& imageIndex_ ) {
	return self->vtable.acquire_next_image( self, semaphorePresentComplete_, imageIndex_ );
}

// ----------------------------------------------------------------------

static VkImage_T* swapchain_get_image( le_swapchain_o* self, uint32_t index ) {
	return self->vtable.get_image( self, index );
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR* swapchain_get_surface_format( le_swapchain_o* self ) {
	return self->vtable.get_surface_format( self );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_width( le_swapchain_o* self ) {
	return self->vtable.get_image_width( self );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_height( le_swapchain_o* self ) {
	return self->vtable.get_image_height( self );
}

// ----------------------------------------------------------------------

static size_t swapchain_get_swapchain_images_count( le_swapchain_o* self ) {
	return self->vtable.get_images_count( self );
}

// ----------------------------------------------------------------------

static void swapchain_destroy( le_swapchain_o* self ) {
	self->vtable.destroy( self );
}

// ----------------------------------------------------------------------

static bool swapchain_present( le_swapchain_o* self, VkQueue_T* queue, VkSemaphore_T* renderCompleteSemaphore, uint32_t* pImageIndex ) {
	return self->vtable.present( self, queue, renderCompleteSemaphore, pImageIndex );
};

// ----------------------------------------------------------------------

static inline le_swapchain_vk_api::swapchain_interface_t const* fetch_interface( le_swapchain_settings_t::Type const& type ) {
	le_swapchain_vk_api::swapchain_interface_t const* interface = nullptr;

	switch ( type ) {
	case le_swapchain_settings_t::LE_KHR_SWAPCHAIN:
		interface = &le_swapchain_vk::api->swapchain_khr_i;
		return interface;
	case le_swapchain_settings_t::LE_DIRECT_SWAPCHAIN:
		interface = &le_swapchain_vk::api->swapchain_direct_i;
		return interface;
	case le_swapchain_settings_t::LE_IMG_SWAPCHAIN:
		interface = &le_swapchain_vk::api->swapchain_img_i;
		return interface;
	}

	assert( false ); // unreachable
	return nullptr;
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_instance_extensions( const le_swapchain_settings_t* settings, char const*** exts, size_t* num_exts ) {
	auto interface = fetch_interface( settings->type );
	interface->get_required_vk_instance_extensions( settings, exts, num_exts );
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_device_extensions( const le_swapchain_settings_t* settings, char const*** exts, size_t* num_exts ) {
	auto interface = fetch_interface( settings->type );
	interface->get_required_vk_device_extensions( settings, exts, num_exts );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_swapchain_vk, api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api*>( api_ );
	auto& swapchain_i = api->swapchain_i;

	swapchain_i.create                              = swapchain_create;
	swapchain_i.destroy                             = swapchain_destroy;
	swapchain_i.reset                               = swapchain_reset;
	swapchain_i.acquire_next_image                  = swapchain_acquire_next_image;
	swapchain_i.get_image                           = swapchain_get_image;
	swapchain_i.get_image_width                     = swapchain_get_image_width;
	swapchain_i.get_image_height                    = swapchain_get_image_height;
	swapchain_i.get_surface_format                  = swapchain_get_surface_format;
	swapchain_i.get_images_count                    = swapchain_get_swapchain_images_count;
	swapchain_i.present                             = swapchain_present;
	swapchain_i.get_required_vk_instance_extensions = swapchain_get_required_vk_instance_extensions;
	swapchain_i.get_required_vk_device_extensions   = swapchain_get_required_vk_device_extensions;

	register_le_swapchain_khr_api( api );
	register_le_swapchain_img_api( api );
	register_le_swapchain_direct_api( api );

#ifdef PLUGINS_DYNAMIC
	le_core_load_library_persistently( "libvulkan.so" );
	le_core_load_library_persistently( "libX11.so" );
#endif
}
