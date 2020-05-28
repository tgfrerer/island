#include "le_backend_vk/le_backend_vk.h"

#define VULKAN_HPP_DISABLE_IMPLICIT_RESULT_VALUE_CAST
#include <vulkan/vulkan.hpp>

#include <iostream>
#include <iomanip>
#include <set>
#include <string>

// Automatically disable Validation Layers for Release Builds

#ifndef SHOULD_USE_VALIDATION_LAYERS
#	ifdef NDEBUG
#		define SHOULD_USE_VALIDATION_LAYERS false
#	else
#		define SHOULD_USE_VALIDATION_LAYERS true
#	endif
#endif

// ----------------------------------------------------------------------

struct le_backend_vk_instance_o {
	vk::Instance               vkInstance     = nullptr;
	vk::DebugUtilsMessengerEXT debugMessenger = nullptr;

	std::set<std::string> instanceExtensionSet{};
};

/*
 * Specify which validation layers to enable within Khronos validation
 * layer. (The following are otherwise disabled by default)
 * 
 */
static const vk::ValidationFeatureEnableEXT enabledValidationFeatures[] = {
    // vk::ValidationFeatureEnableEXT::eGpuAssisted,
    // vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
    vk::ValidationFeatureEnableEXT::eBestPractices,
};

/* 
 * Specify which validation layers to disable within Khronos validation
 * layer. (The following are otherwise enabled by default)
 * 
 */
static const vk::ValidationFeatureDisableEXT disabledValidationFeatures[] = {
    // vk::ValidationFeatureDisableEXT::eAll,
    // vk::ValidationFeatureDisableEXT::eShaders,
    // vk::ValidationFeatureDisableEXT::eThreadSafety,
    // vk::ValidationFeatureDisableEXT::eApiParameters,
    // vk::ValidationFeatureDisableEXT::eObjectLifetimes,
    // vk::ValidationFeatureDisableEXT::eCoreChecks,
    vk::ValidationFeatureDisableEXT::eUniqueHandles,
};

// ----------------------------------------------------------------------

static bool instance_is_extension_available( le_backend_vk_instance_o *self, char const *extension_name ); //ffdecl

// ----------------------------------------------------------------------

#define DECLARE_EXT_PFN( proc ) \
	static PFN_##proc pfn_##proc;

// instance extensions
DECLARE_EXT_PFN( vkCreateDebugUtilsMessengerEXT );
DECLARE_EXT_PFN( vkDestroyDebugUtilsMessengerEXT );

DECLARE_EXT_PFN( vkSetDebugUtilsObjectNameEXT );

// device extensions - for ray tracing
#ifdef LE_FEATURE_RTX
DECLARE_EXT_PFN( vkGetBufferDeviceAddress );
DECLARE_EXT_PFN( vkCreateAccelerationStructureKHR );
DECLARE_EXT_PFN( vkGetAccelerationStructureMemoryRequirementsKHR );
DECLARE_EXT_PFN( vkBindAccelerationStructureMemoryKHR );
DECLARE_EXT_PFN( vkDestroyAccelerationStructureKHR );
DECLARE_EXT_PFN( vkCmdBuildAccelerationStructureKHR );
DECLARE_EXT_PFN( vkGetAccelerationStructureDeviceAddressKHR );
DECLARE_EXT_PFN( vkCreateRayTracingPipelinesKHR );
DECLARE_EXT_PFN( vkGetRayTracingShaderGroupHandlesKHR );
DECLARE_EXT_PFN( vkCmdTraceRaysKHR );
#endif

#ifdef LE_FEATURE_MESH_SHADER_NV
// device extensions for mesh shaders
DECLARE_EXT_PFN( vkCmdDrawMeshTasksNV );
DECLARE_EXT_PFN( vkCmdDrawMeshTasksIndirectNV );
DECLARE_EXT_PFN( vkCmdDrawMeshTasksIndirectCountNV );
#endif

#undef DECLARE_EXT_PFN

