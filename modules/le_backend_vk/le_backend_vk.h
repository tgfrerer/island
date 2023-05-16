#ifndef GUARD_LE_BACKEND_VK_H
#define GUARD_LE_BACKEND_VK_H

#include <stdint.h>
#include "le_core.h"

struct le_backend_o;
struct le_backend_vk_api;

struct le_swapchain_settings_t;
struct le_window_o;

struct le_backend_vk_instance_o; // defined in le_instance_vk.cpp
struct le_device_o;              // defined in le_device_vk.cpp
struct le_renderpass_o;
struct le_buffer_o;
struct le_allocator_o;
struct le_staging_allocator_o;
struct le_resource_handle_t; // defined in renderer_types
struct le_command_stream_t;

struct le_pipeline_manager_o;

constexpr uint8_t LE_MAX_BOUND_DESCRIPTOR_SETS = 8;
constexpr uint8_t LE_MAX_COLOR_ATTACHMENTS     = 16; // maximum number of color attachments to a renderpass

struct graphics_pipeline_state_o; // for le_pipeline_builder
struct compute_pipeline_state_o;  // for le_pipeline_builder
struct rtx_pipeline_state_o;      // for le_pipeline_builder

LE_OPAQUE_HANDLE( le_gpso_handle );
LE_OPAQUE_HANDLE( le_cpso_handle );
LE_OPAQUE_HANDLE( le_rtxpso_handle );
LE_OPAQUE_HANDLE( le_shader_module_handle );

LE_OPAQUE_HANDLE( le_resource_handle );
LE_OPAQUE_HANDLE( le_img_resource_handle );
LE_OPAQUE_HANDLE( le_buf_resource_handle );
LE_OPAQUE_HANDLE( le_tlas_resource_handle );
LE_OPAQUE_HANDLE( le_blas_resource_handle );

LE_OPAQUE_HANDLE( le_cpso_handle );
LE_OPAQUE_HANDLE( le_cpso_handle );
LE_OPAQUE_HANDLE( le_rtx_blas_info_handle ); // handle for backend-managed rtx bottom level acceleration info
LE_OPAQUE_HANDLE( le_rtx_tlas_info_handle ); // handle for backend-managed rtx top level acceleration info

LE_OPAQUE_HANDLE( le_swapchain_handle ); // opaque swapchain handle

struct le_rtx_geometry_t;

struct VkInstance_T;
struct VkDevice_T;
struct VkImage_T;
struct VkBuffer_T;
struct VkPhysicalDevice_T;
struct VkQueue_T;
struct VkPhysicalDeviceProperties;
struct VkPhysicalDeviceMemoryProperties;
struct VkPhysicalDeviceRayTracingPipelinePropertiesKHR;
struct VkImageCreateInfo;
struct VkBufferCreateInfo;
struct VkMemoryRequirements;
struct VkMemoryAllocateInfo;
struct VkSpecializationMapEntry;
struct VkPhysicalDeviceFeatures2;

struct VkFormatEnum;
struct BackendRenderPass;
struct BackendQueueInfo;

struct VmaAllocator_T;
struct VmaAllocation_T;
struct VmaAllocationCreateInfo;
struct VmaAllocationInfo;

typedef uint32_t VkFlags;
typedef VkFlags  VkQueueFlags;

namespace le {
enum class ShaderStageFlagBits : uint32_t;
struct BuildAccelerationStructureFlagsKHR;
} // namespace le

struct LeShaderSourceLanguageEnum;
// enum class LeResourceType : uint8_t;

struct VkFormatEnum; // wrapper around `vk::Format`. Defined in <le_backend_types_internal.h>

struct le_resource_info_t;

struct le_backend_vk_settings_o; // global settings for backend singleton

struct le_pipeline_layout_info {
	uint64_t pipeline_layout_key     = 0;  // handle to pipeline layout
	uint64_t set_layout_keys[ 8 ]    = {}; // maximum number of DescriptorSets is 8
	uint64_t set_layout_count        = 0;  // number of actually used DescriptorSetLayouts for this layout
	uint32_t active_vk_shader_stages = 0;  // bitfield of VkShaderStageFlagBits
};

