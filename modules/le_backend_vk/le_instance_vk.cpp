#include "le_backend_vk/le_backend_vk.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include <iostream>
#include <iomanip>
#include <set>
#include <string>

// Automatically disable Validation Layers for Release Builds
#ifdef NDEBUG
#	define SHOULD_USE_VALIDATION_LAYERS false
#endif

#ifndef SHOULD_USE_VALIDATION_LAYERS
#	define SHOULD_USE_VALIDATION_LAYERS true
#endif

// ----------------------------------------------------------------------

struct le_backend_vk_instance_o {
	vk::Instance               vkInstance     = nullptr;
	vk::DebugUtilsMessengerEXT debugMessenger = nullptr;

	std::vector<std::string> enabledInstanceExtensions{};
};

// ----------------------------------------------------------------------

static bool instance_is_extension_available( le_backend_vk_instance_o *self, char const *extension_name ); //ffdecl

#define DECLARE_EXT_PFN( proc )    \
	static PFN_##proc pfn_##proc { \
	}
DECLARE_EXT_PFN( vkSetDebugUtilsObjectNameEXT );
DECLARE_EXT_PFN( vkCreateDebugUtilsMessengerEXT );
DECLARE_EXT_PFN( vkDestroyDebugUtilsMessengerEXT );
#undef DECLARE_EXT_PFN

