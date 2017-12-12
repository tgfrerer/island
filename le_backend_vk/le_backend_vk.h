#ifndef GUARD_PAL_BACKEND_VK_H
#define GUARD_PAL_BACKEND_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_backend_vk_api( void *api );

struct le_backend_vk_api;
struct le_backend_vk_instance_o;
struct VkInstance_T;

struct le_backend_vk_device_o;

struct le_backend_vk_api {
	static constexpr auto id       = "le_backend_vk";
	static constexpr auto pRegFun  = register_le_backend_vk_api;

	struct instance_interface_t {
		le_backend_vk_instance_o *  ( *create           ) ( le_backend_vk_api * , const char** requestedExtensionNames_, uint32_t requestedExtensionNamesCount_);
		void                        ( *destroy          ) ( le_backend_vk_instance_o* self_);
		void                        ( *post_reload_hook ) ( le_backend_vk_instance_o* self_);
		VkInstance_T*               ( *get_VkInstance   ) ( le_backend_vk_instance_o* self_);
	};

	struct device_interface_t {
		le_backend_vk_device_o *    ( *create           ) ( le_backend_vk_instance_o* instance_ );
		void                        ( *destroy          ) ( le_backend_vk_device_o* self_);
	};

	instance_interface_t       instance_i;
	device_interface_t         device_i;

	le_backend_vk_instance_o *cUniqueInstance = nullptr;
};

#ifdef __cplusplus
} // extern "C"


namespace le {

class Instance {
	le_backend_vk_api::instance_interface_t &mInstanceI;
	le_backend_vk_instance_o *               self;

  public:

	Instance(const char** extensionsArray_ = nullptr, uint32_t numExtensions_ = 0)
	    : mInstanceI( Registry::getApi<le_backend_vk_api>()->instance_i )
	    , self( mInstanceI.create( Registry::getApi<le_backend_vk_api>(),extensionsArray_, numExtensions_ ) ) {
	}

	~Instance() {
		mInstanceI.destroy( self );
	}

	operator VkInstance_T* (){
		return mInstanceI.get_VkInstance(self);
	}

};

class Device {
	le_backend_vk_api::device_interface_t &mDeviceI;
	le_backend_vk_device_o *               self;

  public:
	Device( le_backend_vk_instance_o *instance_ )
	    : mDeviceI( Registry::getApi<le_backend_vk_api>()->device_i )
	    , self( mDeviceI.create( instance_ ) ) {
	}

	~Device(){
		mDeviceI.destroy(self);
	}
};

} // namespace pal
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
