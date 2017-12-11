#ifndef GUARD_PAL_BACKEND_VK_PRIVATE_H
#define GUARD_PAL_BACKEND_VK_PRIVATE_H

#include "vulkan/vulkan.hpp"
#include "pal_backend_vk/pal_backend_vk.h"

struct pal_backend_vk_instance_o {
	vk::Instance               vkInstance = nullptr;
	vk::DebugReportCallbackEXT debugCallback;
};

extern pal_backend_vk_instance_o *instance_create( pal_backend_vk_api * );         // defined in instance_vk.cpp
extern void                       instance_destroy( pal_backend_vk_instance_o * ); // defined in instance_vk.cpp
extern VkInstance_T*              instance_get_VkInstance( pal_backend_vk_instance_o * ); // defined in instance_vk.cpp
extern void                       post_reload_hook( pal_backend_vk_instance_o * ); // defined in instance_vk.cpp

#endif // GUARD_PAL_BACKEND_VK_PRIVATE_H
