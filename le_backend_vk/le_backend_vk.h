#ifndef GUARD_LE_BACKEND_VK_H
#define GUARD_LE_BACKEND_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_backend_vk_api( void *api );

struct le_backend_vk_api;
struct le_backend_vk_instance_o;
struct le_backend_vk_device_o;

struct VkInstance_T;
struct VkDevice_T;
struct VkPhysicalDevice_T;

struct le_backend_vk_api {
	static constexpr auto id       = "le_backend_vk";
	static constexpr auto pRegFun  = register_le_backend_vk_api;

	struct instance_interface_t {
		le_backend_vk_instance_o *  ( *create           ) ( const le_backend_vk_api * , const char** requestedExtensionNames_, uint32_t requestedExtensionNamesCount_ );
		void                        ( *destroy          ) ( le_backend_vk_instance_o* self_ );
		void                        ( *post_reload_hook ) ( le_backend_vk_instance_o* self_ );
		VkInstance_T*               ( *get_vk_instance  ) ( le_backend_vk_instance_o* self_ );
	};

	struct device_interface_t {
		le_backend_vk_device_o *    ( *create                 ) ( le_backend_vk_instance_o* instance_ );
		VkPhysicalDevice_T*         ( *get_vk_physical_device ) ( le_backend_vk_device_o* self_ );
		VkDevice_T*                 ( *get_vk_device          ) ( le_backend_vk_device_o* self_ );
		void                        ( *destroy                ) ( le_backend_vk_device_o* self_ );
	};

	instance_interface_t  instance_i;
	device_interface_t    device_i;

	mutable le_backend_vk_instance_o *cUniqueInstance = nullptr;
};

#ifdef __cplusplus
} // extern "C"


namespace le {

class Backend  {
	const le_backend_vk_api &                      backendApiI = *Registry::getApi<le_backend_vk_api>();
	const le_backend_vk_api::instance_interface_t &instanceI   = backendApiI.instance_i;
	const le_backend_vk_api::device_interface_t &  deviceI     = backendApiI.device_i;
	le_backend_vk_instance_o *                     mInstance   = nullptr;
	le_backend_vk_device_o *                       mDevice     = nullptr;

  public:
	Backend( const char **extensionsArray_ = nullptr, uint32_t numExtensions_ = 0 )
	    : mInstance( instanceI.create( &backendApiI, extensionsArray_, numExtensions_ ) )
	    , mDevice( deviceI.create( mInstance ) ) {
	}

	~Backend() {
		deviceI.destroy( mDevice );
		instanceI.destroy( mInstance );
	}

	VkInstance_T * getVkInstance() {
		return instanceI.get_vk_instance( mInstance );
	}

	VkDevice_T * getVkDevice(){
		return deviceI.get_vk_device(mDevice);
	}

	VkPhysicalDevice_T* getVkPhysicalDevice(){
		return deviceI.get_vk_physical_device(mDevice);
	}

};

} // namespace le
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
