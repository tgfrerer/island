#ifndef GUARD_LE_BACKEND_VK_H
#define GUARD_LE_BACKEND_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_backend_vk_api( void *api );

struct le_backend_vk_api;

struct le_backend_o;

struct le_backend_vk_instance_o; // defined in le_instance_vk.cpp
struct le_backend_vk_device_o;   // defined in le_device_vk.cpp

struct le_swapchain_vk_settings_o;

struct pal_window_o;

struct VkInstance_T;
struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkQueue_T;

namespace vk {
    enum class Format;
}

struct le_backend_vk_settings_t {

	const char** requestedExtensions = nullptr;
	uint32_t numRequestedExtensions = 0;
	le_swapchain_vk_settings_o * swapchain_settings = nullptr;
};

struct le_backend_vk_api {
	static constexpr auto id       = "le_backend_vk";
	static constexpr auto pRegFun  = register_le_backend_vk_api;


	struct backend_vk_interface_t {
		le_backend_o* (*create)(le_backend_vk_settings_t* settings);
		void (*destroy)(le_backend_o* self);
		void (*setup)(le_backend_o* self);
		bool (*clear_frame)(le_backend_o* self, size_t frameIndex);
		bool (*acquire_swapchain_image)(le_backend_o*self, size_t frameIndex);
		void (*process_frame)(le_backend_o*self, size_t frameIndex /* renderGraph */);
		bool (*dispatch_frame)(le_backend_o* self, size_t frameIndex);
		bool (*create_window_surface)(le_backend_o* self, pal_window_o* window_);
		void (*create_swapchain)(le_backend_o* self, le_swapchain_vk_settings_o* swapchainSettings_);
		size_t(*get_num_swapchain_images)(le_backend_o* self);
		void (*reset_swapchain)(le_backend_o* self);
	};

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

	instance_interface_t   vk_instance_i;
	device_interface_t     vk_device_i;
	backend_vk_interface_t vk_backend_i;

	mutable le_backend_vk_instance_o *cUniqueInstance = nullptr;
};

#ifdef __cplusplus
} // extern "C"


namespace le {

class Backend : NoCopy, NoMove {
	const le_backend_vk_api &                      backendApiI = *Registry::getApi<le_backend_vk_api>();
	const le_backend_vk_api::backend_vk_interface_t &backendI   = backendApiI.vk_backend_i;
	le_backend_o *                     self        = nullptr;
	bool is_reference = false;

public:

	operator auto (){
		return self;
	}

	Backend( le_backend_vk_settings_t *settings )
	    : self( backendI.create( settings ) )
	    , is_reference( false ) {
	}

	Backend( le_backend_o *ref )
	    : self( ref )
	    , is_reference( true ) {
	}

	~Backend(){
		if ( !is_reference ) {
			backendI.destroy( self );
		}
	}

	void setup(){
		backendI.setup(self);
	}

	bool clearFrame(size_t frameIndex){
		return backendI.clear_frame(self, frameIndex);
	}

	void processFrame(size_t frameIndex){
		backendI.process_frame(self,frameIndex);
	}

	bool createWindowSurface(pal_window_o* window){
		return backendI.create_window_surface(self, window);
	}

	void createSwapchain(le_swapchain_vk_settings_o* swapchainSettings){
		backendI.create_swapchain(self,swapchainSettings);
	}

	size_t getNumSwapchainImages(){
		return backendI.get_num_swapchain_images(self);
	}

	bool acquireSwapchainImage(size_t frameIndex){
		return backendI.acquire_swapchain_image(self, frameIndex);
	}

	bool dispatchFrame(size_t frameIndex){
		return backendI.dispatch_frame(self, frameIndex);
	}

	bool resetSwapchain(){
		backendI.reset_swapchain(self);
	};

};


class Instance {
	const le_backend_vk_api &                      backendApiI = *Registry::getApi<le_backend_vk_api>();
	const le_backend_vk_api::instance_interface_t &instanceI   = backendApiI.vk_instance_i;
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

	operator auto () {
		return self;
	}
};


class Device : NoCopy, NoMove {
	const le_backend_vk_api &                    backendApiI = *Registry::getApi<le_backend_vk_api>();
	const le_backend_vk_api::device_interface_t &deviceI     = backendApiI.vk_device_i;
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
