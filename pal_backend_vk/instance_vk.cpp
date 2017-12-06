#include "pal_backend_vk/pal_backend_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>


struct pal_backend_o {
	vk::Instance        vkInstance = nullptr;
};

extern PFN_vkDebugReportCallbackEXT cPVkDebugCallback;
PFN_vkDebugReportCallbackEXT        cPVkDebugCallback;

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
	os << std::left << std::setw( 8 ) << logLevel << "{" << std::setw( 10 ) << pLayerPrefix << "}: " << pMessage << std::endl;
	std::cout << os.str();

	// if error returns true, this layer will try to bail out and not forward the command
	return shouldBailout;
}

// ----------------------------------------------------------------------

pal_backend_o* create() {

	auto obj = new pal_backend_o();

	vk::ApplicationInfo appInfo;
	appInfo
	    .setPApplicationName( "debug app" )
	    .setApplicationVersion( VK_MAKE_VERSION( 0, 0, 0 ) )
	    .setPEngineName( "project island" )
	    .setEngineVersion( VK_MAKE_VERSION( 0, 1, 0 ) )
	    .setApiVersion( VK_MAKE_VERSION( 1, 0, 46 ) );

	std::vector<const char *> instanceLayerNames     = {};
	std::vector<const char *> instanceExtensionNames = {};

	instanceExtensionNames.push_back( "VK_KHR_xcb_surface" );
	instanceExtensionNames.push_back( "VK_KHR_surface" );

	bool shouldDebug = true;

	if ( shouldDebug ) {
		instanceExtensionNames.push_back( VK_EXT_DEBUG_REPORT_EXTENSION_NAME );
		instanceLayerNames.push_back( "VK_LAYER_LUNARG_standard_validation" );
		instanceLayerNames.push_back( "VK_LAYER_LUNARG_object_tracker" );
	}

	cPVkDebugCallback = debugCallback;

	vk::DebugReportCallbackCreateInfoEXT debugCallbackCreateInfo;
	debugCallbackCreateInfo
	    .setPNext( nullptr )
	    .setFlags( ~vk::DebugReportFlagBitsEXT() )
	    .setPfnCallback( cPVkDebugCallback )
	    .setPUserData( nullptr );

	vk::InstanceCreateInfo info;
	info.setFlags( {} )
	    .setPNext( &debugCallbackCreateInfo )
	    .setPApplicationInfo( &appInfo )
	    .setEnabledLayerCount( uint32_t( instanceLayerNames.size() ) )
	    .setPpEnabledLayerNames( instanceLayerNames.data() )
	    .setEnabledExtensionCount( uint32_t( instanceExtensionNames.size() ) )
	    .setPpEnabledExtensionNames( instanceExtensionNames.data() );

	obj->vkInstance = vk::createInstance( info );
	std::cout << "Instance created." << std::endl;
	return obj;
}

// ----------------------------------------------------------------------

void update(pal_backend_o* obj) {
}

// ----------------------------------------------------------------------

void destroy(pal_backend_o* obj) {
	obj->vkInstance.destroy();
	delete(obj);
	std::cout << "Instance destroyed." << std::endl;
}
