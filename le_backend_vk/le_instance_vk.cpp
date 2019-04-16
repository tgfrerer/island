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
	vk::Instance               vkInstance = nullptr;
	vk::DebugReportCallbackEXT debugCallback;
	std::vector<std::string> enabledInstanceExtensions{};
};

PFN_vkCreateDebugReportCallbackEXT  pfn_vkCreateDebugReportCallbackEXT;
PFN_vkDestroyDebugReportCallbackEXT pfn_vkDestroyDebugReportCallbackEXT;
PFN_vkDebugReportMessageEXT         pfn_vkDebugReportMessageEXT;
// ----------------------------------------------------------------------

static bool instance_is_extension_available( le_backend_vk_instance_o *self, char const *extension_name ); //ffdecl

void patchExtProcAddrs( le_backend_vk_instance_o *obj ) {
	pfn_vkCreateDebugReportCallbackEXT  = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>( obj->vkInstance.getProcAddr( "vkCreateDebugReportCallbackEXT" ) );
	pfn_vkDestroyDebugReportCallbackEXT = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>( obj->vkInstance.getProcAddr( "vkDestroyDebugReportCallbackEXT" ) );
	pfn_vkDebugReportMessageEXT         = reinterpret_cast<PFN_vkDebugReportMessageEXT>( obj->vkInstance.getProcAddr( "vkDebugReportMessageEXT" ) );
	std::cout << "Patched proc addrs." << std::endl;
}

// ----------------------------------------------------------------------

static void create_debug_callback( le_backend_vk_instance_o *obj );  // ffdecl.
static void destroy_debug_callback( le_backend_vk_instance_o *obj ); // ffdecl.

// ----------------------------------------------------------------------

static VkBool32 debugCallback(
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT objectType,
    uint64_t                   object,
    size_t                     location,
    int32_t                    messageCode,
    const char *               pLayerPrefix,
    const char *               pMessage,
    void *                     pUserData ) {

	bool        shouldBailout = VK_FALSE;
	std::string logLevel      = "";

	if ( flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT ) {
		logLevel = "INFO";
	} else if ( flags & VK_DEBUG_REPORT_WARNING_BIT_EXT ) {
		logLevel = "WARN";
	} else if ( flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT ) {
		logLevel = "PERF";
	} else if ( flags & VK_DEBUG_REPORT_ERROR_BIT_EXT ) {
		logLevel = "ERROR";
		shouldBailout |= VK_TRUE;
	} else if ( flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT ) {
		logLevel = "DEBUG";
	}

	std::ostringstream os;
	os << " * \t " << std::left << std::setw( 8 ) << logLevel << "{" << std::setw( 10 ) << pLayerPrefix << "}: " << pMessage << std::endl;
	std::cout << os.str();
	std::cout << std::flush;

	// if error returns true, this layer will try to bail out and not forward the command
	return shouldBailout;
}

// ----------------------------------------------------------------------

static void create_debug_callback( le_backend_vk_instance_o *obj ) {

	if ( !SHOULD_USE_VALIDATION_LAYERS ) {
		return;
	}

	vk::DebugReportCallbackCreateInfoEXT debugCallbackCreateInfo;
	debugCallbackCreateInfo
	    .setPNext( nullptr )
	    .setFlags( vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning )
	    .setPfnCallback( debugCallback )
	    .setPUserData( obj );

	obj->debugCallback = obj->vkInstance.createDebugReportCallbackEXT( debugCallbackCreateInfo );
}

// ----------------------------------------------------------------------

static void destroy_debug_callback( le_backend_vk_instance_o *obj ) {

	if ( !SHOULD_USE_VALIDATION_LAYERS ) {
		return;
	}

	obj->vkInstance.destroyDebugReportCallbackEXT( obj->debugCallback );
	obj->debugCallback = nullptr;
}

// ----------------------------------------------------------------------

