#ifndef LE_SWAPCHAIN_VK_COMMON_GUARD
#define LE_SWAPCHAIN_VK_COMMON_GUARD

#include "le_swapchain_vk/le_swapchain_vk.h"

void register_le_swapchain_khr_api( void *api );    // in le_swapchain_khr.cpp
void register_le_swapchain_img_api( void *api );    // in le_swapchain_img.cpp
void register_le_swapchain_direct_api( void *api ); // in le_swapchain_direct.cpp

// ----------------------------------------------------------------------

struct le_swapchain_o {
	const le_swapchain_vk_api::swapchain_interface_t &vtable;
	void *                                            data           = nullptr;
	uint32_t                                          referenceCount = 0;

	le_swapchain_o( const le_swapchain_vk_api::swapchain_interface_t &vtable )
	    : vtable( vtable ) {
	}

	~le_swapchain_o() = default;
};
#endif