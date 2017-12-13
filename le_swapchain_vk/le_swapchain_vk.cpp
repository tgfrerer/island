#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>

struct le_backend_swapchain_vk_settings_o {
	uint32_t                         width           = 0;
	uint32_t                         height          = 0;
	uint32_t                         imagecount      = 0;
	uint32_t                         imagecountHint  = 3;
	le_swapchain_vk_api::Presentmode presentmodeHint = le_swapchain_vk_api::Presentmode::eDefault;
	vk::PresentModeKHR               presentMode     = vk::PresentModeKHR::eFifo;
};

// ----------------------------------------------------------------------

struct le_backend_swapchain_o {
	le_backend_swapchain_vk_settings_o mSettings;
};

// ----------------------------------------------------------------------

static le_backend_swapchain_vk_settings_o *swapchain_settings_create() {
	auto self = new ( le_backend_swapchain_vk_settings_o );
	return self;
}

// ----------------------------------------------------------------------

static void swapchain_settings_destroy( le_backend_swapchain_vk_settings_o *self_ ) {
	delete ( self_ );
	self_ = nullptr;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_presentmode_hint( le_backend_swapchain_vk_settings_o *self_, const le_swapchain_vk_api::Presentmode &mode_ ) {
	self_->presentmodeHint = mode_;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_width( le_backend_swapchain_vk_settings_o *self_, uint32_t width_ ) {
	self_->width = width_;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_imagecount_hint( le_backend_swapchain_vk_settings_o *self_, uint32_t imagecount_hint_ ) {
	self_->imagecountHint = imagecount_hint_;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_height( le_backend_swapchain_vk_settings_o *self_, uint32_t height_ ) {
	self_->width = height_;
}

// ----------------------------------------------------------------------

static le_backend_swapchain_o *swapchain_create( const le_backend_swapchain_vk_settings_o *settings_, le_backend_swapchain_o *old_swapchain ) {
	auto self = new ( le_backend_swapchain_o );
	self->mSettings = *settings_;




	return self;
}

// ----------------------------------------------------------------------

static void swapchain_destroy( le_backend_swapchain_o *self_ ) {
	delete ( self_ );
	self_ = nullptr;
}

// ----------------------------------------------------------------------

void register_le_swapchain_vk_api( void *api_ ) {
	auto  api           = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i   = api->swapchain_i;
	auto &settings_vk_i = api->settings_vk_i;

	swapchain_i.create  = swapchain_create;
	swapchain_i.destroy = swapchain_destroy;

	settings_vk_i.create               = swapchain_settings_create;
	settings_vk_i.destroy              = swapchain_settings_destroy;
	settings_vk_i.set_width            = swapchain_settings_set_width;
	settings_vk_i.set_height           = swapchain_settings_set_height;
	settings_vk_i.set_presentmode_hint = swapchain_settings_set_presentmode_hint;
	settings_vk_i.set_image_count_hint = swapchain_settings_set_imagecount_hint;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