struct le_pipeline_and_layout_info_t {
	struct VkPipeline_T*    pipeline;
	le_pipeline_layout_info layout_info;
};

struct le_backend_vk_api {

	struct backend_vk_settings_interface_t // global settings for backend - must be set before backend setup- after that, settings are read-only.
	{
		bool ( *add_required_device_extension )( char const* ext );                           // returns true if successfully added - returns false if setting was already present
		bool ( *add_required_instance_extension )( char const* ext );                         // -"-
		VkPhysicalDeviceFeatures2 const* ( *get_requested_physical_device_features_chain )(); // readonly

		void ( *set_concurrency_count )( uint32_t concurrency_count );
		bool ( *set_data_frames_count )( uint32_t data_frames_count );

		void ( *get_requested_queue_capabilities )( VkQueueFlags* queues, uint32_t* num_queues );
		bool ( *set_requested_queue_capabilities )( VkQueueFlags* queues, uint32_t num_queues );
	};

	// clang-format off
	struct backend_vk_interface_t {
		le_backend_o *         ( *create                     ) ( );
		void                   ( *destroy                    ) ( le_backend_o *self );

		void 				   ( *initialise 				 ) ( le_backend_o* self);
		void                   ( *setup                      ) ( le_backend_o *self);

		bool                   ( *poll_frame_fence           ) ( le_backend_o* self, size_t frameIndex);
		bool                   ( *clear_frame                ) ( le_backend_o *self, size_t frameIndex );
		void                   ( *process_frame              ) ( le_backend_o *self, size_t frameIndex );
		bool                   ( *acquire_physical_resources ) ( le_backend_o *self, size_t frameIndex, le_renderpass_o **passes, size_t numRenderPasses, le_resource_handle const * declared_resources, le_resource_info_t const * declared_resources_infos, size_t const & declared_resources_count );
		void                   ( *set_frame_queue_submission_keys ) ( le_backend_o *self, size_t frameIndex, void const * p_affinity_masks, uint32_t num_affinity_masks, char const** root_names, uint32_t root_names_count); // void* p_affinity_masks must be cast to le::RootPassesField, we can't forward-declare a using declaration

		bool                   ( *dispatch_frame             ) ( le_backend_o *self, size_t frameIndex );
		le_allocator_o**       ( *get_transient_allocators   ) ( le_backend_o* self, size_t frameIndex);
		le_command_stream_t**  ( *get_frame_command_streams  ) ( le_backend_o* self, size_t frameIndex, size_t num_command_streams);
		le_staging_allocator_o*( *get_staging_allocator      ) ( le_backend_o* self, size_t frameIndex);

		le_shader_module_handle( *create_shader_module       ) ( le_backend_o* self, char const * path, const LeShaderSourceLanguageEnum& shader_source_language, const le::ShaderStageFlagBits& moduleType, char const * macro_definitions, le_shader_module_handle handle, VkSpecializationMapEntry const * specialization_map_entries, uint32_t specialization_map_entries_count, void * specialization_map_data, uint32_t specialization_map_data_num_bytes);
		void                   ( *update_shader_modules      ) ( le_backend_o* self );

		le_pipeline_manager_o* ( *get_pipeline_cache         ) ( le_backend_o* self);


		// --- modern swapchain interface
		le_swapchain_handle    ( * add_swapchain 		    ) ( le_backend_o* self, le_swapchain_settings_t const * settings);
		bool 				   ( * remove_swapchain 	 	) ( le_backend_o* self, le_swapchain_handle swapchain);
		le_img_resource_handle ( * get_swapchain_resource   ) ( le_backend_o* self, le_swapchain_handle swapchain );
		le_img_resource_handle ( * get_swapchain_resource_default ) ( le_backend_o* self);
		bool                   ( * get_swapchain_extent     ) ( le_backend_o* self, le_swapchain_handle swapchain, uint32_t * p_width, uint32_t * p_height );
		bool                   ( * get_swapchains           ) ( le_backend_o* self, size_t *num_swapchains , le_swapchain_handle* p_swapchain_handles);
		
