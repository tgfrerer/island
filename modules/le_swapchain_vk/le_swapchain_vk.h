#ifndef GUARD_LE_SWAPCHAIN_VK_H
#define GUARD_LE_SWAPCHAIN_VK_H

#include <stdint.h>
#include "le_core.h"

struct le_swapchain_o;
struct le_backend_o;

struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkSurfaceKHR_T;
struct VkSemaphore_T;
struct VkImage_T;
struct VkImageView_T;
struct VkQueue_T;
struct VkSurfaceFormatKHR;
struct le_swapchain_settings_t;

struct le_swapchain_vk_api {

	// clang-format off
	struct swapchain_interface_t {
		le_swapchain_o *          ( *create                   ) ( le_swapchain_vk_api::swapchain_interface_t const & interface, le_backend_o* backend, const le_swapchain_settings_t* settings );
		void                      ( *destroy                  ) ( le_swapchain_o* self );
		void                      ( *reset                    ) ( le_swapchain_o* self, const le_swapchain_settings_t* settings );
		bool                      ( *present                  ) ( le_swapchain_o* self, VkQueue_T* queue, VkSemaphore_T* renderCompleteSemaphore, uint32_t* pImageIndex);
		bool                      ( *acquire_next_image       ) ( le_swapchain_o* self, VkSemaphore_T* semaphore_, uint32_t& imageIndex_ );
		VkSurfaceFormatKHR*       ( *get_surface_format       ) ( le_swapchain_o* self );
		VkImage_T*                ( *get_image                ) ( le_swapchain_o* self, uint32_t index_);
		uint32_t                  ( *get_image_width          ) ( le_swapchain_o* self );
		uint32_t                  ( *get_image_height         ) ( le_swapchain_o* self );
		size_t                    ( *get_images_count         ) ( le_swapchain_o* self );
		
		void                      ( *get_required_vk_instance_extensions )(const le_swapchain_settings_t* settings, char const *** exts, size_t * num_exts);
		void                      ( *get_required_vk_device_extensions )(const le_swapchain_settings_t* settings, char const *** exts, size_t * num_exts);
	};

	// clang-format on

	swapchain_interface_t swapchain_i;        // base (public) interface, forwards to either:
	swapchain_interface_t swapchain_khr_i;    // (private) khr swapchain interface
	swapchain_interface_t swapchain_img_i;    // (private) image swapchain interface
	swapchain_interface_t swapchain_direct_i; // (private) direct mode swapchain interface
};

LE_MODULE( le_swapchain_vk );
LE_MODULE_LOAD_DEFAULT( le_swapchain_vk );

#ifdef __cplusplus

namespace le_swapchain_vk {

static const auto &api         = le_swapchain_vk_api_i;
static const auto &swapchain_i = api -> swapchain_i;

} // namespace le_swapchain_vk

#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
