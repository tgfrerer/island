// these includes are only used for code completion
#include "le_backend_vk.h"
#include "le_hash_util.h"
#include "le_log.h"
#include <atomic>
#include <set>
#include <vector>
#include <string>
#include <vulkan/vulkan.h>
#include <cstring> // for memcpy

struct le_backend_vk_settings_o {
	std::set<std::string> required_instance_extensions_set; // we use set to give us permanent addresses for char*, and to ensure uniqueness of requested extensions
	std::set<std::string> required_device_extensions_set;   // we use set to give us permanent addresses for char*, and to ensure uniqueness of requested extensions
	                                                        //
	                                                        // we keep the sets in sync with the following two vectors, which point into the set contents for their char const *
	                                                        //
	std::vector<char const*> required_instance_extensions;  //
	std::vector<char const*> required_device_extensions;    //

	struct RequestedDeviceFeatures {
		VkPhysicalDeviceFeatures2                        features;
		VkPhysicalDeviceVulkan11Features                 vk_11;
		VkPhysicalDeviceVulkan12Features                 vk_12;
		VkPhysicalDeviceVulkan13Features                 vk_13;
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR    ray_tracing_pipeline;
		VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure;
		VkPhysicalDeviceMeshShaderFeaturesNV             mesh_shader;
	} requested_device_features;

	std::vector<VkQueueFlags> requested_queues_capabilities = {
	    VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
	    //	    VK_QUEUE_COMPUTE_BIT,
	}; // each entry stands for one queue and its capabilities

	uint32_t         data_frames_count = 2; // mumber of backend data frames - must be at minimum 2
	uint32_t         concurrency_count = 1; // number of potential worker threads
	std::atomic_bool readonly          = false;
};

static bool le_backend_vk_settings_set_requested_queue_capabilities( VkQueueFlags* queues, uint32_t num_queues ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( self->readonly == false && queues != nullptr && num_queues > 0 ) {
		self->requested_queues_capabilities.assign( queues, queues + num_queues );
		return true;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot set queue capabilities" );
		return false;
	}
}
static bool le_backend_vk_settings_add_requested_queue_capabilities( VkQueueFlags* queues, uint32_t num_queues ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( self->readonly == false && queues != nullptr && num_queues > 0 ) {
		self->requested_queues_capabilities.insert( self->requested_queues_capabilities.end(), queues, queues + num_queues );
		return true;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot set queue capabilities" );
		return false;
	}
}
static void le_backend_vk_settings_get_requested_queue_capabilities( VkQueueFlags* queues, uint32_t* num_queues ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( num_queues ) {
		*num_queues = uint32_t(self->requested_queues_capabilities.size());
	}
	if ( queues ) {
		memcpy( queues, self->requested_queues_capabilities.data(), self->requested_queues_capabilities.size() * sizeof( VkQueueFlags ) );
	}
}

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_required_instance_extension( le_backend_vk_settings_o* self, char const* ext ) {
	if ( self->readonly == false ) {
		auto const& [ str, was_inserted ] = self->required_instance_extensions_set.emplace( ext );
		if ( was_inserted ) {
			// only add to the vector if item was actually added. this is how we enforce that
			// elements in vector are unique.
			self->required_instance_extensions.push_back( str->c_str() );
		}
		return true;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot add required instance extension '%s'", ext );
		return false;
	}
}

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_required_device_extension( le_backend_vk_settings_o* self, char const* ext ) {
	if ( self->readonly == false ) {

		auto const& [ str, was_inserted ] = self->required_device_extensions_set.emplace( ext );
		if ( was_inserted ) {
			// only add to the vector if item was actually added. this is how we enforce that
			// elements in vector are unique.
			self->required_device_extensions.push_back( str->c_str() );
		}

		// Enable StorageBuffer16BitAccess if corresponding extension was requested.
		if ( std::string( ext ).find( VK_KHR_16BIT_STORAGE_EXTENSION_NAME ) != std::string::npos ) {
			self->requested_device_features.vk_11.storageBuffer16BitAccess = VK_TRUE;
			self->requested_device_features.features.features.shaderInt16  = VK_TRUE;
		}

		// we need timeline semaphores for multi-queue ops
		self->requested_device_features.vk_12.timelineSemaphore = VK_TRUE;

		return true;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot add required device extension '%s'", ext );
		return false;
	}
}
// ----------------------------------------------------------------------