		// declares all resources belonging to a swapchain and makes them available to the current frame.
		void 					( *acquire_swapchain_resources)(le_backend_o* self, size_t frameIndex);
		// ---

		// return number of in-flight backend data frames
		size_t                 ( *get_data_frames_count   ) ( le_backend_o *self );

		// this is called from the rendergraph to patch renderpass sizes - it must only be called on the recording thread
		bool                   ( *get_swapchains_infos        ) ( le_backend_o* self, uint32_t frame_index, uint32_t *count, uint32_t* p_width, uint32_t * p_height, le_img_resource_handle * p_handlle );


		le_rtx_blas_info_handle( *create_rtx_blas_info )(le_backend_o* self, le_rtx_geometry_t const * geometries, uint32_t geometries_count,le::BuildAccelerationStructureFlagsKHR const * flags);
		le_rtx_tlas_info_handle( *create_rtx_tlas_info )(le_backend_o* self,  uint32_t instances_count, le::BuildAccelerationStructureFlagsKHR const * flags);
	};

	struct private_backend_vk_interface_t {
		le_device_o*              ( *get_le_device            )(le_backend_o* self);
		le_backend_vk_instance_o* ( *get_instance             )(le_backend_o* self);
		VkDevice_T*               ( *get_vk_device            )(le_backend_o const * self);
		VkPhysicalDevice_T*       ( *get_vk_physical_device   )(le_backend_o const * self);

        BackendQueueInfo*         ( *get_default_graphics_queue_info )(le_backend_o* self);

		int32_t ( *allocate_image )
		(
		    le_backend_o*                   self,
		    VkImageCreateInfo const *       pImageCreateInfo,
		    VmaAllocationCreateInfo const * pAllocationCreateInfo,
		    VkImage_T **                    pImage,
		    VmaAllocation_T **              pAllocation,
		    VmaAllocationInfo *             pAllocationInfo
		);

		void (*destroy_image)(le_backend_o* self, struct VkImage_T * image, struct VmaAllocation_T* allocation);

		int32_t ( *allocate_buffer )
		(
		    le_backend_o*                  self,
		    VkBufferCreateInfo const*      pBufferCreateInfo,
		    VmaAllocationCreateInfo const *pAllocationCreateInfo,
		    VkBuffer_T* *                  pBuffer,
		    VmaAllocation_T **             pAllocation,
		    VmaAllocationInfo *            pAllocationInfo
		);

		void ( *destroy_buffer )(le_backend_o* self, struct VkBuffer_T * buffer, struct VmaAllocation_T* allocation);

	};

	struct instance_interface_t {
		le_backend_vk_instance_o *  ( *create           ) ( const char** requestedExtensionNames_, uint32_t requestedExtensionNamesCount_ );
		void                        ( *destroy          ) ( le_backend_vk_instance_o* self_ );
		void                        ( *post_reload_hook ) ( le_backend_vk_instance_o* self_ );
		VkInstance_T*               ( *get_vk_instance  ) ( le_backend_vk_instance_o* self_ );
		bool                        ( *is_extension_available ) ( le_backend_vk_instance_o* self, char const * extension_name);

	};

	struct device_interface_t {
		le_device_o *               ( *create                                  ) ( le_backend_vk_instance_o* instance_, const char **extension_names, uint32_t extension_names_count );
		void                        ( *destroy                                 ) ( le_device_o* self_ );

		le_device_o *			    ( *decrease_reference_count                ) ( le_device_o* self_ );
		le_device_o *			    ( *increase_reference_count                ) ( le_device_o* self_ );
		uint32_t                    ( *get_reference_count                     ) ( le_device_o* self_ );

        void                        ( *get_queue_family_indices                ) ( le_device_o* self, uint32_t * family_indices, uint32_t* num_family_indices);
        void                        (* get_queues_info                         ) ( le_device_o* self, uint32_t* queue_count, VkQueue_T** queues, uint32_t* queues_family_index, VkQueueFlags* queues_flags);
		VkFormatEnum const*         ( *get_default_depth_stencil_format        ) ( le_device_o* self_ );
		VkPhysicalDevice_T*         ( *get_vk_physical_device                  ) ( le_device_o* self_ );
		VkDevice_T*                 ( *get_vk_device                           ) ( le_device_o* self_ );
		bool                        ( *is_extension_available                  ) ( le_device_o* self, char const * extension_name);

