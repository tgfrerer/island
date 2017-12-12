#ifndef GUARD_PAL_BACKEND_VK_PRIVATE_H
#define GUARD_PAL_BACKEND_VK_PRIVATE_H

#include "vulkan/vulkan.hpp"
#include "le_backend_vk/le_backend_vk.h"

struct le_backend_vk_instance_o {
	vk::Instance               vkInstance = nullptr;
	vk::DebugReportCallbackEXT debugCallback;
};

extern le_backend_vk_instance_o *instance_create( le_backend_vk_api *, const char **, uint32_t ); // defined in instance_vk.cpp
extern void                      instance_destroy( le_backend_vk_instance_o * );                  // defined in instance_vk.cpp
extern VkInstance_T *            instance_get_VkInstance( le_backend_vk_instance_o * );           // defined in instance_vk.cpp
extern void                      post_reload_hook( le_backend_vk_instance_o * );                  // defined in instance_vk.cpp

extern PFN_vkCreateDebugReportCallbackEXT  pfn_vkCreateDebugReportCallbackEXT;
extern PFN_vkDestroyDebugReportCallbackEXT pfn_vkDestroyDebugReportCallbackEXT;
extern PFN_vkDebugReportMessageEXT         pfn_vkDebugReportMessageEXT;

#endif // GUARD_PAL_BACKEND_VK_PRIVATE_H