static le_backend_vk_settings_o* le_backend_vk_settings_create() {

	le_backend_vk_settings_o* self = new le_backend_vk_settings_o{};

	self->requested_device_features.features = {
	    .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
	    .pNext    = &self->requested_device_features.vk_11, // optional
	    .features = {
	        .robustBufferAccess                      = 0,
	        .fullDrawIndexUint32                     = 0,
	        .imageCubeArray                          = 0,
	        .independentBlend                        = 0,
	        .geometryShader                          = VK_TRUE, // we want geometry shaders
	        .tessellationShader                      = 0,
	        .sampleRateShading                       = VK_TRUE, // so that we can use sampleShadingEnable
	        .dualSrcBlend                            = 0,
	        .logicOp                                 = 0,
	        .multiDrawIndirect                       = 0,
	        .drawIndirectFirstInstance               = 0,
	        .depthClamp                              = 0,
	        .depthBiasClamp                          = 0,
	        .fillModeNonSolid                        = VK_TRUE,
	        .depthBounds                             = 0,
	        .wideLines                               = VK_TRUE,
	        .largePoints                             = 0,
	        .alphaToOne                              = 0,
	        .multiViewport                           = 0,
	        .samplerAnisotropy                       = 0,
	        .textureCompressionETC2                  = 0,
	        .textureCompressionASTC_LDR              = 0,
	        .textureCompressionBC                    = 0,
	        .occlusionQueryPrecise                   = 0,
	        .pipelineStatisticsQuery                 = 0,
	        .vertexPipelineStoresAndAtomics          = VK_TRUE,
	        .fragmentStoresAndAtomics                = VK_TRUE,
	        .shaderTessellationAndGeometryPointSize  = 0,
	        .shaderImageGatherExtended               = 0,
	        .shaderStorageImageExtendedFormats       = 0,
	        .shaderStorageImageMultisample           = 0,
	        .shaderStorageImageReadWithoutFormat     = 0,
	        .shaderStorageImageWriteWithoutFormat    = 0,
	        .shaderUniformBufferArrayDynamicIndexing = 0,
	        .shaderSampledImageArrayDynamicIndexing  = 0,
	        .shaderStorageBufferArrayDynamicIndexing = 0,
	        .shaderStorageImageArrayDynamicIndexing  = 0,
	        .shaderClipDistance                      = 0,
	        .shaderCullDistance                      = 0,
	        .shaderFloat64                           = VK_TRUE,
	        .shaderInt64                             = VK_TRUE,
	        .shaderInt16                             = 0,
	        .shaderResourceResidency                 = 0,
	        .shaderResourceMinLod                    = 0,
	        .sparseBinding                           = 0,
	        .sparseResidencyBuffer                   = 0,
	        .sparseResidencyImage2D                  = 0,
	        .sparseResidencyImage3D                  = 0,
	        .sparseResidency2Samples                 = 0,
	        .sparseResidency4Samples                 = 0,
	        .sparseResidency8Samples                 = 0,
	        .sparseResidency16Samples                = 0,
	        .sparseResidencyAliased                  = 0,
	        .variableMultisampleRate                 = 0,
	        .inheritedQueries                        = 0,
	    },
	};
	self->requested_device_features.vk_11 = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
	    .pNext = &self->requested_device_features.vk_12, // optional
	};
	self->requested_device_features.vk_12 = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
	    .pNext = &self->requested_device_features.vk_13, // optional
	};
	self->requested_device_features.vk_13 = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
	    .pNext = &self->requested_device_features.ray_tracing_pipeline, // optional
	};
	self->requested_device_features.ray_tracing_pipeline = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
	    .pNext = &self->requested_device_features.acceleration_structure, // optional
	};
	self->requested_device_features.acceleration_structure = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
	    .pNext = &self->requested_device_features.mesh_shader, // optional
	};
	self->requested_device_features.mesh_shader = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV,
	    .pNext = nullptr, // optional
	};

	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME );

	// Apply some customisations

	self->requested_device_features.vk_13.synchronization2 = VK_TRUE; // use synchronisation2 by default

#ifdef LE_FEATURE_VIDEO
	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME );
	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME );
#endif

#ifdef LE_FEATURE_RTX
	self->requested_device_features.vk_12.bufferDeviceAddress                    = true; // needed for rtx
	self->requested_device_features.ray_tracing_pipeline.rayTracingPipeline      = true;
	self->requested_device_features.acceleration_structure.accelerationStructure = true;

	// request device extensions necessary for rtx
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_deferred_host_operations" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_ray_tracing_pipeline" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_acceleration_structure" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_pipeline_library" );
#endif

#ifdef LE_FEATURE_MESH_SHADER_NV

	self->requested_device_features.mesh_shader.meshShader = true;
	self->requested_device_features.mesh_shader.taskShader = true;

	// We require 8 bit integers, and 16 bit floats for when we use mesh shaders -
	// because most use cases will want to make use of these.

	self->requested_device_features.vk_12.shaderInt8    = true;
	self->requested_device_features.vk_12.shaderFloat16 = true;
#endif

	return self;
};

// ----------------------------------------------------------------------

static void le_backend_vk_settings_destroy( le_backend_vk_settings_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_required_instance_extension( char const* ext ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	return le_backend_vk_settings_add_required_instance_extension( self, ext );
}

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_required_device_extension( char const* ext ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	return le_backend_vk_settings_add_required_device_extension( self, ext );
}

// ----------------------------------------------------------------------

static void le_backend_vk_settings_set_concurrency_count( uint32_t concurrency_count ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	self->concurrency_count        = concurrency_count;
}

// ----------------------------------------------------------------------
static bool le_backend_vk_settings_set_data_frames_count( uint32_t data_frames_count ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( self->readonly ) {
		return false;
	}
	// ----------| invariant: settings is not readonly
	self->data_frames_count        = data_frames_count;
	return true;
}

// ----------------------------------------------------------------------

static VkPhysicalDeviceFeatures2 const* le_backend_vk_get_requested_physical_device_features_chain() {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	return reinterpret_cast<VkPhysicalDeviceFeatures2 const*>( &self->requested_device_features.features );
}