		const VkPhysicalDeviceProperties*       ( *get_vk_physical_device_properties        ) ( le_device_o* self );
		const VkPhysicalDeviceMemoryProperties* ( *get_vk_physical_device_memory_properties ) ( le_device_o* self );
		void                                    ( *get_vk_physical_device_ray_tracing_properties)(le_device_o* self, VkPhysicalDeviceRayTracingPipelinePropertiesKHR* properties);
		bool                                    ( *get_memory_allocation_info               ) ( le_device_o *self, const VkMemoryRequirements &memReqs, const uint32_t &memPropsRef, VkMemoryAllocateInfo *pMemoryAllocationInfo );
	};

	struct le_pipeline_manager_interface_t {
		le_pipeline_manager_o*                   ( *create                            ) ( le_device_o * device        );
		void                                     ( *destroy                           ) ( le_pipeline_manager_o* self );

		bool                                     ( *introduce_graphics_pipeline_state ) ( le_pipeline_manager_o *self, graphics_pipeline_state_o* gpso, le_gpso_handle *gpsoHandle);
		bool                                     ( *introduce_compute_pipeline_state  ) ( le_pipeline_manager_o *self, compute_pipeline_state_o* cpso, le_cpso_handle *cpsoHandle);
		bool                                     ( *introduce_rtx_pipeline_state      ) ( le_pipeline_manager_o *self, rtx_pipeline_state_o* cpso, le_rtxpso_handle *cpsoHandle);

		le_pipeline_and_layout_info_t            ( *produce_graphics_pipeline         ) ( le_pipeline_manager_o *self, le_gpso_handle gpsoHandle, const BackendRenderPass &pass, uint32_t subpass ) ;
		le_pipeline_and_layout_info_t            ( *produce_rtx_pipeline              ) ( le_pipeline_manager_o *self, le_rtxpso_handle rtxpsoHandle, char ** shader_group_data);
		le_pipeline_and_layout_info_t            ( *produce_compute_pipeline          ) ( le_pipeline_manager_o *self, le_cpso_handle cpsoHandle);

		le_shader_module_handle                  ( *create_shader_module              ) ( le_pipeline_manager_o* self, char const * path, const LeShaderSourceLanguageEnum& shader_source_language, const le::ShaderStageFlagBits& moduleType, char const *macro_definitions, le_shader_module_handle handle, VkSpecializationMapEntry const * specialization_map_entries, uint32_t specialization_map_entries_count, void * specialization_map_data, uint32_t specialization_map_data_num_bytes);
		void                                     ( *update_shader_modules             ) ( le_pipeline_manager_o* self );

        bool                                     ( *graphics_pipeline_add_shader_stage )(le_pipeline_manager_o* self, le_gpso_handle gpsoHandle, le_shader_module_handle shader_stage);

		struct VkPipelineLayout_T*               ( *get_pipeline_layout               ) ( le_pipeline_manager_o* self, uint64_t pipeline_layout_key);
		const struct le_descriptor_set_layout_t* ( *get_descriptor_set_layout         ) ( le_pipeline_manager_o* self, uint64_t setlayout_key);
	};

	struct allocator_linear_interface_t {
		le_allocator_o *        ( *create               ) ( VmaAllocationInfo const *info, uint16_t alignment);
		void                    ( *destroy              ) ( le_allocator_o* self );
		bool                    ( *allocate             ) ( le_allocator_o* self, uint64_t numBytes, void ** pData, uint64_t* bufferOffset, le_buf_resource_handle *p_buffer);
		void                    ( *reset                ) ( le_allocator_o* self );
	};

	struct staging_allocator_interface_t {
		le_staging_allocator_o* ( *create  )( VmaAllocator_T* const vmaAlloc, VkDevice_T* const device );
		void                    ( *destroy )( le_staging_allocator_o* self ) ;
		void                    ( *reset   )( le_staging_allocator_o* self );
		bool                    ( *map     )( le_staging_allocator_o* self, uint64_t numBytes, void **pData, le_buf_resource_handle *resource_handle );
	};

