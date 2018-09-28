#ifndef GUARD_LE_BACKEND_VK_H
#define GUARD_LE_BACKEND_VK_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LE_DEFINE_HANDLE_GUARD
#	define LE_DEFINE_HANDLE( object ) typedef struct object##_T *object;
#	define LE_DEFINE_HANDLE_GUARD
#endif
LE_DEFINE_HANDLE( LeResourceHandle )

void register_le_backend_vk_api( void *api );
void register_le_instance_vk_api( void *api );       // for le_instance_vk.cpp
void register_le_allocator_linear_api( void *api_ ); // for le_allocator.cpp
void register_le_device_vk_api( void *api );         // for le_device_vk.cpp

struct le_backend_vk_api;

struct le_backend_o;

struct le_backend_vk_instance_o; // defined in le_instance_vk.cpp
struct le_backend_vk_device_o;   // defined in le_device_vk.cpp
struct le_renderpass_o;
struct le_buffer_o;
struct le_allocator_o;

struct le_swapchain_vk_settings_o;
struct pal_window_o;

struct le_shader_module_o;
struct le_graphics_pipeline_state_o;
struct le_graphics_pipeline_create_info_t;

struct VkInstance_T;
struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkQueue_T;
struct VkPhysicalDeviceProperties;
struct VkPhysicalDeviceMemoryProperties;

struct VkMemoryRequirements;
struct VkMemoryAllocateInfo;

struct VmaAllocationInfo;

enum class LeShaderType : uint64_t; // we're forward declaring this enum, for heaven's sake...
enum class LeResourceType : uint8_t;

typedef int LeFormat_t; // we're declaring this as a placeholder for image format enum

struct le_backend_vk_settings_t {
	const char **               requestedExtensions    = nullptr;
	uint32_t                    numRequestedExtensions = 0;
	le_swapchain_vk_settings_o *swapchain_settings     = nullptr;
};

struct le_backend_vk_api {
	static constexpr auto id      = "le_backend_vk";
	static constexpr auto pRegFun = register_le_backend_vk_api;

	// clang-format off
	struct backend_vk_interface_t {
		le_backend_o *         ( *create                   ) ( le_backend_vk_settings_t *settings );
		void                   ( *destroy                  ) ( le_backend_o *self );
		void                   ( *setup                    ) ( le_backend_o *self );
		bool                   ( *poll_frame_fence         ) ( le_backend_o* self, size_t frameIndex);
		bool                   ( *clear_frame              ) ( le_backend_o *self, size_t frameIndex );
		void                   ( *process_frame            ) ( le_backend_o *self, size_t frameIndex );
		bool                   ( *acquire_physical_resources ) ( le_backend_o *self, size_t frameIndex, le_renderpass_o **passes, size_t numRenderPasses  );
		bool                   ( *dispatch_frame           ) ( le_backend_o *self, size_t frameIndex );
		bool                   ( *create_window_surface    ) ( le_backend_o *self, pal_window_o *window_ );
		void                   ( *create_swapchain         ) ( le_backend_o *self, le_swapchain_vk_settings_o *swapchainSettings_ );
		size_t                 ( *get_num_swapchain_images ) ( le_backend_o *self );
		void                   ( *reset_swapchain          ) ( le_backend_o *self );
		le_allocator_o**       ( *get_transient_allocators ) ( le_backend_o* self, size_t frameIndex, size_t numAllocators);

		le_graphics_pipeline_state_o* (*create_graphics_pipeline_state_object)(le_backend_o* self, le_graphics_pipeline_create_info_t const * info);

		LeResourceHandle       ( *declare_resource         ) (le_backend_o* self, LeResourceType type);
		LeResourceHandle       ( *get_backbuffer_resource  ) (le_backend_o* self);

		le_shader_module_o*    ( *create_shader_module     ) ( le_backend_o* self, char const * path, LeShaderType moduleType);
		void                   ( *update_shader_modules    ) ( le_backend_o* self );
		void                   ( *destroy_shader_module    ) ( le_backend_o* self, le_shader_module_o* shader_module);

	};

	struct instance_interface_t {
		le_backend_vk_instance_o *  ( *create           ) ( const char** requestedExtensionNames_, uint32_t requestedExtensionNamesCount_ );
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
		LeFormat_t                  ( *get_default_depth_stencil_format        ) ( le_backend_vk_device_o* self_ );
		VkPhysicalDevice_T*         ( *get_vk_physical_device                  ) ( le_backend_vk_device_o* self_ );
		VkDevice_T*                 ( *get_vk_device                           ) ( le_backend_vk_device_o* self_ );

		const VkPhysicalDeviceProperties&       ( *get_vk_physical_device_properties        ) ( le_backend_vk_device_o* self );
		const VkPhysicalDeviceMemoryProperties& ( *get_vk_physical_device_memory_properties ) ( le_backend_vk_device_o* self );
		bool                                    ( *get_memory_allocation_info               ) ( le_backend_vk_device_o *self, const VkMemoryRequirements &memReqs, const uint32_t &memPropsRef, VkMemoryAllocateInfo *pMemoryAllocationInfo );
	};

