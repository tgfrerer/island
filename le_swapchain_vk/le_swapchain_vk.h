#ifndef GUARD_PAL_BACKEND_VK_H
#define GUARD_PAL_BACKEND_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_swapchain_api( void *api );

struct le_backend_swapchain_o;
struct le_backend_swapchain_vk_settings_o;

struct le_swapchain_api {
	static constexpr auto id       = "le_backend_vk";
	static constexpr auto pRegFun  = register_le_swapchain_api;

	struct swapchain_settings_vk_interface_t {

	};

	struct swapchain_interface_t {
		le_backend_swapchain_o * ( *create           ) ( le_backend_swapchain_vk_settings_o* settings_, le_backend_swapchain_o* old_swapchain );
		void                     ( *destroy          ) ( le_backend_swapchain_o* self_ );
	};

	swapchain_settings_vk_interface_t settings_vk_i;
	swapchain_interface_t swapchain_i;

};

#ifdef __cplusplus
} // extern "C"

namespace le {

class Swapchain {

};

} // namespace pal
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
