#include "le_core.h"
#include "le_backend_vk.h"
#include "le_backend_types_internal.h"
#include "le_log.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <string>

static constexpr auto LOGGER_LABEL = "le_instance_vk";

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
//static const vk::ValidationFeatureEnableEXT enabledValidationFeatures[] = {
// vk::ValidationFeatureEnableEXT::eGpuAssisted,
// vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
// vk::ValidationFeatureEnableEXT::eBestPractices,
// vk::ValidationFeatureEnableEXT::eDebugPrintf,
//};

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
DECLARE_EXT_PFN( vkSetDebugUtilsObjectTagEXT );
DECLARE_EXT_PFN( vkQueueBeginDebugUtilsLabelEXT );
DECLARE_EXT_PFN( vkQueueEndDebugUtilsLabelEXT );
DECLARE_EXT_PFN( vkQueueInsertDebugUtilsLabelEXT );
DECLARE_EXT_PFN( vkCmdBeginDebugUtilsLabelEXT );
DECLARE_EXT_PFN( vkCmdEndDebugUtilsLabelEXT );
DECLARE_EXT_PFN( vkCmdInsertDebugUtilsLabelEXT );

DECLARE_EXT_PFN( vkSetDebugUtilsObjectNameEXT );

// device extensions - for ray tracing
#ifdef LE_FEATURE_RTX
//DECLARE_EXT_PFN( vkGetBufferDeviceAddress );
DECLARE_EXT_PFN( vkCreateAccelerationStructureKHR );
DECLARE_EXT_PFN( vkCmdBuildAccelerationStructuresKHR );
DECLARE_EXT_PFN( vkDestroyAccelerationStructureKHR );
DECLARE_EXT_PFN( vkGetAccelerationStructureDeviceAddressKHR );

DECLARE_EXT_PFN( vkGetAccelerationStructureBuildSizesKHR );
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
		GET_EXT_PROC_ADDR( vkSetDebugUtilsObjectTagEXT );
		GET_EXT_PROC_ADDR( vkQueueBeginDebugUtilsLabelEXT );
		GET_EXT_PROC_ADDR( vkQueueEndDebugUtilsLabelEXT );
		GET_EXT_PROC_ADDR( vkQueueInsertDebugUtilsLabelEXT );
		GET_EXT_PROC_ADDR( vkCmdBeginDebugUtilsLabelEXT );
		GET_EXT_PROC_ADDR( vkCmdEndDebugUtilsLabelEXT );
		GET_EXT_PROC_ADDR( vkCmdInsertDebugUtilsLabelEXT );
	}

	// device extensions for ray tracing
#ifdef LE_FEATURE_RTX
	GET_EXT_PROC_ADDR( vkCmdBuildAccelerationStructuresKHR );
	GET_EXT_PROC_ADDR( vkCreateAccelerationStructureKHR );
	GET_EXT_PROC_ADDR( vkDestroyAccelerationStructureKHR );
	GET_EXT_PROC_ADDR( vkGetAccelerationStructureDeviceAddressKHR );
	GET_EXT_PROC_ADDR( vkGetAccelerationStructureBuildSizesKHR );
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

	static auto logger = LeLog( LOGGER_LABEL );
	logger.debug( "Patched proc addrs." );
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
VkResult vkSetDebugUtilsObjectTagEXT(
    VkDevice                            device,
    const VkDebugUtilsObjectTagInfoEXT *pTagInfo ) {
	return pfn_vkSetDebugUtilsObjectTagEXT( device, pTagInfo );
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
void vkQueueBeginDebugUtilsLabelEXT(
    VkQueue                     queue,
    const VkDebugUtilsLabelEXT *pLabelInfo ) {
	pfn_vkQueueBeginDebugUtilsLabelEXT( queue, pLabelInfo );
}
void vkQueueEndDebugUtilsLabelEXT(
    VkQueue queue ) {
	pfn_vkQueueEndDebugUtilsLabelEXT( queue );
}
void vkQueueInsertDebugUtilsLabelEXT(
    VkQueue                     queue,
    const VkDebugUtilsLabelEXT *pLabelInfo ) {
	pfn_vkQueueInsertDebugUtilsLabelEXT( queue, pLabelInfo );
}
void vkCmdBeginDebugUtilsLabelEXT(
    VkCommandBuffer             commandBuffer,
    const VkDebugUtilsLabelEXT *pLabelInfo ) {
	pfn_vkCmdBeginDebugUtilsLabelEXT( commandBuffer, pLabelInfo );
}
void vkCmdEndDebugUtilsLabelEXT(
    VkCommandBuffer commandBuffer ) {
	pfn_vkCmdEndDebugUtilsLabelEXT( commandBuffer );
}
void vkCmdInsertDebugUtilsLabelEXT(
    VkCommandBuffer             commandBuffer,
    const VkDebugUtilsLabelEXT *pLabelInfo ) {
	pfn_vkCmdInsertDebugUtilsLabelEXT( commandBuffer, pLabelInfo );
}
#ifdef LE_FEATURE_RTX

//VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(
//    VkDevice                         device,
//    const VkBufferDeviceAddressInfo *pInfo ) {
//	return pfn_vkGetBufferDeviceAddress( device, pInfo );
//}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(
    VkDevice                                    device,
    const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *               pAllocator,
    VkAccelerationStructureKHR *                pAccelerationStructure ) {
	return pfn_vkCreateAccelerationStructureKHR( device, pCreateInfo, pAllocator, pAccelerationStructure );
};

VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(
    VkDevice                     device,
    VkAccelerationStructureKHR   accelerationStructure,
    const VkAllocationCallbacks *pAllocator ) {
	pfn_vkDestroyAccelerationStructureKHR( device, accelerationStructure, pAllocator );
};

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(
    VkCommandBuffer                                        commandBuffer,
    uint32_t                                               infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR *    pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos ) {
	return pfn_vkCmdBuildAccelerationStructuresKHR( commandBuffer, infoCount, pInfos, ppBuildRangeInfos );
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(
    VkDevice                                           device,
    const VkAccelerationStructureDeviceAddressInfoKHR *pInfo ) {
	return pfn_vkGetAccelerationStructureDeviceAddressKHR( device, pInfo );
}

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(
    VkDevice                                           device,
    VkAccelerationStructureBuildTypeKHR                buildType,
    const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
    const uint32_t *                                   pMaxPrimitiveCounts,
    VkAccelerationStructureBuildSizesInfoKHR *         pSizeInfo ) {
	return pfn_vkGetAccelerationStructureBuildSizesKHR( device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo );
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(
    VkDevice                                 device,
    VkDeferredOperationKHR                   deferredOperation,
    VkPipelineCache                          pipelineCache,
    uint32_t                                 createInfoCount,
    const VkRayTracingPipelineCreateInfoKHR *pCreateInfos,
    const VkAllocationCallbacks *            pAllocator,
    VkPipeline *                             pPipelines ) {
	return pfn_vkCreateRayTracingPipelinesKHR( device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines );
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
    VkCommandBuffer                        commandBuffer,
    const VkStridedDeviceAddressRegionKHR *pRaygenShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR *pMissShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR *pHitShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR *pCallableShaderBindingTable,
    uint32_t                               width,
    uint32_t                               height,
    uint32_t                               depth ) {
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

//static void create_debug_messenger_callback( le_backend_vk_instance_o *obj );  // ffdecl.
//static void destroy_debug_messenger_callback( le_backend_vk_instance_o *obj ); // ffdecl.

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

	static auto logger = le_log_api_i->get_channel( LOGGER_LABEL );

	auto log_fun = le_log_api_i->le_log_channel_i.error;

	if ( messageSeverity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT ) {
		log_fun = le_log_api_i->le_log_channel_i.debug;
	} else if ( messageSeverity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
		log_fun = le_log_api_i->le_log_channel_i.info;
	} else if ( messageSeverity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		log_fun = le_log_api_i->le_log_channel_i.warn;
	} else {
		// keep logger == error
	}

	log_fun( logger, "vk validation: {%10s | %7s} %s", msgType.c_str(), logLevel.c_str(), pCallbackData->pMessage );

	return shouldBailout;
}
// ----------------------------------------------------------------------

static void create_debug_messenger_callback( le_backend_vk_instance_o *obj ) {

	if ( false == SHOULD_USE_VALIDATION_LAYERS ) {
		return;
	}

	if ( !instance_is_extension_available( obj, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
		return;
	}

	vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo;
	debugMessengerCreateInfo
	    .setPNext( nullptr )
	    .setFlags( {} )
	    .setMessageSeverity( ~vk::DebugUtilsMessageSeverityFlagsEXT() ) // everything.
	    .setMessageType( ~vk::DebugUtilsMessageTypeFlagsEXT() )         // everything.
	    .setPfnUserCallback( debugUtilsMessengerCallback )
	    .setPUserData( nullptr );

	obj->debugMessenger = obj->vkInstance.createDebugUtilsMessengerEXT( debugMessengerCreateInfo );
}

// ----------------------------------------------------------------------

static void destroy_debug_messenger_callback( le_backend_vk_instance_o *obj ) {

	if ( false == SHOULD_USE_VALIDATION_LAYERS ) {
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

	static_assert( VK_HEADER_VERSION >= 162, "Wrong VK_HEADER_VERSION!" );

	auto        self   = new le_backend_vk_instance_o();
	static auto logger = LeLog( LOGGER_LABEL );

	vk::ApplicationInfo appInfo;
	appInfo
	    .setPApplicationName( "Island App" )
	    .setApplicationVersion( VK_MAKE_API_VERSION( 0, 0, 0, 0 ) )
	    .setPEngineName( ISL_ENGINE_NAME )
	    .setEngineVersion( ISL_ENGINE_VERSION )
	    .setApiVersion( VK_MAKE_API_VERSION( 0, 1, 2, 162 ) );

	// -- create a vector of unique requested instance extension names

	if ( true == SHOULD_USE_VALIDATION_LAYERS ) {
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

	if ( true == SHOULD_USE_VALIDATION_LAYERS ) {
		instanceLayerNames.push_back( "VK_LAYER_KHRONOS_validation" );
		logger.info( "Debug instance layers added." );
	}

	vk::ValidationFeaturesEXT validationFeatures;
	validationFeatures
	    .setPNext( nullptr )
	    //.setEnabledValidationFeatureCount( uint32_t( sizeof( enabledValidationFeatures ) / sizeof( vk::ValidationFeatureEnableEXT ) ) )
	    //.setPEnabledValidationFeatures( enabledValidationFeatures )
	    .setDisabledValidationFeatureCount( uint32_t( sizeof( disabledValidationFeatures ) / sizeof( vk::ValidationFeatureDisableEXT ) ) )
	    .setPDisabledValidationFeatures( disabledValidationFeatures );

	vk::InstanceCreateInfo info;
	info.setFlags( {} )
	    .setPNext( SHOULD_USE_VALIDATION_LAYERS ? &validationFeatures : nullptr ) // We add a debug messenger object to instance creation to get creation-time debug info
	    .setPApplicationInfo( &appInfo )
	    .setEnabledLayerCount( uint32_t( instanceLayerNames.size() ) )
	    .setPpEnabledLayerNames( instanceLayerNames.data() )
	    .setEnabledExtensionCount( uint32_t( instanceExtensionCstr.size() ) )
	    .setPpEnabledExtensionNames( instanceExtensionCstr.data() );

	self->vkInstance = vk::createInstance( info );

	// store a reference to into our central dictionary
	*le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_backend_instance_o" ) ) = self;

	patchExtProcAddrs( self );

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		create_debug_messenger_callback( self );
		logger.info( "Vulkan Validation Layers Active." );
	}
	logger.info( "Instance created." );

	return self;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_backend_vk_instance_o *obj ) {
	static auto logger = LeLog( LOGGER_LABEL );
	destroy_debug_messenger_callback( obj );
	obj->vkInstance.destroy();
	delete ( obj );
	logger.info( "Instance destroyed." );
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
	static auto logger = LeLog( LOGGER_LABEL );
	logger.debug( "post reload hook triggered." );
	patchExtProcAddrs( obj );
	destroy_debug_messenger_callback( obj );
	logger.debug( "Removed debug report callback." );
	create_debug_messenger_callback( obj );
	logger.debug( "Added new debug report callback." );
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