	struct allocator_linear_interface_t {
		le_allocator_o *        ( *create               ) ( VmaAllocationInfo const *info, uint16_t alignment);
		void                    ( *destroy              ) ( le_allocator_o *self );
		bool                    ( *allocate             ) ( le_allocator_o* self, uint64_t numBytes, void ** pData, uint64_t* bufferOffset);
		void                    ( *reset                ) ( le_allocator_o* self );
		LeResourceHandle        ( *get_le_resource_id   ) ( le_allocator_o* self );
	};
	// clang-format on

	allocator_linear_interface_t le_allocator_linear_i;
	instance_interface_t         vk_instance_i;
	device_interface_t           vk_device_i;
	backend_vk_interface_t       vk_backend_i;

	mutable le_backend_vk_instance_o *cUniqueInstance = nullptr;
};

#ifdef __cplusplus
} // extern "C"

namespace le_backend_vk {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_backend_vk_api>( true );
#	else
const auto api = Registry::addApiStatic<le_backend_vk_api>();
#	endif

static const auto &vk_backend_i          = api -> vk_backend_i;
static const auto &le_allocator_linear_i = api -> le_allocator_linear_i;
static const auto &vk_instance_i         = api -> vk_instance_i;
static const auto &vk_device_i           = api -> vk_device_i;

} // namespace le_backend_vk

namespace le {

class Backend : NoCopy, NoMove {
	le_backend_o *self         = nullptr;
	bool          is_reference = false;

  public:
	operator auto() {
		return self;
	}

	Backend( le_backend_vk_settings_t *settings )
	    : self( le_backend_vk::vk_backend_i.create( settings ) )
	    , is_reference( false ) {
	}

	Backend( le_backend_o *ref )
	    : self( ref )
	    , is_reference( true ) {
	}

	~Backend() {
		if ( !is_reference ) {
			le_backend_vk::vk_backend_i.destroy( self );
		}
	}

	void setup() {
		le_backend_vk::vk_backend_i.setup( self );
	}

	bool clearFrame( size_t frameIndex ) {
		return le_backend_vk::vk_backend_i.clear_frame( self, frameIndex );
	}

	void processFrame( size_t frameIndex ) {
		le_backend_vk::vk_backend_i.process_frame( self, frameIndex );
	}

	bool createWindowSurface( pal_window_o *window ) {
		return le_backend_vk::vk_backend_i.create_window_surface( self, window );
	}

	void createSwapchain( le_swapchain_vk_settings_o *swapchainSettings ) {
		le_backend_vk::vk_backend_i.create_swapchain( self, swapchainSettings );
	}

	size_t getNumSwapchainImages() {
		return le_backend_vk::vk_backend_i.get_num_swapchain_images( self );
	}

	bool acquirePhysicalResources( size_t frameIndex, struct le_renderpass_o **passes, size_t numRenderPasses ) {
		return le_backend_vk::vk_backend_i.acquire_physical_resources( self, frameIndex, passes, numRenderPasses );
	}

	bool dispatchFrame( size_t frameIndex ) {
		return le_backend_vk::vk_backend_i.dispatch_frame( self, frameIndex );
	}

	void resetSwapchain() {
		le_backend_vk::vk_backend_i.reset_swapchain( self );
	}
};

class Instance {
	le_backend_vk_instance_o *self = nullptr;

  public:
	Instance( const char **extensionsArray_ = nullptr, uint32_t numExtensions_ = 0 )
	    : self( le_backend_vk::vk_instance_i.create( extensionsArray_, numExtensions_ ) ) {
	}

	~Instance() {
		le_backend_vk::vk_instance_i.destroy( self );
	}

	VkInstance_T *getVkInstance() {
		return le_backend_vk::vk_instance_i.get_vk_instance( self );
	}

	operator auto() {
		return self;
	}
};

class Device : NoCopy, NoMove {
	le_backend_vk_device_o *self = nullptr;

  public:
	Device( le_backend_vk_instance_o *instance_ )
	    : self( le_backend_vk::vk_device_i.create( instance_ ) ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	~Device() {
		le_backend_vk::vk_device_i.decrease_reference_count( self );
		if ( le_backend_vk::vk_device_i.get_reference_count( self ) == 0 ) {
			le_backend_vk::vk_device_i.destroy( self );
		}
	}

	// copy constructor
	Device( const Device &lhs )
	    : self( lhs.self ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	// reference from data constructor
	Device( le_backend_vk_device_o *device_ )
	    : self( device_ ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	VkDevice_T *getVkDevice() const {
		return le_backend_vk::vk_device_i.get_vk_device( self );
	}

	VkPhysicalDevice_T *getVkPhysicalDevice() const {
		return le_backend_vk::vk_device_i.get_vk_physical_device( self );
	}

	uint32_t getDefaultGraphicsQueueFamilyIndex() const {
		return le_backend_vk::vk_device_i.get_default_graphics_queue_family_index( self );
	}

	uint32_t getDefaultComputeQueueFamilyIndex() const {
		return le_backend_vk::vk_device_i.get_default_compute_queue_family_index( self );
	}

	VkQueue_T *getDefaultGraphicsQueue() const {
		return le_backend_vk::vk_device_i.get_default_graphics_queue( self );
	}

	VkQueue_T *getDefaultComputeQueue() const {
		return le_backend_vk::vk_device_i.get_default_compute_queue( self );
	}

	LeFormat_t getDefaultDepthStencilFormat() const {
		return le_backend_vk::vk_device_i.get_default_depth_stencil_format( self );
	}

	operator auto() {
		return self;
	}
};

} // namespace le
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
