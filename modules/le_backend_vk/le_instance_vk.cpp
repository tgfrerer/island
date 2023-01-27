#include "le_core.h"
#include "le_backend_vk.h"
#include "le_hash_util.h"
#include "le_log.h"
#include <cassert>
#include <vector>
#include "util/volk/volk.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <string>

#ifdef _MSC_VER
#	include <intrin.h> // for debugbreak()
#else
#	include <csignal> // for std::raise
#endif

static constexpr auto LOGGER_LABEL = "le_instance_vk";

static bool should_use_validation_layers() {
// Disable Validation Layers for Release Builds by default,
// and enable Validation Layers for Debug Builds by default,
// unless explicitly set via LE_SETTING on startup.
#ifdef NDEBUG
	LE_SETTING( bool, LE_SETTING_SHOULD_USE_VALIDATION_LAYERS, false );
#else
	LE_SETTING( bool, LE_SETTING_SHOULD_USE_VALIDATION_LAYERS, true );
#endif
	return *LE_SETTING_SHOULD_USE_VALIDATION_LAYERS;
}

// ----------------------------------------------------------------------

#include "private/le_backend_vk/le_backend_vk_instance.inl"

/*
 * Specify which validation layers to enable within Khronos validation
 * layer. (The following are otherwise disabled by default)
 *
 */
 static const std::vector<VkValidationFeatureEnableEXT> enabledValidationFeatures = {
//  VkValidationFeatureEnableEXT::eGpuAssisted,
//  VkValidationFeatureEnableEXT::eGpuAssistedReserveBindingSlot,
//  VkValidationFeatureEnableEXT::eBestPractices,
//  VkValidationFeatureEnableEXT::eDebugPrintf,
 };

/*
 * Specify which validation layers to disable within Khronos validation
 * layer. (The following are otherwise enabled by default)
 *
 */

static const std::vector<VkValidationFeatureDisableEXT> disabledValidationFeatures = {
// VK_VALIDATION_FEATURE_DISABLE_UNIQUE_HANDLES_EXT, // this can cause crashes when resizing the swapchain window
 };


// ----------------------------------------------------------------------

static bool instance_is_extension_available( le_backend_vk_instance_o* self, char const* extension_name ); // ffdecl// ----------------------------------------------------------------------

static void patchExtProcAddrs( le_backend_vk_instance_o* obj ) {

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

	// fixme: this is where we call volk

	volkLoadInstance( obj->vkInstance );

	static auto logger = LeLog( LOGGER_LABEL );
	logger.debug( "Patched proc addrs." );
}

// ----------------------------------------------------------------------

// static void create_debug_messenger_callback( le_backend_vk_instance_o *obj );  // ffdecl.
// static void destroy_debug_messenger_callback( le_backend_vk_instance_o *obj ); // ffdecl.

static VkBool32 debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* ) {

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

#ifndef NDEBUG
	// Raise a breakpoint on ERROR in case we're running in debug mode.
	if ( shouldBailout ) {
#	ifdef _MSC_VER
		__debugbreak(); // see: https://docs.microsoft.com/en-us/cpp/intrinsics/debugbreak?view=msvc-170
#	else
		std::raise( SIGINT );
#	endif
	}
#endif

	return shouldBailout;
}
// ----------------------------------------------------------------------

