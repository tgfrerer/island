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

	struct PhysicalDeviceFeatures {
		VkPhysicalDeviceFeatures2                        features;
		VkPhysicalDeviceVulkan11Features                 vk_1_1;
		VkPhysicalDeviceVulkan12Features                 vk_1_2;
		VkPhysicalDeviceVulkan13Features                 vk_1_3;
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR    ray_tracing_pipeline;
		VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure;
		VkPhysicalDeviceMeshShaderFeaturesNV             mesh_shader;
	} physical_device_features;

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
		*num_queues = uint32_t( self->requested_queues_capabilities.size() );
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

		return true;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot add required device extension '%s'", ext );
		return false;
	}
}

// ----------------------------------------------------------------------

struct GenericVkStruct {
	VkStructureType sType;
	void*           pNext;
};

// ----------------------------------------------------------------------
// Search for a struct in the chain that has the type given as s_type
// if nothing could be found, then returns nullptr
// p_previous will hold the last valid entry in the chain.
//
static GenericVkStruct* find_in_features_chain( GenericVkStruct* vk_features_chain, VkStructureType const s_type, GenericVkStruct** p_previous = nullptr ) {

	GenericVkStruct* p_current = vk_features_chain;

	// Test whether a struct of the type that we require already exists.
	while ( p_current != nullptr ) {

		if ( p_current->sType == s_type ) {
			return p_current;
		}
		if ( p_previous ) {
			// store the last element of the
			*p_previous = p_current;
		}
		p_current = reinterpret_cast<GenericVkStruct*>( p_current->pNext );
	}
	return p_current;
};

// ----------------------------------------------------------------------

// Finds or inserts link with link->sType into vk_struct_chain.
//
// Element must contain valid sType
//
// Returns pointer to inserted or found link - if return pointer is different
// to given link, this means that instead of adding a new link, an
// existing link was found, and returned.
//
// Ownership is unchanged - the linked list does not own any of its elements.
//
static GenericVkStruct* fetch_or_insert_chain_link( GenericVkStruct* vk_struct_chain, GenericVkStruct* link ) {

	if ( vk_struct_chain == nullptr ) {
		return nullptr;
	}

	// -----------| invariant: vk_features_chain is valid

	GenericVkStruct* p_current  = vk_struct_chain;
	GenericVkStruct* p_previous = nullptr;

	while ( p_current ) {

		// find matching element
		if ( p_current->sType == link->sType ) {
			return link;
		}

		p_previous = p_current;
		p_current  = ( GenericVkStruct* )p_current->pNext;
	}

	// ---------- | invariant: no element found,
	// 				p_previous holds the last valid element

	// Append element as the last element.
	p_previous->pNext = link;

	return ( GenericVkStruct* )p_previous->pNext;
}

// templated version - not for public use
template <typename T>
static T* fetch_or_insert_chain_link( GenericVkStruct* vk_struct_chain, T* link ) {
	return ( T* )fetch_or_insert_chain_link( vk_struct_chain, ( GenericVkStruct* )link );
}

// ----------------------------------------------------------------------

static le_backend_vk_settings_o* le_backend_vk_settings_create() {

	le_backend_vk_settings_o* self = new le_backend_vk_settings_o{};

	{
		self->physical_device_features.features = {
		    .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		    .pNext    = &self->physical_device_features.vk_1_1, // optional
		    .features = {
		        .robustBufferAccess                      = 0,
		        .fullDrawIndexUint32                     = 0,
		        .imageCubeArray                          = 0,
		        .independentBlend                        = VK_TRUE, // VULKAN ROADMAP 2022: enable independent blend
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

		// Initialise bare minimum of features structs - their sType. And everything else to zero:
		self->physical_device_features.vk_1_1       = {};
		self->physical_device_features.vk_1_1.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

		self->physical_device_features.vk_1_2       = {};
		self->physical_device_features.vk_1_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

		self->physical_device_features.vk_1_3       = {};
		self->physical_device_features.vk_1_3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	}

	// Setup the features chain
	GenericVkStruct* features_chain = reinterpret_cast<GenericVkStruct*>( &self->physical_device_features );

	// Add links to the features chain
	auto vk_11_features = fetch_or_insert_chain_link( features_chain, &self->physical_device_features.vk_1_1 );
	auto vk_12_features = fetch_or_insert_chain_link( features_chain, &self->physical_device_features.vk_1_2 );
	auto vk_13_features = fetch_or_insert_chain_link( features_chain, &self->physical_device_features.vk_1_3 );

	// ----------------------------------------------------------------------
	// Enable some default features that we don't want to live without
	// ----------------------------------------------------------------------

	// GENERALLY, we deem it safe to enable any features that are part of
	// the ROADMAP 2022 Profile.
	// See <https://docs.vulkan.org/spec/latest/appendices/roadmap.html#roadmap-2022>

	vk_11_features->samplerYcbcrConversion = VK_TRUE; // needed for video decoding pipeline
	vk_12_features->timelineSemaphore      = VK_TRUE; // needed for cross-queue synchronisation
	vk_13_features->synchronization2       = VK_TRUE; // use synchronisation2 by default

	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME );
	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME );

#ifdef LE_FEATURE_RTX

	self->physical_device_features.ray_tracing_pipeline.sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	self->physical_device_features.acceleration_structure.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

	auto rtx_features                    = fetch_or_insert_chain_link( features_chain, &self->physical_device_features.ray_tracing_pipeline );
	auto acceleration_structure_features = fetch_or_insert_chain_link( features_chain, &self->physical_device_features.acceleration_structure );

	rtx_features->rayTracingPipeline                       = VK_TRUE;
	acceleration_structure_features->accelerationStructure = VK_TRUE;
	vk_12_features->bufferDeviceAddress                    = VK_TRUE; // requirement for rtx

	// Request device extensions that are required for Ray Tracing
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_deferred_host_operations" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_ray_tracing_pipeline" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_acceleration_structure" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_pipeline_library" );
#endif

#ifdef LE_FEATURE_MESH_SHADER_NV

	self->physical_device_features.mesh_shader.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV;

	auto mesh_shader_features = fetch_or_insert_chain_link( features_chain, &self->physical_device_features.mesh_shader );

	mesh_shader_features->meshShader = VK_TRUE;
	mesh_shader_features->taskShader = VK_TRUE;

	// We require 8 bit integers, and 16 bit floats for when we use mesh shaders -
	// because most use cases will want to make use of these.

	vk_12_features->shaderInt8    = VK_TRUE;
	vk_12_features->shaderFloat16 = VK_TRUE;
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
	self->data_frames_count = data_frames_count;
	return true;
}

// ----------------------------------------------------------------------

static VkPhysicalDeviceFeatures2* le_backend_vk_get_physical_device_features_chain() {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	return reinterpret_cast<VkPhysicalDeviceFeatures2*>( &self->physical_device_features.features );
}