void patchExtProcAddrs( le_backend_vk_instance_o *obj ) {

#define GET_EXT_PROC_ADDR( proc ) \
	pfn_##proc = reinterpret_cast<PFN_##proc>( obj->vkInstance.getProcAddr( #proc ) )

	if ( instance_is_extension_available( obj, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
		GET_EXT_PROC_ADDR( vkSetDebugUtilsObjectNameEXT );
		GET_EXT_PROC_ADDR( vkCreateDebugUtilsMessengerEXT );
		GET_EXT_PROC_ADDR( vkDestroyDebugUtilsMessengerEXT );
	}

#undef GET_EXT_PROC_ADDR

	std::cout << "Patched proc addrs." << std::endl;
}

// ----------------------------------------------------------------------

static void create_debug_messenger_callback( le_backend_vk_instance_o *obj );  // ffdecl.
static void destroy_debug_messenger_callback( le_backend_vk_instance_o *obj ); // ffdecl.

static VkBool32 debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *                                      pUserData ) {

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

	vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo;
	debugMessengerCreateInfo
	    .setFlags( {} )
	    .setMessageSeverity( vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning )
	    .setMessageType( vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral )
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

	auto instance = new le_backend_vk_instance_o();

	static_assert( VK_HEADER_VERSION >= 121, "Wrong VK_HEADER_VERSION!" );

	vk::ApplicationInfo appInfo;
	appInfo
	    .setPApplicationName( "Island App" )
	    .setApplicationVersion( VK_MAKE_VERSION( 0, 0, 0 ) )
	    .setPEngineName( "Island" )
	    .setEngineVersion( VK_MAKE_VERSION( 0, 1, 0 ) )
	    .setApiVersion( VK_MAKE_VERSION( 1, 1, 121 ) );

	// -- create a vector of unique requested instance extension names

	std::set<std::string> instanceExtensionSet{};

	instanceExtensionSet.emplace( VK_KHR_SURFACE_EXTENSION_NAME );

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		instanceExtensionSet.emplace( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
	}

	// Merge with user-requested extensions
	for ( uint32_t i = 0; i != numExtensionNames_; ++i ) {
		instanceExtensionSet.emplace( extensionNamesArray_[ i ] );
	}

	{
		// Store requested instance extensions with instance so that
		// we may query later.
		instance->enabledInstanceExtensions.reserve( instanceExtensionSet.size() );

		for ( auto &e : instanceExtensionSet ) {
			instance->enabledInstanceExtensions.push_back( e );
		}
	}

	std::vector<const char *> instanceExtensionCstr{};

	for ( auto &e : instance->enabledInstanceExtensions ) {
		instanceExtensionCstr.push_back( e.c_str() );
	}

	// -- Create a vector of requested instance layers

	std::vector<const char *> instanceLayerNames = {};

	/*
	 * Specify which validation layers to enable within Khronos validation
	 * layer. (The following are otherwise disabled by default)
	 * 
	 */
	std::vector<vk::ValidationFeatureEnableEXT> enabledValidationFeatures{
	    vk::ValidationFeatureEnableEXT::eGpuAssisted,
	    vk::ValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
	    vk::ValidationFeatureEnableEXT::eBestPractices,
	};

	/* 
	 * Specify which validation layers to disable within Khronos validation
	 * layer. (The following are otherwise enabled by default)
	 * 
	 */
	std::vector<vk::ValidationFeatureDisableEXT> disabledValidationFeatures{
	    // vk::ValidationFeatureDisableEXT::eAll,
	    // vk::ValidationFeatureDisableEXT::eShaders,
	    // vk::ValidationFeatureDisableEXT::eThreadSafety,
	    // vk::ValidationFeatureDisableEXT::eApiParameters,
	    // vk::ValidationFeatureDisableEXT::eObjectLifetimes,
	    // vk::ValidationFeatureDisableEXT::eCoreChecks,
	    vk::ValidationFeatureDisableEXT::eUniqueHandles,
	};

	vk::ValidationFeaturesEXT validationFeatures;
	validationFeatures
	    .setPNext( nullptr )
	    .setEnabledValidationFeatureCount( uint32_t( enabledValidationFeatures.size() ) )
	    .setPEnabledValidationFeatures( enabledValidationFeatures.data() )
	    .setDisabledValidationFeatureCount( uint32_t( disabledValidationFeatures.size() ) )
	    .setPDisabledValidationFeatures( disabledValidationFeatures.data() );

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		instanceLayerNames.push_back( "VK_LAYER_KHRONOS_validation" );
		std::cout << "Debug instance layers added." << std::endl;
	}

	vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo;
	debugMessengerCreateInfo
	    .setPNext( SHOULD_USE_VALIDATION_LAYERS ? &validationFeatures : nullptr )
	    .setFlags( {} )
	    .setMessageSeverity( ~vk::DebugUtilsMessageSeverityFlagsEXT() ) // everything.
	    .setMessageType( ~vk::DebugUtilsMessageTypeFlagsEXT() )         // everything.
	    .setPfnUserCallback( debugUtilsMessengerCallback )
	    .setPUserData( nullptr );

	vk::InstanceCreateInfo info;
	info.setFlags( {} )
	    .setPNext( &debugMessengerCreateInfo ) // We add a debug messenger object to instance creation to get creation-time debug info
	    .setPApplicationInfo( &appInfo )
	    .setEnabledLayerCount( uint32_t( instanceLayerNames.size() ) )
	    .setPpEnabledLayerNames( instanceLayerNames.data() )
	    .setEnabledExtensionCount( uint32_t( instanceExtensionCstr.size() ) )
	    .setPpEnabledExtensionNames( instanceExtensionCstr.data() );

	instance->vkInstance = vk::createInstance( info );

	le_backend_vk::api->cUniqueInstance = instance;

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		patchExtProcAddrs( instance );
		create_debug_messenger_callback( instance );
		std::cout << "VULKAN VALIDATION LAYERS ACTIVE." << std::endl;
	}

	std::cout << "Instance created." << std::endl;
	return instance;
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

	for ( auto const &e : self->enabledInstanceExtensions ) {
		if ( e == std::string( extension_name ) ) {
			return true;
		}
	}

	return false;
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

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_instance_vk_api( void *api_ ) {
	auto  api_i      = static_cast<le_backend_vk_api *>( api_ );
	auto &instance_i = api_i->vk_instance_i;

	instance_i.create                 = instance_create;
	instance_i.destroy                = instance_destroy;
	instance_i.get_vk_instance        = instance_get_vk_instance;
	instance_i.post_reload_hook       = instance_post_reload_hook;
	instance_i.is_extension_available = instance_is_extension_available;
}
