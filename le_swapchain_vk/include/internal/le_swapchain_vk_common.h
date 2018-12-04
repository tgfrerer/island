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
	void *                                            data;
	uint32_t                                          referenceCount = 0;

	le_swapchain_o( const le_swapchain_vk_api::swapchain_interface_t &vtable )
	    : vtable( vtable ) {
	}

	~le_swapchain_o() = default;
};
