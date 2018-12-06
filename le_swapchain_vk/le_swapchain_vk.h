#ifndef GUARD_LE_SWAPCHAIN_VK_H
#define GUARD_LE_SWAPCHAIN_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_swapchain_vk_api( void *api );  // in le_swapchain_vk.cpp
void register_le_swapchain_khr_api( void *api ); // in le_swapchain_khr.cpp
void register_le_swapchain_img_api( void *api ); // in le_swapchain_img.cpp

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

struct le_swapchain_vk_settings_t {
	enum class Presentmode : uint32_t {
		eDefault = 0,
		eImmediate,
		eMailbox,
		eFifo,
		eFifoRelaxed,
		eSharedDemandRefresh,
		eSharedContinuousRefresh,
	};
	uint32_t        width_hint       = 640;
	uint32_t        height_hint      = 480;
	uint32_t        imagecount_hint  = 3;
	Presentmode     presentmode_hint = Presentmode::eFifo;
	VkSurfaceKHR_T *vk_surface       = nullptr; // owned by window
};

struct le_swapchain_vk_api {
	static constexpr auto id      = "le_swapchain_vk";
	static constexpr auto pRegFun = register_le_swapchain_vk_api;

	// clang-format off
	struct swapchain_interface_t {
		le_swapchain_o *          ( *create                   ) ( le_swapchain_vk_api::swapchain_interface_t const & interface, le_backend_o* backend, const le_swapchain_vk_settings_t* settings_ );
		void                      ( *destroy                  ) ( le_swapchain_o* self );
		void                      ( *reset                    ) ( le_swapchain_o* self, const le_swapchain_vk_settings_t* settings_ );
		bool                      ( *present                  ) ( le_swapchain_o* self, VkQueue_T* queue, VkSemaphore_T* renderCompleteSemaphore, uint32_t* pImageIndex);
		bool                      ( *acquire_next_image       ) ( le_swapchain_o* self, VkSemaphore_T* semaphore_, uint32_t& imageIndex_ );
		VkSurfaceFormatKHR*       ( *get_surface_format       ) ( le_swapchain_o* self );
		VkImage_T*                ( *get_image                ) ( le_swapchain_o* self, uint32_t index_);
		uint32_t                  ( *get_image_width          ) ( le_swapchain_o* self );
		uint32_t                  ( *get_image_height         ) ( le_swapchain_o* self );
		size_t                    ( *get_images_count         ) ( le_swapchain_o* self );
	};

	// clang-format on

	swapchain_interface_t swapchain_i;     // base interface, forwards to either:
	swapchain_interface_t swapchain_khr_i; // khr swapchain interface
	swapchain_interface_t swapchain_img_i; // image swapchain interface
};

#ifdef __cplusplus
} // extern "C"

namespace le_swapchain_vk {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_swapchain_vk_api>( true );
#	else
const auto api = Registry::addApiStatic<le_swapchain_vk_api>();
#	endif

static const auto &swapchain_i     = api -> swapchain_i;
static const auto &swapchain_khr_i = api -> swapchain_khr_i; /// private interface, do not use directly.
static const auto &swapchain_img_i = api -> swapchain_img_i; /// private interface, do not use directly.

} // namespace le_swapchain_vk

namespace le {
namespace Swapchain {
using Presentmode = le_swapchain_vk_settings_t::Presentmode;
} // namespace Swapchain
} // namespace le

#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
