#include "le_swapchain_vk/le_swapchain_vk.h"

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
