#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>

struct le_backend_swapchain_vk_settings_o {
	uint32_t           width       = 0;
	uint32_t           height      = 0;
	uint32_t           imageCount  = 0;
	vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
};

struct le_backend_swapchain_o {
	le_backend_swapchain_vk_settings_o *mSettings = nullptr;
};

static le_backend_swapchain_o *swapchain_create( le_backend_swapchain_vk_settings_o *settings_, le_backend_swapchain_o *old_swapchain ) {
	le_backend_swapchain_o *self = new ( le_backend_swapchain_o );

	return self;
}

static void swapchain_destroy( le_backend_swapchain_o *self_ ) {
	delete ( self_ );
	self_ = nullptr;
}

// ----------------------------------------------------------------------

void register_le_swapchain_api( void *api_ ) {
	auto  api         = static_cast<le_swapchain_api *>( api_ );
	auto &swapchain_i = api->swapchain_i;
	auto &settings_vk_i = api->settings_vk_i;

	swapchain_i.create  = swapchain_create;
	swapchain_i.destroy = swapchain_destroy;



	Registry::loadLibraryPersistently( "libvulkan.so" );
}
