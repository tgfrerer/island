#include "./include/internal/le_swapchain_vk_common.h" // defines struct le_swapchain_o

// ----------------------------------------------------------------------

static void swapchain_reset( le_swapchain_o *self, const le_swapchain_settings_t *settings ) {
	self->vtable.reset( self, settings );
}

// ----------------------------------------------------------------------

static le_swapchain_o *swapchain_create( le_swapchain_vk_api::swapchain_interface_t const &interface, le_backend_o *backend, const le_swapchain_settings_t *settings_ ) {
	return interface.create( interface, backend, settings_ );
}

// ----------------------------------------------------------------------

static bool swapchain_acquire_next_image( le_swapchain_o *self, VkSemaphore_T *semaphorePresentComplete_, uint32_t &imageIndex_ ) {
	return self->vtable.acquire_next_image( self, semaphorePresentComplete_, imageIndex_ );
}

// ----------------------------------------------------------------------

static VkImage_T *swapchain_get_image( le_swapchain_o *self, uint32_t index ) {
	return self->vtable.get_image( self, index );
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR *swapchain_get_surface_format( le_swapchain_o *self ) {
	return self->vtable.get_surface_format( self );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_width( le_swapchain_o *self ) {
	return self->vtable.get_image_width( self );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_height( le_swapchain_o *self ) {
	return self->vtable.get_image_height( self );
}

// ----------------------------------------------------------------------

static size_t swapchain_get_swapchain_images_count( le_swapchain_o *self ) {
	return self->vtable.get_images_count( self );
}

// ----------------------------------------------------------------------

static void swapchain_destroy( le_swapchain_o *self ) {
	self->vtable.destroy( self );
}

// ----------------------------------------------------------------------

static bool swapchain_present( le_swapchain_o *self, VkQueue_T *queue, VkSemaphore_T *renderCompleteSemaphore, uint32_t *pImageIndex ) {
	return self->vtable.present( self, queue, renderCompleteSemaphore, pImageIndex );
};

// ----------------------------------------------------------------------

void register_le_swapchain_vk_api( void *api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i = api->swapchain_i;

	swapchain_i.create             = swapchain_create;
	swapchain_i.destroy            = swapchain_destroy;
	swapchain_i.reset              = swapchain_reset;
	swapchain_i.acquire_next_image = swapchain_acquire_next_image;
	swapchain_i.get_image          = swapchain_get_image;
	swapchain_i.get_image_width    = swapchain_get_image_width;
	swapchain_i.get_image_height   = swapchain_get_image_height;
	swapchain_i.get_surface_format = swapchain_get_surface_format;
	swapchain_i.get_images_count   = swapchain_get_swapchain_images_count;
	swapchain_i.present            = swapchain_present;

	register_le_swapchain_khr_api( api );
	register_le_swapchain_img_api( api );

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