	struct shader_module_interface_t {
		le::ShaderStageFlagBits ( * get_stage ) ( le_pipeline_manager_o* manager, le_shader_module_handle module );
	};

	struct private_shader_file_watcher_interface_t {
		void * on_callback_addr;
	};

	// clang-format on

	le_backend_vk_settings_o*       backend_settings_singleton; // global settings for all backends - readonly after setup.
	backend_vk_settings_interface_t le_backend_settings_i;
	allocator_linear_interface_t    le_allocator_linear_i;
	instance_interface_t            vk_instance_i;
	device_interface_t              vk_device_i;
	backend_vk_interface_t          vk_backend_i;
	le_pipeline_manager_interface_t le_pipeline_manager_i;
	shader_module_interface_t       le_shader_module_i;
	staging_allocator_interface_t   le_staging_allocator_i;

	private_backend_vk_interface_t          private_backend_vk_i;
	private_shader_file_watcher_interface_t private_shader_file_watcher_i;
};

LE_MODULE( le_backend_vk );
LE_MODULE_LOAD_DEFAULT( le_backend_vk );

#ifdef __cplusplus

namespace le_backend_vk {
static const auto api = le_backend_vk_api_i;

static const auto& settings_i             = api->le_backend_settings_i;
static const auto& vk_backend_i           = api->vk_backend_i;
static const auto& private_backend_vk_i   = api->private_backend_vk_i;
static const auto& le_allocator_linear_i  = api->le_allocator_linear_i;
static const auto& le_staging_allocator_i = api->le_staging_allocator_i;
static const auto& vk_instance_i          = api->vk_instance_i;
static const auto& vk_device_i            = api->vk_device_i;
static const auto& le_pipeline_manager_i  = api->le_pipeline_manager_i;
static const auto& le_shader_module_i     = api->le_shader_module_i;

} // namespace le_backend_vk

namespace le {

class Backend : NoCopy, NoMove {
	le_backend_o* self         = nullptr;
	bool          is_reference = false;

  public:
	operator auto() {
		return self;
	}

	Backend()
	    : self( le_backend_vk::vk_backend_i.create() )
	    , is_reference( false ) {
	}

	Backend( le_backend_o* ref )
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

	size_t getNumFrames() {
		return le_backend_vk::vk_backend_i.get_data_frames_count( self );
	}

	bool dispatchFrame( size_t frameIndex ) {
		return le_backend_vk::vk_backend_i.dispatch_frame( self, frameIndex );
	}
};

class Device : NoCopy, NoMove {
	le_device_o* self = nullptr;

  public:
	Device( le_backend_vk_instance_o* instance_, const char** extension_names, uint32_t extension_names_count )
	    : self( le_backend_vk::vk_device_i.create( instance_, extension_names, extension_names_count ) ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	~Device() {
		// Note: this will implicitly destroy once reference count hits 0.
		le_backend_vk::vk_device_i.decrease_reference_count( self );
	}

	// copy constructor
	Device( const Device& lhs )
	    : self( lhs.self ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	// reference from data constructor
	Device( le_device_o* device_ )
	    : self( device_ ) {
		le_backend_vk::vk_device_i.increase_reference_count( self );
	}

	VkDevice_T* getVkDevice() const {
		return le_backend_vk::vk_device_i.get_vk_device( self );
	}

	VkPhysicalDevice_T* getVkPhysicalDevice() const {
		return le_backend_vk::vk_device_i.get_vk_physical_device( self );
	}

	void getRaytracingProperties( struct VkPhysicalDeviceRayTracingPipelinePropertiesKHR* properties ) {
		le_backend_vk::vk_device_i.get_vk_physical_device_ray_tracing_properties( self, properties );
	}
	bool isExtensionAvailable( char const* extensionName ) const {
		return le_backend_vk::vk_device_i.is_extension_available( self, extensionName );
	}

	operator auto() {
		return self;
	}
};

} // namespace le
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H
