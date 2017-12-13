#ifndef GUARD_LE_SWAPCHAIN_VK_H
#define GUARD_LE_SWAPCHAIN_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_swapchain_vk_api( void *api );

struct le_backend_swapchain_o;
struct le_backend_swapchain_vk_settings_o;

struct le_swapchain_vk_api {
	static constexpr auto id       = "le_swapchain_vk";
	static constexpr auto pRegFun  = register_le_swapchain_vk_api;

	enum class Presentmode : uint32_t {
		eDefault = 0,
		eImmediate,
		eMailbox,
		eFifo,
		eFifoRelaxed,
		eSharedDemandRefresh,
		eSharedContinuousRefresh,
	};

	struct swapchain_settings_vk_interface_t {
		le_backend_swapchain_vk_settings_o* (*create) ();
		void (*set_presentmode_hint)(le_backend_swapchain_vk_settings_o* self, const Presentmode& mode_);
		void (*set_image_count_hint)(le_backend_swapchain_vk_settings_o* self, uint32_t image_count_hint);
		void (*set_width)(le_backend_swapchain_vk_settings_o* self, uint32_t width);
		void (*set_height)(le_backend_swapchain_vk_settings_o* self, uint32_t height);
		void (*destroy)(le_backend_swapchain_vk_settings_o*);
	};

	struct swapchain_interface_t {
		le_backend_swapchain_o * ( *create           ) ( const le_backend_swapchain_vk_settings_o* settings_, le_backend_swapchain_o* old_swapchain );
		void                     ( *destroy          ) ( le_backend_swapchain_o* self_ );
	};

	swapchain_settings_vk_interface_t settings_vk_i;
	swapchain_interface_t swapchain_i;

};

#ifdef __cplusplus
} // extern "C"

namespace le {

class Swapchain {

	const le_swapchain_vk_api::swapchain_interface_t &swapchainI = Registry::getApi<le_swapchain_vk_api>()->swapchain_i;
	le_backend_swapchain_o *                       self       = swapchainI.create( nullptr, nullptr );

  public:

	using Presentmode = le_swapchain_vk_api::Presentmode;

	class Settings {
		const le_swapchain_vk_api::swapchain_settings_vk_interface_t &swapchainSettingsI = Registry::getApi<le_swapchain_vk_api>()->settings_vk_i;
		le_backend_swapchain_vk_settings_o *                       self               = swapchainSettingsI.create();

	  public:
		Settings() = default;
		~Settings(){
			swapchainSettingsI.destroy(self);
		}

		Settings & setPresentModeHint(const Presentmode& presentmode_){
			swapchainSettingsI.set_presentmode_hint(self,presentmode_);
			return *this;
		}

		Settings& setImageCountHint(uint32_t imageCount_){
			swapchainSettingsI.set_image_count_hint(self,imageCount_);
			return *this;
		}

		Settings& setWidth(uint32_t width_){
			swapchainSettingsI.set_width(self,width_);
			return *this;
		}

		Settings& setHeight(uint32_t height_){
			swapchainSettingsI.set_height(self,height_);
			return *this;
		}

		operator const le_backend_swapchain_vk_settings_o *() const {
			return self;
		}
	};

	Swapchain(const Settings& settings_)
	    : self(swapchainI.create(settings_, nullptr)){
	}

	~Swapchain(){
		swapchainI.destroy(self);
	}



};

} // namespace le
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
