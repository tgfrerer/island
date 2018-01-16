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
struct VkQueue_T;

namespace vk {
    enum class Format;
}

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
		le_backend_vk_device_o *    ( *create                                  ) ( le_backend_vk_instance_o* instance_ );
		void                        ( *destroy                                 ) ( le_backend_vk_device_o* self_ );

		void                        ( *decrease_reference_count                ) ( le_backend_vk_device_o* self_ );
		void                        ( *increase_reference_count                ) ( le_backend_vk_device_o* self_ );
		uint32_t                    ( *get_reference_count                     ) ( le_backend_vk_device_o* self_ );

		uint32_t                    ( *get_default_graphics_queue_family_index ) ( le_backend_vk_device_o* self_ );
		uint32_t                    ( *get_default_compute_queue_family_index  ) ( le_backend_vk_device_o* self_ );
		VkQueue_T *                 ( *get_default_graphics_queue              ) ( le_backend_vk_device_o* self_ );
		VkQueue_T *                 ( *get_default_compute_queue               ) ( le_backend_vk_device_o* self_ );
		vk::Format                  ( *get_default_depth_stencil_format        ) ( le_backend_vk_device_o* self_ );
		VkPhysicalDevice_T*         ( *get_vk_physical_device                  ) ( le_backend_vk_device_o* self_ );
		VkDevice_T*                 ( *get_vk_device                           ) ( le_backend_vk_device_o* self_ );
	};

	instance_interface_t  instance_i;
	device_interface_t    device_i;

	mutable le_backend_vk_instance_o *cUniqueInstance = nullptr;
};

#ifdef __cplusplus
} // extern "C"


namespace le {

class Instance {
	const le_backend_vk_api &                      backendApiI = *Registry::getApi<le_backend_vk_api>();
	const le_backend_vk_api::instance_interface_t &instanceI   = backendApiI.instance_i;
	le_backend_vk_instance_o *                     self        = nullptr;

  public:
	Instance( const char **extensionsArray_ = nullptr, uint32_t numExtensions_ = 0 )
	    : self( instanceI.create( &backendApiI, extensionsArray_, numExtensions_ ) ) {
	}

	~Instance() {
		instanceI.destroy( self );
	}

	VkInstance_T *getVkInstance() {
		return instanceI.get_vk_instance( self );
	}

	operator le_backend_vk_instance_o *() {
		return self;
	}
};



class Device : NoCopy, NoMove {
	const le_backend_vk_api &                    backendApiI = *Registry::getApi<le_backend_vk_api>();
	const le_backend_vk_api::device_interface_t &deviceI     = backendApiI.device_i;
	le_backend_vk_device_o *                     self        = nullptr;

  public:

	Device( le_backend_vk_instance_o *instance_ )
	    : self( deviceI.create( instance_ ) ){
		deviceI.increase_reference_count(self);
	}

	~Device() {
		deviceI.decrease_reference_count(self);
		if (deviceI.get_reference_count(self) == 0){
			deviceI.destroy( self );
		}
	}

	// copy assignment operator
	Device& operator=(const Device& lhs) = delete ;

	// move assignment operator
	Device& operator=(const Device&& lhs) = delete ;

	// copy constructor
	Device(const Device& lhs)
	    :self(lhs.self){
		deviceI.increase_reference_count( self );
	}

	// reference from data constructor
	Device( le_backend_vk_device_o *device_ )
	    : self( device_ ) {
		deviceI.increase_reference_count( self );
	}

	VkDevice_T *getVkDevice() const {
		return deviceI.get_vk_device( self );
	}

	VkPhysicalDevice_T *getVkPhysicalDevice() const {
		return deviceI.get_vk_physical_device( self );
	}

	uint32_t getDefaultGraphicsQueueFamilyIndex() const {
		return deviceI.get_default_graphics_queue_family_index( self );
	}

	uint32_t getDefaultComputeQueueFamilyIndex() const {
		return deviceI.get_default_compute_queue_family_index( self );
	}

	VkQueue_T *getDefaultGraphicsQueue() const {
		return deviceI.get_default_graphics_queue( self );
	}

	VkQueue_T *getDefaultComputeQueue() const {
		return deviceI.get_default_compute_queue( self );
	}

	vk::Format getDefaultDepthStencilFormat() const {
		return deviceI.get_default_depth_stencil_format(self);
	}

	operator auto () {
		return self;
	}

};

} // namespace le
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