le_backend_vk_instance_o *instance_create( const char **extensionNamesArray_, uint32_t numExtensionNames_ ) {

	auto instance = new le_backend_vk_instance_o();

	vk::ApplicationInfo appInfo;
	appInfo
	    .setPApplicationName( "island app" )
	    .setApplicationVersion( VK_MAKE_VERSION( 0, 0, 0 ) )
	    .setPEngineName( "island" )
	    .setEngineVersion( VK_MAKE_VERSION( 0, 1, 0 ) )
	    .setApiVersion( VK_MAKE_VERSION( 1, 1, 101 ) );

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

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		instanceExtensionNames.push_back( VK_EXT_DEBUG_REPORT_EXTENSION_NAME );
		// instanceLayerNames.push_back( "VK_LAYER_LUNARG_standard_validation" ); // <- deactivate for now because of mem leak in unique_objects
		instanceLayerNames.push_back( "VK_LAYER_GOOGLE_threading" );
		instanceLayerNames.push_back( "VK_LAYER_LUNARG_parameter_validation" );
		instanceLayerNames.push_back( "VK_LAYER_LUNARG_object_tracker" );
		instanceLayerNames.push_back( "VK_LAYER_LUNARG_core_validation" );
		// instanceLayerNames.push_back("VK_LAYER_GOOGLE_unique_objects"); // <- deactivate for now because memory leak
		std::cout << "Debug instance layers added." << std::endl;
	}

	// seems that the latest driver won't let us do this.
	vk::DebugReportCallbackCreateInfoEXT debugCallbackCreateInfo;
	debugCallbackCreateInfo
	    .setPNext( nullptr )
	    .setFlags( ~vk::DebugReportFlagBitsEXT() )
	    .setPfnCallback( debugCallback )
	    .setPUserData( nullptr );

	vk::InstanceCreateInfo info;
	info.setFlags( {} )
	    .setPNext( &debugCallbackCreateInfo ) // we add a debugcallback object to instance creation to get creation-time debug info
	    .setPApplicationInfo( &appInfo )
	    .setEnabledLayerCount( uint32_t( instanceLayerNames.size() ) )
	    .setPpEnabledLayerNames( instanceLayerNames.data() )
	    .setEnabledExtensionCount( uint32_t( instanceExtensionNames.size() ) )
	    .setPpEnabledExtensionNames( instanceExtensionNames.data() );

	obj->vkInstance = vk::createInstance( info );

	le_backend_vk::api->cUniqueInstance = obj;

	if ( SHOULD_USE_VALIDATION_LAYERS ) {
		patchExtProcAddrs( obj );
		create_debug_callback( obj );
		std::cout << "VULKAN VALIDATION LAYERS ACTIVE." << std::endl;
	}

	std::cout << "Instance created." << std::endl;
	return obj;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_backend_vk_instance_o *obj ) {
	destroy_debug_callback( obj );
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
	destroy_debug_callback( obj );
	std::cout << "** Removed debug report callback." << std::endl;
	create_debug_callback( obj );
	std::cout << "** Added new debug report callback." << std::endl;
}

// ----------------------------------------------------------------------
// These method definitions are exported so that vkhpp can call them
// vkhpp has matching declarations
VkResult vkCreateDebugReportCallbackEXT(
    VkInstance                                instance,
    const VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *             pAllocator,
    VkDebugReportCallbackEXT *                pCallback ) {
	return pfn_vkCreateDebugReportCallbackEXT(
	    instance,
	    pCreateInfo,
	    pAllocator,
	    pCallback );
}

void vkDestroyDebugReportCallbackEXT(
    VkInstance                   instance,
    VkDebugReportCallbackEXT     callback,
    const VkAllocationCallbacks *pAllocator ) {
	pfn_vkDestroyDebugReportCallbackEXT(
	    instance,
	    callback,
	    pAllocator );
}

void vkDebugReportMessageEXT(
    VkInstance                 instance,
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT objectType,
    uint64_t                   object,
    size_t                     location,
    int32_t                    messageCode,
    const char *               pLayerPrefix,
    const char *               pMessage ) {
	pfn_vkDebugReportMessageEXT(
	    instance,
	    flags,
	    objectType,
	    object,
	    location,
	    messageCode,
	    pLayerPrefix,
	    pMessage );
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