static void create_debug_messenger_callback( le_backend_vk_instance_o* obj ) {

	if ( false == obj->is_using_validation_layers ) {
		return;
	}

	if ( !instance_is_extension_available( obj, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
		return;
	}

	VkDebugUtilsMessengerCreateInfoEXT info{
	    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	    .pNext = nullptr, // optional
	    .flags = 0,       // optional
	    .messageSeverity =
	        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
	        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
	        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
	        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
	    .messageType =
	        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
	        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
	        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
	    .pfnUserCallback = debugUtilsMessengerCallback,
	    .pUserData       = nullptr, // optional
	};

	vkCreateDebugUtilsMessengerEXT( obj->vkInstance, &info, nullptr, &obj->debugMessenger );
}

// ----------------------------------------------------------------------

static void destroy_debug_messenger_callback( le_backend_vk_instance_o* obj ) {

	if ( false == obj->is_using_validation_layers ) {
		return;
	}

	if ( !instance_is_extension_available( obj, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) ) {
		return;
	}

	vkDestroyDebugUtilsMessengerEXT( obj->vkInstance, obj->debugMessenger, nullptr );

	obj->debugMessenger = nullptr;
}

// ----------------------------------------------------------------------

le_backend_vk_instance_o* instance_create( const char** extensionNamesArray_, uint32_t numExtensionNames_ ) {

	static_assert( VK_HEADER_VERSION >= 162, "Wrong VK_HEADER_VERSION!" );

	auto        self   = new le_backend_vk_instance_o();
	static auto logger = LeLog( LOGGER_LABEL );

	self->is_using_validation_layers = should_use_validation_layers();

	VkApplicationInfo appInfo{
	    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	    .pNext              = nullptr,      // optional
	    .pApplicationName   = "Island App", // optional
	    .applicationVersion = VK_MAKE_API_VERSION( 0, 0, 0, 0 ),
	    .pEngineName        = ISL_ENGINE_NAME, // optional
	    .engineVersion      = ISL_ENGINE_VERSION,
	    .apiVersion         = ( VK_MAKE_API_VERSION( 0, 1, 3, 236 ) ),
	};

	// -- create a vector of unique requested instance extension names

	if ( self->is_using_validation_layers ) {
		self->instanceExtensionSet.insert( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
	}

	// Merge with user-requested extensions
	for ( uint32_t i = 0; i != numExtensionNames_; ++i ) {
		self->instanceExtensionSet.insert( extensionNamesArray_[ i ] );
	}

	std::vector<const char*> instanceExtensionCstr{};

	for ( auto& e : self->instanceExtensionSet ) {
		instanceExtensionCstr.push_back( e.c_str() );
	}

	// -- Create a vector of requested instance layers

	std::vector<const char*> instanceLayerNames = {};

	if ( self->is_using_validation_layers ) {
		instanceLayerNames.push_back( "VK_LAYER_KHRONOS_validation" );
		logger.info( "Debug instance layers added." );
	}

	VkValidationFeaturesEXT validationFeatures{
	    .sType                          = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
	    .pNext                          = nullptr, // optional
	    .enabledValidationFeatureCount = uint32_t(enabledValidationFeatures.size()) , 
	    .pEnabledValidationFeatures     = enabledValidationFeatures.data(),
	    .disabledValidationFeatureCount = uint32_t(disabledValidationFeatures.size()),
	    .pDisabledValidationFeatures    = disabledValidationFeatures.data(),
	};
	;

	VkInstanceCreateInfo info{
	    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .pNext                   = self->is_using_validation_layers ? &validationFeatures : nullptr, // optional
	    .flags                   = 0,                                                                // optional
	    .pApplicationInfo        = &appInfo,                                                         // optional
	    .enabledLayerCount       = uint32_t( instanceLayerNames.size() ),                            // optional
	    .ppEnabledLayerNames     = instanceLayerNames.data(),
	    .enabledExtensionCount   = uint32_t( instanceExtensionCstr.size() ), // optional
	    .ppEnabledExtensionNames = instanceExtensionCstr.data(),
	};

	VkResult result = volkInitialize();
	assert( result == VK_SUCCESS && "Volk must successfully load instance" );

	vkCreateInstance( &info, nullptr, &self->vkInstance );

	// store a reference to this object into our central dictionary
	*le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_backend_instance_o" ) ) = self;

	patchExtProcAddrs( self );

	if ( self->is_using_validation_layers ) {
		create_debug_messenger_callback( self );
		logger.info( "Vulkan Validation Layers Active." );
	}
	logger.info( "Instance created." );

	return self;
}

// ----------------------------------------------------------------------

static void instance_destroy( le_backend_vk_instance_o* obj ) {
	static auto logger = LeLog( LOGGER_LABEL );
	destroy_debug_messenger_callback( obj );
	vkDestroyInstance( obj->vkInstance, nullptr );
	delete ( obj );
	logger.info( "Instance destroyed." );
}

// ----------------------------------------------------------------------

static VkInstance_T* instance_get_vk_instance( le_backend_vk_instance_o* obj ) {
	return ( reinterpret_cast<VkInstance&>( obj->vkInstance ) );
}

// ----------------------------------------------------------------------

static bool instance_is_extension_available( le_backend_vk_instance_o* self, char const* extension_name ) {
	return self->instanceExtensionSet.find( extension_name ) != self->instanceExtensionSet.end();
}

// ----------------------------------------------------------------------

static void instance_post_reload_hook( le_backend_vk_instance_o* obj ) {
	static auto logger = LeLog( LOGGER_LABEL );
	logger.debug( "post reload hook triggered." );

	VkResult result = volkInitialize();
	assert( result == VK_SUCCESS && "Volk must successfully load instance" );

	patchExtProcAddrs( obj );
	destroy_debug_messenger_callback( obj );
	logger.debug( "Removed debug report callback." );
	create_debug_messenger_callback( obj );
	logger.debug( "Added new debug report callback." );
}

// ----------------------------------------------------------------------

void register_le_instance_vk_api( void* api_ ) {
	auto  api_i      = static_cast<le_backend_vk_api*>( api_ );
	auto& instance_i = api_i->vk_instance_i;

	instance_i.create                 = instance_create;
	instance_i.destroy                = instance_destroy;
	instance_i.get_vk_instance        = instance_get_vk_instance;
	instance_i.post_reload_hook       = instance_post_reload_hook;
	instance_i.is_extension_available = instance_is_extension_available;
}
