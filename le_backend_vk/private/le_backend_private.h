#ifndef GUARD_PAL_BACKEND_VK_PRIVATE_H
#define GUARD_PAL_BACKEND_VK_PRIVATE_H

/*

  This header is shared amongst all implementation unit of the Backend.

  This is where we declare shared cpp objects, as they will be hidden from the outside world.


*/

#include "vulkan/vulkan.hpp"
#include "le_backend_vk/le_backend_vk.h"
#include <vector>

struct le_backend_vk_instance_o {
	vk::Instance               vkInstance = nullptr;
	vk::DebugReportCallbackEXT debugCallback;
};

extern le_backend_vk_instance_o *instance_create( const le_backend_vk_api *, const char **, uint32_t ); // defined in le_instance_vk.cpp
extern void                      instance_destroy( le_backend_vk_instance_o * );                        // defined in le_instance_vk.cpp
extern VkInstance                instance_get_vk_instance( le_backend_vk_instance_o * );                // defined in le_instance_vk.cpp
extern void                      post_reload_hook( le_backend_vk_instance_o * );                        // defined in le_instance_vk.cpp

struct le_backend_vk_device_o {

	vk::Device                         vkDevice         = nullptr;
	vk::PhysicalDevice                 vkPhysicalDevice = nullptr;
	vk::PhysicalDeviceProperties       vkPhysicalDeviceProperties;
	vk::PhysicalDeviceMemoryProperties vkPhysicalDeviceMemoryProperties;

	std::vector<vk::Queue>      queues;
	std::vector<vk::QueueFlags> queueCapabilities{vk::QueueFlagBits::eGraphics, vk::QueueFlagBits::eCompute};
	std::vector<uint32_t>       queueFamilyIndices;
};

extern le_backend_vk_device_o *device_create( le_backend_vk_instance_o *instance_ );
extern void                    device_destroy( le_backend_vk_device_o *self_ );
extern VkDevice                device_get_vk_device( le_backend_vk_device_o *self_ );
extern VkPhysicalDevice        device_get_vk_physical_device( le_backend_vk_device_o *self_ );

extern PFN_vkCreateDebugReportCallbackEXT  pfn_vkCreateDebugReportCallbackEXT;
extern PFN_vkDestroyDebugReportCallbackEXT pfn_vkDestroyDebugReportCallbackEXT;
extern PFN_vkDebugReportMessageEXT         pfn_vkDebugReportMessageEXT;

#endif // GUARD_PAL_BACKEND_VK_PRIVATE_H
