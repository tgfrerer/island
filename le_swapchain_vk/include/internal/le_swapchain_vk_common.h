#include "le_swapchain_vk/le_swapchain_vk.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>
#include <assert.h>

// ----------------------------------------------------------------------

struct SurfaceProperties {
	vk::SurfaceFormatKHR                windowSurfaceFormat;
	vk::SurfaceCapabilitiesKHR          surfaceCapabilities;
	VkBool32                            presentSupported = VK_FALSE;
	std::vector<::vk::PresentModeKHR>   presentmodes;
	std::vector<::vk::SurfaceFormatKHR> availableSurfaceFormats;
};

struct le_swapchain_o {
	const le_swapchain_vk_api::swapchain_interface_t &vtable;
	le_swapchain_vk_settings_t                        mSettings;
	le_backend_o *                                    backend;
	uint32_t                                          mImagecount      = 0;
	uint32_t                                          mImageIndex      = uint32_t( ~0 ); // current image index
	vk::SwapchainKHR                                  swapchainKHR     = nullptr;
	vk::Extent2D                                      mSwapchainExtent = {};
	vk::PresentModeKHR                                mPresentMode     = vk::PresentModeKHR::eFifo;
	uint32_t                                          referenceCount   = 0;
	SurfaceProperties                                 mSurfaceProperties;
	std::vector<vk::Image>                            mImageRefs; // owned by SwapchainKHR, don't delete
	vk::Device                                        device                         = nullptr;
	vk::PhysicalDevice                                physicalDevice                 = nullptr;
	uint32_t                                          vk_graphics_queue_family_index = 0;

	le_swapchain_o( const le_swapchain_vk_api::swapchain_interface_t &vtable )
	    : vtable( vtable ) {
	}

	~le_swapchain_o() = default;
};