// ----------------------------------------------------------------------

static void patchExtProcAddrs( le_backend_vk_instance_o *obj ) {

	// Note that we get proc addresses via the instance for instance extensions as
	// well as for device extensions. If a device extension gets called via an instance,
	// this means that the dispatcher deals with calling the device specific function.
	//
	// We could store device specific functions in the device and call the direct functions
	// via device, but that would be very cumbersome, as we would have to re-expose all
	// relevant device function signatures via some sort of private (backend-internal)
	// interface.
	//
	// The advantage of fetching the device specific function pointers are that it's
	// quicker to call without having to go through the dispatcher. That's why libraries
	// such as volk exist <https://github.com/zeux/volk>, but they don't interact nicely
	// with vulkan-hpp as far as i can see.
	//
	// Since most methods that we call are goint to be beefy (like creating
	// acceleration structures and the like, we don't think it's worth the additional
	// complexity to re-expose the vulkan headers internally. We might revisit that
	// once we have fallen out of love with vulkan-hpp, and using volk becomes practical.

#define GET_EXT_PROC_ADDR( proc ) \
	pfn_##proc = reinterpret_cast<PFN_##proc>( obj->vkInstance.getProcAddr( #proc ) )

	if ( instance_is_extension_available( obj, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
		GET_EXT_PROC_ADDR( vkSetDebugUtilsObjectNameEXT );
		GET_EXT_PROC_ADDR( vkCreateDebugUtilsMessengerEXT );
		GET_EXT_PROC_ADDR( vkDestroyDebugUtilsMessengerEXT );
	}

	// device extensions for ray tracing
#ifdef LE_FEATURE_RTX
	GET_EXT_PROC_ADDR( vkGetBufferDeviceAddress );
	GET_EXT_PROC_ADDR( vkCreateAccelerationStructureKHR );
	GET_EXT_PROC_ADDR( vkGetAccelerationStructureMemoryRequirementsKHR );
	GET_EXT_PROC_ADDR( vkBindAccelerationStructureMemoryKHR );
	GET_EXT_PROC_ADDR( vkDestroyAccelerationStructureKHR );
	GET_EXT_PROC_ADDR( vkCmdBuildAccelerationStructureKHR );
	GET_EXT_PROC_ADDR( vkGetAccelerationStructureDeviceAddressKHR );
	GET_EXT_PROC_ADDR( vkCreateRayTracingPipelinesKHR );
	GET_EXT_PROC_ADDR( vkGetRayTracingShaderGroupHandlesKHR );
	GET_EXT_PROC_ADDR( vkCmdTraceRaysKHR );
#endif

	// device extensions for task/mesh shaders
#ifdef LE_FEATURE_MESH_SHADER_NV

	GET_EXT_PROC_ADDR( vkCmdDrawMeshTasksNV );
	GET_EXT_PROC_ADDR( vkCmdDrawMeshTasksIndirectNV );
	GET_EXT_PROC_ADDR( vkCmdDrawMeshTasksIndirectCountNV );
#endif

#undef GET_EXT_PROC_ADDR

	std::cout << "Patched proc addrs." << std::endl;
}

// ----------------------------------------------------------------------
// These method definitions are exported so that vkhpp can call them
// vkhpp has matching declarations
// - Note that these methods are not defined as `extern` -
// their linkage *must not* be local, so that they can be called
// from other translation units.

VkResult vkSetDebugUtilsObjectNameEXT(
    VkDevice                             device,
    const VkDebugUtilsObjectNameInfoEXT *pNameInfo ) {
	return pfn_vkSetDebugUtilsObjectNameEXT( device, pNameInfo );
}
VkResult vkCreateDebugUtilsMessengerEXT(
    VkInstance                                instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *             pAllocator,
    VkDebugUtilsMessengerEXT *                pMessenger ) {
	return pfn_vkCreateDebugUtilsMessengerEXT( instance, pCreateInfo, pAllocator, pMessenger );
}
void vkDestroyDebugUtilsMessengerEXT(
    VkInstance                   instance,
    VkDebugUtilsMessengerEXT     messenger,
    const VkAllocationCallbacks *pAllocator ) {
	pfn_vkDestroyDebugUtilsMessengerEXT( instance, messenger, pAllocator );
}

#ifdef LE_FEATURE_RTX

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(
    VkDevice                         device,
    const VkBufferDeviceAddressInfo *pInfo ) {
	return pfn_vkGetBufferDeviceAddress( device, pInfo );
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(
    VkDevice                                    device,
    const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *               pAllocator,
    VkAccelerationStructureKHR *                pAccelerationStructure ) {
	return pfn_vkCreateAccelerationStructureKHR( device, pCreateInfo, pAllocator, pAccelerationStructure );
};

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureMemoryRequirementsKHR(
    VkDevice                                                device,
    const VkAccelerationStructureMemoryRequirementsInfoKHR *pInfo,
    VkMemoryRequirements2KHR *                              pMemoryRequirements ) {
	return pfn_vkGetAccelerationStructureMemoryRequirementsKHR( device, pInfo, pMemoryRequirements );
};

VKAPI_ATTR VkResult VKAPI_CALL vkBindAccelerationStructureMemoryKHR(
    VkDevice                                        device,
    uint32_t                                        bindInfoCount,
    const VkBindAccelerationStructureMemoryInfoKHR *pBindInfos ) {
	return pfn_vkBindAccelerationStructureMemoryKHR( device, bindInfoCount, pBindInfos );
};

VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(
    VkDevice                     device,
    VkAccelerationStructureKHR   accelerationStructure,
    const VkAllocationCallbacks *pAllocator ) {
	pfn_vkDestroyAccelerationStructureKHR( device, accelerationStructure, pAllocator );
};

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructureKHR(
    VkCommandBuffer                                         commandBuffer,
    uint32_t                                                infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR *     pInfos,
    const VkAccelerationStructureBuildOffsetInfoKHR *const *ppOffsetInfos ) {
	return pfn_vkCmdBuildAccelerationStructureKHR( commandBuffer, infoCount, pInfos, ppOffsetInfos );
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(
    VkDevice                                           device,
    const VkAccelerationStructureDeviceAddressInfoKHR *pInfo ) {
	return pfn_vkGetAccelerationStructureDeviceAddressKHR( device, pInfo );
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(
    VkDevice                                 device,
    VkPipelineCache                          pipelineCache,
    uint32_t                                 createInfoCount,
    const VkRayTracingPipelineCreateInfoKHR *pCreateInfos,
    const VkAllocationCallbacks *            pAllocator,
    VkPipeline *                             pPipelines ) {
	return pfn_vkCreateRayTracingPipelinesKHR( device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines );
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(
    VkDevice   device,
    VkPipeline pipeline,
    uint32_t   firstGroup,
    uint32_t   groupCount,
    size_t     dataSize,
    void *     pData ) {
	return pfn_vkGetRayTracingShaderGroupHandlesKHR( device, pipeline, firstGroup, groupCount, dataSize, pData );
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(
    VkCommandBuffer                 commandBuffer,
    const VkStridedBufferRegionKHR *pRaygenShaderBindingTable,
    const VkStridedBufferRegionKHR *pMissShaderBindingTable,
    const VkStridedBufferRegionKHR *pHitShaderBindingTable,
    const VkStridedBufferRegionKHR *pCallableShaderBindingTable,
    uint32_t                        width,
    uint32_t                        height,
    uint32_t                        depth ) {
	return pfn_vkCmdTraceRaysKHR( commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth );
}
#endif

#ifdef LE_FEATURE_MESH_SHADER_NV // mesh shader method prototypes

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksNV(
    VkCommandBuffer commandBuffer,
    uint32_t        taskCount,
    uint32_t        firstTask ) {
	pfn_vkCmdDrawMeshTasksNV( commandBuffer, taskCount, firstTask );
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectNV(
    VkCommandBuffer commandBuffer,
    VkBuffer        buffer,
    VkDeviceSize    offset,
    uint32_t        drawCount,
    uint32_t        stride ) {
	pfn_vkCmdDrawMeshTasksIndirectNV( commandBuffer, buffer, offset, drawCount, stride );
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountNV(
    VkCommandBuffer commandBuffer,
    VkBuffer        buffer,
    VkDeviceSize    offset,
    VkBuffer        countBuffer,
    VkDeviceSize    countBufferOffset,
    uint32_t        maxDrawCount,
    uint32_t        stride ) {
	pfn_vkCmdDrawMeshTasksIndirectCountNV( commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride );
}
#endif
// ----------------------------------------------------------------------

static void create_debug_messenger_callback( le_backend_vk_instance_o *obj );  // ffdecl.
static void destroy_debug_messenger_callback( le_backend_vk_instance_o *obj ); // ffdecl.

static VkBool32 debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void * ) {

	bool shouldBailout = VK_FALSE;

	std::string logLevel = "";
	std::string msgType  = "";

	if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT ) {
		logLevel = "INFO";
	} else if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT ) {
		logLevel = "VERBOSE";
	} else if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
		logLevel = "WARNING";
	} else if ( messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		logLevel = "ERROR";
	}

	if ( messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ) {
		msgType = "GENERAL";
	} else if ( messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ) {
		msgType = "VALIDATION";
	} else if ( messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT ) {
		msgType = "PERF";
		return false;
	}

	if ( messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		shouldBailout |= VK_TRUE;
	}

	std::ostringstream os;
	os << "[ " << std::left << std::setw( 10 ) << msgType << " | " << std::setw( 7 ) << logLevel << " ] " << pCallbackData->pMessage << std::endl;
	std::cout << os.str();
	std::cout << std::flush;

	return shouldBailout;
}
// ----------------------------------------------------------------------

static void create_debug_messenger_callback( le_backend_vk_instance_o *obj ) {

	if ( !SHOULD_USE_VALIDATION_LAYERS ) {
		return;
	}

	if ( !instance_is_extension_available( obj, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
		return;
	}

	vk::ValidationFeaturesEXT validationFeatures;
	validationFeatures
	    .setPNext( nullptr )
	    .setEnabledValidationFeatureCount( uint32_t( sizeof( enabledValidationFeatures ) / sizeof( vk::ValidationFeatureEnableEXT ) ) )
	    .setPEnabledValidationFeatures( enabledValidationFeatures )
	    .setDisabledValidationFeatureCount( uint32_t( sizeof( disabledValidationFeatures ) / sizeof( vk::ValidationFeatureDisableEXT ) ) )
	    .setPDisabledValidationFeatures( disabledValidationFeatures );

	vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo;
	debugMessengerCreateInfo
	    .setPNext( SHOULD_USE_VALIDATION_LAYERS ? &validationFeatures : nullptr )
	    .setFlags( {} )
	    .setMessageSeverity( ~vk::DebugUtilsMessageSeverityFlagsEXT() ) // everything.
	    .setMessageType( ~vk::DebugUtilsMessageTypeFlagsEXT() )         // everything.
	    .setPfnUserCallback( debugUtilsMessengerCallback )
	    .setPUserData( nullptr );

	obj->debugMessenger = obj->vkInstance.createDebugUtilsMessengerEXT( debugMessengerCreateInfo );
}

// ----------------------------------------------------------------------

static void destroy_debug_messenger_callback( le_backend_vk_instance_o *obj ) {

	if ( !SHOULD_USE_VALIDATION_LAYERS ) {
		return;
	}

	if ( !instance_is_extension_available( obj, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
		return;
	}

	obj->vkInstance.destroyDebugUtilsMessengerEXT( obj->debugMessenger );

	obj->debugMessenger = nullptr;
}

// ----------------------------------------------------------------------

le_backend_vk_instance_o *instance_create( const char **extensionNamesArray_, uint32_t numExtensionNames_ ) {

	auto self = new le_backend_vk_instance_o();

	static_assert( VK_HEADER_VERSION >= 121, "Wrong VK_HEADER_VERSION!" );

	vk::ApplicationInfo appInfo;
	appInfo
	    .setPApplicationName( "Island App" )
	    .setApplicationVersion( VK_MAKE_VERSION( 0, 0, 0 ) )
	    .setPEngineName( "Island" )
	    .setEngineVersion( VK_MAKE_VERSION( 0, 1, 0 ) )
	    .setApiVersion( VK_MAKE_VERSION( 1, 2, 135 ) );

	// -- create a vector of unique requested instance extension names

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		self->instanceExtensionSet.insert( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
	}

	// Merge with user-requested extensions
	for ( uint32_t i = 0; i != numExtensionNames_; ++i ) {
		self->instanceExtensionSet.insert( extensionNamesArray_[ i ] );
	}

	std::vector<const char *> instanceExtensionCstr{};

	for ( auto &e : self->instanceExtensionSet ) {
		instanceExtensionCstr.push_back( e.c_str() );
	}

	// -- Create a vector of requested instance layers

	std::vector<const char *> instanceLayerNames = {};


	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		instanceLayerNames.push_back( "VK_LAYER_KHRONOS_validation" );
		std::cout << "Debug instance layers added." << std::endl;
	}


	vk::InstanceCreateInfo info;
	info.setFlags( {} )
	    .setPNext( nullptr ) // We add a debug messenger object to instance creation to get creation-time debug info
	    .setPApplicationInfo( &appInfo )
	    .setEnabledLayerCount( uint32_t( instanceLayerNames.size() ) )
	    .setPpEnabledLayerNames( instanceLayerNames.data() )
	    .setEnabledExtensionCount( uint32_t( instanceExtensionCstr.size() ) )
	    .setPpEnabledExtensionNames( instanceExtensionCstr.data() );

	self->vkInstance = vk::createInstance( info );

	le_backend_vk::api->cUniqueInstance = self;

	patchExtProcAddrs( self );

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		create_debug_messenger_callback( self );
		std::cout << "VULKAN VALIDATION LAYERS ACTIVE." << std::endl;
	}

	std::cout << "Instance created." << std::endl;
	return self;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_backend_vk_instance_o *obj ) {
	destroy_debug_messenger_callback( obj );
	obj->vkInstance.destroy();
	delete ( obj );
	std::cout << "Instance destroyed." << std::endl
	          << std::flush;
}

// ----------------------------------------------------------------------

static VkInstance_T *instance_get_vk_instance( le_backend_vk_instance_o *obj ) {
	return ( reinterpret_cast<VkInstance &>( obj->vkInstance ) );
}

// ----------------------------------------------------------------------

static bool instance_is_extension_available( le_backend_vk_instance_o *self, char const *extension_name ) {
	return self->instanceExtensionSet.find( extension_name ) != self->instanceExtensionSet.end();
}

// ----------------------------------------------------------------------

static void instance_post_reload_hook( le_backend_vk_instance_o *obj ) {
	std::cout << "** post reload hook triggered." << std::endl;
	patchExtProcAddrs( obj );
	destroy_debug_messenger_callback( obj );
	std::cout << "** Removed debug report callback." << std::endl;
	create_debug_messenger_callback( obj );
	std::cout << "** Added new debug report callback." << std::endl;
}

// ----------------------------------------------------------------------

void register_le_instance_vk_api( void *api_ ) {
	auto  api_i      = static_cast<le_backend_vk_api *>( api_ );
	auto &instance_i = api_i->vk_instance_i;

	instance_i.create                 = instance_create;
	instance_i.destroy                = instance_destroy;
	instance_i.get_vk_instance        = instance_get_vk_instance;
	instance_i.post_reload_hook       = instance_post_reload_hook;
	instance_i.is_extension_available = instance_is_extension_available;
}
