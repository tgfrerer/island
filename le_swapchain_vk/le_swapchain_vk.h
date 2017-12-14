#ifndef GUARD_LE_SWAPCHAIN_VK_H
#define GUARD_LE_SWAPCHAIN_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_swapchain_vk_api( void *api );

struct le_backend_swapchain_o;

struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkSurfaceKHR_T;

struct le_swapchain_vk_api {
	static constexpr auto id       = "le_swapchain_vk";
	static constexpr auto pRegFun  = register_le_swapchain_vk_api;

	struct settings_o {
		enum class Presentmode : uint32_t {
			eDefault = 0,
			eImmediate,
			eMailbox,
			eFifo,
			eFifoRelaxed,
			eSharedDemandRefresh,
			eSharedContinuousRefresh,
		};
		uint32_t            width_hint                     = 640;
		uint32_t            height_hint                    = 480;
		uint32_t            imagecount_hint                = 3;
		Presentmode         presentmode_hint               = Presentmode::eFifo;
		VkDevice_T *        vk_device                      = nullptr; // owned by backend
		VkPhysicalDevice_T *vk_physical_device             = nullptr;
		VkSurfaceKHR_T *    vk_surface                     = nullptr; // owned by window
		uint32_t            vk_graphics_queue_family_index = ~uint32_t( 0 );
	};

	struct swapchain_interface_t {
		le_backend_swapchain_o * ( *create           ) ( const settings_o* settings_ );
		void                     ( *reset            ) ( le_backend_swapchain_o* self, const settings_o* settings_ );
		void                     ( *destroy          ) ( le_backend_swapchain_o* self_ );
	};

	swapchain_interface_t swapchain_i;

};

#ifdef __cplusplus
} // extern "C"

namespace le {

class Swapchain {

	const le_swapchain_vk_api::swapchain_interface_t &swapchainI = Registry::getApi<le_swapchain_vk_api>()->swapchain_i;
	le_backend_swapchain_o *                          self       = swapchainI.create( nullptr );

  public:
	using Presentmode = le_swapchain_vk_api::settings_o::Presentmode;

	class Settings {
		le_swapchain_vk_api::settings_o                           self;

	  public:
		Settings() = default;
		~Settings() = default;

		Settings &setPresentModeHint( const Presentmode &presentmode_ ) {
			self.presentmode_hint = presentmode_;
			return *this;
		}

		Settings &setImageCountHint( uint32_t imageCount_ ) {
			self.imagecount_hint = imageCount_;
			return *this;
		}

		Settings &setWidthHint( uint32_t width_ ) {
			self.width_hint = width_;
			return *this;
		}

		Settings &setHeightHint( uint32_t height_ ) {
			self.height_hint = height_;
			return *this;
		}

		Settings &setVkDevice( VkDevice_T *vk_device_ ) {
			self.vk_device = vk_device_;
			return *this;
		}

		Settings &setVkPhysicalDevice(VkPhysicalDevice_T* vk_physical_device_){
			self.vk_physical_device = vk_physical_device_;
			return *this;
		}

		Settings &setVkSurfaceKHR( VkSurfaceKHR_T *vk_surface_khr_ ) {
			self.vk_surface= vk_surface_khr_;
			return *this;
		}

		Settings& setGraphicsQueueFamilyIndex(uint32_t index_){
			self.vk_graphics_queue_family_index = index_;
			return *this;
		}

		operator const auto *() const {
			return &self;
		}
	};

	Swapchain( const Settings &settings_ )
	    : self( swapchainI.create( settings_ ) ) {
	}

	void reset( const Settings &settings_ ) {
		swapchainI.reset( self, settings_ );
	}

	~Swapchain() {
		swapchainI.destroy( self );
	}
};

} // namespace le
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
