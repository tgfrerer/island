// these includes are only used for code completion
#include "le_backend_vk.h"
#include "le_hash_util.h"
#include "le_log.h"
#include <atomic>
#include <set>
#include <vector>
#include <string>
#include <vulkan/vulkan.hpp>

struct le_backend_vk_settings_o {
	std::set<std::string> required_instance_extensions_set; // we use set to give us permanent addresses for char*, and to ensure uniqueness of requested extensions
	std::set<std::string> required_device_extensions_set;   // we use set to give us permanent addresses for char*, and to ensure uniqueness of requested extensions
	                                                        //
	// we keep the sets in sync with the following two vectors, which point into the set contents for their char const *
	//
	std::vector<char const*> required_instance_extensions;
	std::vector<char const*> required_device_extensions;

	vk::StructureChain<
	    vk::PhysicalDeviceFeatures2,
	    vk::PhysicalDeviceVulkan11Features,
	    vk::PhysicalDeviceVulkan12Features,
	    vk::PhysicalDeviceVulkan13Features,
	    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
	    vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
	    vk::PhysicalDeviceMeshShaderFeaturesNV>

	    requested_device_features = {};

	std::vector<le_swapchain_settings_t> swapchain_settings;
	uint32_t                             concurrency_count = 1; // number of potential worker threads
	std::atomic_bool                     readonly          = false;
};

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_required_instance_extension( le_backend_vk_settings_o* self, char const* ext ) {
	if ( self->readonly == false ) {
		auto result = self->required_instance_extensions_set.emplace( ext );
		if ( result.second ) {
			// only add to the vector if item was actually added. this is how we enforce that
			// elements in vector are unique.
			self->required_instance_extensions.push_back( result.first->c_str() );
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

		auto result = self->required_device_extensions_set.emplace( ext );
		if ( result.second ) {
			// only add to the vector if item was actually added. this is how we enforce that
			// elements in vector are unique.
			self->required_device_extensions.push_back( result.first->c_str() );
		}

		// Enable StorageBuffer16BitAccess if corresponding extension was requested.
		if ( std::string( ext ).find( VK_KHR_16BIT_STORAGE_EXTENSION_NAME ) != std::string::npos ) {
			self->requested_device_features.get<vk::PhysicalDeviceVulkan11Features>()
			    .setStorageBuffer16BitAccess( true ) //
			    ;
		}

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

	self->requested_device_features.get<vk::PhysicalDeviceFeatures2>()
	    .features                                  //
	    .setFillModeNonSolid( true )               // allow drawing as wireframe
	    .setWideLines( true )                      // require enable wide lines
	    .setRobustBufferAccess( false )            // disable robust buffer access
	    .setVertexPipelineStoresAndAtomics( true ) //
	    .setFragmentStoresAndAtomics( true )       //
	    .setSampleRateShading( true )              // enable so that we can use sampleShadingEnable
	    .setGeometryShader( true )                 // we want geometry shaders
	    .setShaderInt16( true )                    //
	    .setShaderFloat64( true )                  //
	    ;

#ifdef LE_FEATURE_VIDEO
	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME );
	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME );
	le_backend_vk_settings_add_required_device_extension( self, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME );
#endif

#ifdef LE_FEATURE_RTX
	self->requested_device_features.get<vk::PhysicalDeviceVulkan12Features>()
	    .setBufferDeviceAddress( true ) // needed for rtx
	    //	    .setBufferDeviceAddressCaptureReplay( true ) // needed for frame debuggers, when using bufferDeviceAddress
	    ;

	self->requested_device_features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>()
	    .setRayTracingPipeline( true );

	self->requested_device_features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>()
	    .setAccelerationStructure( true );

	// request device extensions necessary for rtx
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_deferred_host_operations" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_ray_tracing_pipeline" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_acceleration_structure" );
	le_backend_vk_settings_add_required_device_extension( self, "VK_KHR_pipeline_library" );
#endif

#ifdef LE_FEATURE_MESH_SHADER_NV

	self->requested_device_features.get<vk::PhysicalDeviceMeshShaderFeaturesNV>()
	    .setMeshShader( true )
	    .setTaskShader( true );

	// We require 8 bit integers, and 16 bit floats for when we use mesh shaders -
	// because most use cases will want to make use of these.

	self->requested_device_features.get<vk::PhysicalDeviceVulkan12Features>()
	    .setShaderInt8( true )    //
	    .setShaderFloat16( true ) //
	    ;
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

static bool le_backend_vk_settings_add_swapchain_setting( le_swapchain_settings_t const* settings ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( self->readonly == false ) {
		self->swapchain_settings.emplace_back( *settings );
		return true;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot add Swapchain setting" );
		return false;
	}
}

// ----------------------------------------------------------------------

static VkPhysicalDeviceFeatures2 const* le_backend_vk_get_requested_physical_device_features_chain() {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	return reinterpret_cast<VkPhysicalDeviceFeatures2 const*>( &self->requested_device_features.get<vk::PhysicalDeviceFeatures2>() );
}