#include "le_swapchain_vk.h"
#ifndef _MSC_VER
#	define VK_USE_PLATFORM_XLIB_XRANDR_EXT
#endif
#include "private/le_swapchain_vk/le_swapchain_vk_common.inl"

#include "util/volk/volk.h"
#include "private/le_swapchain_vk/vk_to_string_helpers.inl"

#include "le_backend_vk.h"
#include "private/le_renderer_types.h"
#include "le_log.h"

#include <iostream>
#include <vector>
#include <assert.h>

#define ASSERT_VK_SUCCESS( x ) \
	assert( x == VkResult::eSuccess )

static constexpr auto LOGGER_LABEL = "le_swapchain_direct";

struct SurfaceProperties {
	VkSurfaceFormatKHR              windowSurfaceFormat;
	VkSurfaceCapabilitiesKHR        surfaceCapabilities;
	VkBool32                        presentSupported = VK_FALSE;
	std::vector<VkPresentModeKHR>   presentmodes;
	std::vector<VkSurfaceFormatKHR> availableSurfaceFormats;
};

#define getInstanceProc( instance, procName ) \
	static auto procName = reinterpret_cast<PFN_##procName>( vkGetInstanceProcAddr( instance, #procName ) )

#define getDeviceProc( instance, procName ) \
	static auto procName = reinterpret_cast<PFN_##procName>( vkGetDeviceProcAddr( instance, #procName ) )

struct swp_direct_data_o {
	le_swapchain_settings_t mSettings                      = {};
	le_backend_o*           backend                        = nullptr;
	uint32_t                mImagecount                    = 0;
	uint32_t                mImageIndex                    = uint32_t( ~0 ); // current image index
	VkSwapchainKHR          swapchainKHR                   = nullptr;
	VkExtent2D              mSwapchainExtent               = {};
	VkPresentModeKHR        mPresentMode                   = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t                vk_graphics_queue_family_index = 0;
	SurfaceProperties       mSurfaceProperties             = {};
	std::vector<VkImage>    mImageRefs                     = {}; // owned by SwapchainKHR, don't delete
	VkInstance              instance                       = nullptr;
	VkDevice                device                         = nullptr;
	VkPhysicalDevice        physicalDevice                 = nullptr;

#ifdef _MSC_VER

#else
	Display* x11_display = nullptr;
#endif

	VkDisplayKHR                            display                 = nullptr;
	VkSurfaceKHR                            surface                 = nullptr;
	std::vector<VkDisplayModePropertiesKHR> display_mode_properties = {};
};

// ----------------------------------------------------------------------

template <typename T>
static inline auto clamp( const T& val_, const T& min_, const T& max_ ) {
	return std::max( min_, ( std::min( val_, max_ ) ) );
}

// ----------------------------------------------------------------------

static inline void vk_result_assert_success( VkResult const&& result ) {
	static auto logger = LeLog( LOGGER_LABEL );

	if ( result != VK_SUCCESS ) {
		logger.error( "Vulkan operation returned: %s, but we expected VkResult::eSuccess", to_str( result ) );
	}
	assert( result == VK_SUCCESS && "Vulkan operation must succeed" );
}
// ----------------------------------------------------------------------

static void swapchain_query_surface_capabilities( le_swapchain_o* base ) {

	// we need to find out if the current physical device supports PRESENT

	auto self = static_cast<swp_direct_data_o* const>( base->data );

	using namespace le_backend_vk;

	auto& surfaceProperties = self->mSurfaceProperties;

	auto result = vkGetPhysicalDeviceSurfaceSupportKHR( self->physicalDevice, self->vk_graphics_queue_family_index, self->surface, &surfaceProperties.presentSupported );
	assert( result == VK_SUCCESS );
	// Get list of supported surface formats

	{
		uint32_t num_elements{};
		vkGetPhysicalDeviceSurfaceFormatsKHR( self->physicalDevice, self->surface, &num_elements, nullptr );
		assert( result == VK_SUCCESS );
		surfaceProperties.availableSurfaceFormats.resize( num_elements );
		result = vkGetPhysicalDeviceSurfaceFormatsKHR( self->physicalDevice, self->surface, &num_elements, surfaceProperties.availableSurfaceFormats.data() );
		assert( result == VK_SUCCESS );
	}
	{
		result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( self->physicalDevice, self->surface, &surfaceProperties.surfaceCapabilities );
		assert( result == VK_SUCCESS );
	}
	{
		uint32_t num_elements{};
		result = vkGetPhysicalDeviceSurfacePresentModesKHR( self->physicalDevice, self->surface, &num_elements, nullptr );
		assert( result == VK_SUCCESS );
		surfaceProperties.presentmodes.resize( num_elements );
		result = vkGetPhysicalDeviceSurfacePresentModesKHR( self->physicalDevice, self->surface, &num_elements, surfaceProperties.presentmodes.data() );
		assert( result == VK_SUCCESS );
	}
	size_t selectedSurfaceFormatIndex = 0;
	auto   preferredSurfaceFormat     = VkFormat( self->mSettings.format_hint );

	if ( ( surfaceProperties.availableSurfaceFormats.size() == 1 ) && ( surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].format == VK_FORMAT_UNDEFINED ) ) {

		// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
		// there is no preferred format, and we must assume VkFormat::eB8G8R8A8Unorm.
		surfaceProperties.windowSurfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;

	} else {

		// Iterate over the list of available surface formats and check for the presence of
		// our preferredSurfaceFormat
		//
		// Select the first available color format if the preferredSurfaceFormat cannot be found.

		for ( size_t i = 0; i != surfaceProperties.availableSurfaceFormats.size(); ++i ) {
			if ( surfaceProperties.availableSurfaceFormats[ i ].format == preferredSurfaceFormat ) {
				selectedSurfaceFormatIndex = i;
				break;
			}
		}
		surfaceProperties.windowSurfaceFormat.format = surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].format;
	}

	// always select the corresponding color space
	surfaceProperties.windowSurfaceFormat.colorSpace = surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].colorSpace;
}

// ----------------------------------------------------------------------

static VkPresentModeKHR get_direct_presentmode( const le_swapchain_settings_t::khr_settings_t::Presentmode& presentmode_hint_ ) {
	using PresentMode = le_swapchain_settings_t::khr_settings_t::Presentmode;
	switch ( presentmode_hint_ ) {
	case ( PresentMode::eDefault ):
		return VK_PRESENT_MODE_FIFO_KHR;
	case ( PresentMode::eImmediate ):
		return VK_PRESENT_MODE_IMMEDIATE_KHR;
	case ( PresentMode::eMailbox ):
		return VK_PRESENT_MODE_MAILBOX_KHR;
	case ( PresentMode::eFifo ):
		return VK_PRESENT_MODE_FIFO_KHR;
	case ( PresentMode::eFifoRelaxed ):
		return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
	case ( PresentMode::eSharedDemandRefresh ):
		return VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
	case ( PresentMode::eSharedContinuousRefresh ):
		return VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
	}
	assert( false ); // something's wrong: control should never come here, switch needs to cover all cases.
	return VK_PRESENT_MODE_FIFO_KHR;
}

// ----------------------------------------------------------------------

static void swapchain_attach_images( le_swapchain_o* base ) {
	auto self = static_cast<swp_direct_data_o* const>( base->data );

	auto result = vkGetSwapchainImagesKHR( self->device, self->swapchainKHR, &self->mImagecount, nullptr );
	assert( result == VK_SUCCESS );

	self->mImageRefs.resize( self->mImagecount );
	vkGetSwapchainImagesKHR( self->device, self->swapchainKHR, &self->mImagecount, self->mImageRefs.data() );
	assert( result == VK_SUCCESS );
}

// ----------------------------------------------------------------------

static void swapchain_direct_reset( le_swapchain_o* base, const le_swapchain_settings_t* settings_ ) {
	static auto logger = LeLog( LOGGER_LABEL );

	auto self = static_cast<swp_direct_data_o* const>( base->data );

	if ( settings_ ) {
		self->mSettings = *settings_;
	}

	// `settings_` may have been a nullptr in which case this operation is only valid
	// if self->mSettings has been fully set before.

	assert( self->mSettings.type == le_swapchain_settings_t::Type::LE_DIRECT_SWAPCHAIN );

	//	VkResult err = ::VkResult::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	swapchain_query_surface_capabilities( base );

	VkSwapchainKHR oldSwapchain = self->swapchainKHR;

	const VkSurfaceCapabilitiesKHR&        surfaceCapabilities = self->mSurfaceProperties.surfaceCapabilities;
	const std::vector<::VkPresentModeKHR>& presentModes        = self->mSurfaceProperties.presentmodes;

	// Either set or get the swapchain surface extents

	if ( surfaceCapabilities.currentExtent.width == 0 ) {
		self->mSwapchainExtent.width  = self->mSettings.width_hint;
		self->mSwapchainExtent.height = self->mSettings.height_hint;
	} else {
		// set dimensions from surface extents if surface extents are available
		self->mSwapchainExtent = surfaceCapabilities.currentExtent;
	}

	auto presentModeHint = get_direct_presentmode( self->mSettings.khr_settings.presentmode_hint );

	for ( auto& p : presentModes ) {
		if ( p == presentModeHint ) {
			self->mPresentMode = p;
			break;
		}
	}

	if ( self->mPresentMode != presentModeHint ) {
		logger.warn(
		    "Could not switch to selected Swapchain Present Mode (%s), falling back to: %s",
		    to_str( presentModeHint ),
		    to_str( self->mPresentMode ) );
	}

	self->mImagecount = clamp( self->mSettings.imagecount_hint,
	                           surfaceCapabilities.minImageCount,
	                           surfaceCapabilities.maxImageCount );

	if ( self->mImagecount != self->mSettings.imagecount_hint ) {
		logger.warn( "Number of swapchain images was adjusted to: %d", self->mImagecount );
	}

	VkSurfaceTransformFlagBitsKHR preTransform;
	// Note: this will be interesting for mobile devices
	// - if rotation and mirroring for the final output can
	// be defined here.

	if ( surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ) {
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	} else {
		preTransform = surfaceCapabilities.currentTransform;
	}

	VkSwapchainCreateInfoKHR swapChainCreateInfo{
	    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	    .pNext                 = nullptr, // optional
	    .flags                 = 0,       // optional
	    .surface               = self->surface,
	    .minImageCount         = self->mImagecount,
	    .imageFormat           = self->mSurfaceProperties.windowSurfaceFormat.format,
	    .imageColorSpace       = self->mSurfaceProperties.windowSurfaceFormat.colorSpace,
	    .imageExtent           = self->mSwapchainExtent,
	    .imageArrayLayers      = 1,
	    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0, // optional
	    .pQueueFamilyIndices   = 0,
	    .preTransform          = preTransform,
	    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	    .presentMode           = self->mPresentMode,
	    .clipped               = VK_TRUE,
	    .oldSwapchain          = oldSwapchain, // optional
	};

	auto result = vkCreateSwapchainKHR( self->device, &swapChainCreateInfo, nullptr, &self->swapchainKHR );
	assert( result == VK_SUCCESS );

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ( oldSwapchain ) {
		vkDestroySwapchainKHR( self->device, oldSwapchain, nullptr );
		oldSwapchain = nullptr;
	}

	swapchain_attach_images( base );
}

// ----------------------------------------------------------------------

static le_swapchain_o* swapchain_direct_create( const le_swapchain_vk_api::swapchain_interface_t& interface, le_backend_o* backend, const le_swapchain_settings_t* settings ) {

	static auto logger = LeLog( LOGGER_LABEL );

	auto base  = new le_swapchain_o( interface );
	base->data = new swp_direct_data_o{};
	auto self  = static_cast<swp_direct_data_o*>( base->data );

	self->backend = backend;

	{
		using namespace le_backend_vk;
		self->device                         = private_backend_vk_i.get_vk_device( backend );
		self->physicalDevice                 = private_backend_vk_i.get_vk_physical_device( backend );
		self->instance                       = vk_instance_i.get_vk_instance( private_backend_vk_i.get_instance( backend ) );
		self->vk_graphics_queue_family_index = vk_device_i.get_default_graphics_queue_family_index( private_backend_vk_i.get_le_device( backend ) );
	}

#ifdef _MSC_VER
#else
	self->x11_display    = XOpenDisplay( nullptr );
#endif
	auto phyDevice = VkPhysicalDevice( self->physicalDevice );

	std::vector<VkDisplayPropertiesKHR> display_props;
	uint32_t                            prop_count = 0;

	auto result = vkGetPhysicalDeviceDisplayPropertiesKHR( self->physicalDevice, &prop_count, nullptr );

	assert( result == VK_SUCCESS );
	display_props.resize( prop_count );
	result = vkGetPhysicalDeviceDisplayPropertiesKHR( self->physicalDevice, &prop_count, display_props.data() );
	assert( result == VK_SUCCESS );

	// We want to find out which display is secondary display
	// but we assume that the primary display will be listed first.
	self->display = display_props.back().display;

	VkResult vk_result = VK_SUCCESS;

#ifdef _MSC_VER
#else

	getInstanceProc( self->instance, vkAcquireXlibDisplayEXT );
	vk_result = vkAcquireXlibDisplayEXT( phyDevice, self->x11_display, self->display );

	if ( vk_result != VK_SUCCESS ) {
		logger.error( "Unable to acquire display: %s", display_props.back().displayName );
	}

	assert( vk_result == VK_SUCCESS );
#endif

	{
		uint32_t num_props{};
		result = vkGetDisplayModePropertiesKHR( self->physicalDevice, self->display, &num_props, nullptr );
		assert( vk_result == VK_SUCCESS );

		self->display_mode_properties.resize( num_props );
		result = vkGetDisplayModePropertiesKHR( self->physicalDevice, self->display, &num_props, self->display_mode_properties.data() );
		assert( vk_result == VK_SUCCESS );
	}
	// let's try to acquire this screen

	{
		VkDisplaySurfaceCreateInfoKHR info{
		    .sType           = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
		    .pNext           = nullptr, // optional
		    .flags           = 0,       // optional
		    .displayMode     = self->display_mode_properties[ 0 ].displayMode,
		    .planeIndex      = 0,
		    .planeStackIndex = 0,
		    .transform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		    .globalAlpha     = 1.f,
		    .alphaMode       = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
		    .imageExtent     = self->display_mode_properties[ 0 ].parameters.visibleRegion,
		};

		result = vkCreateDisplayPlaneSurfaceKHR( self->instance, &info, nullptr, &self->surface );

		assert( result == VK_SUCCESS );

		self->mSwapchainExtent.height = uint32_t( info.imageExtent.height );
		self->mSwapchainExtent.width  = uint32_t( info.imageExtent.width );
	}

	swapchain_direct_reset( base, settings );

	return base;
}

// ----------------------------------------------------------------------

static void swapchain_direct_destroy( le_swapchain_o* base ) {

	auto self = static_cast<swp_direct_data_o* const>( base->data );

	vkDestroySwapchainKHR( self->device, self->swapchainKHR, nullptr );
	self->swapchainKHR = nullptr;

	vkDestroySurfaceKHR( self->instance, self->surface, nullptr );
	self->surface = nullptr;

	getInstanceProc( self->instance, vkReleaseDisplayEXT );
	vkReleaseDisplayEXT( self->physicalDevice, self->display );

#ifdef _MSC_VER
#else
	XCloseDisplay( self->x11_display );
#endif
	delete self; // delete object's data
	delete base; // delete object
}

// ----------------------------------------------------------------------

static bool swapchain_direct_acquire_next_image( le_swapchain_o* base, VkSemaphore semaphorePresentComplete_, uint32_t& imageIndex_ ) {

	auto self = static_cast<swp_direct_data_o* const>( base->data );
	// This method will return the next avaliable vk image index for this swapchain, possibly
	// before this image is available for writing. Image will be ready for writing when
	// semaphorePresentComplete is signalled.

	auto result = vkAcquireNextImageKHR( self->device, self->swapchainKHR, UINT64_MAX, semaphorePresentComplete_, nullptr, &imageIndex_ );

	switch ( result ) {
	case VK_SUCCESS:
		self->mImageIndex = imageIndex_;
		return true;
	case VK_SUBOPTIMAL_KHR:         // | fall-through
	case VK_ERROR_SURFACE_LOST_KHR: // |
	case VK_ERROR_OUT_OF_DATE_KHR:  // |
	{
		return false;
	}
	default:
		return false;
	}
}

// ----------------------------------------------------------------------

static bool swapchain_direct_present( le_swapchain_o* base, VkQueue queue_, VkSemaphore renderCompleteSemaphore_, uint32_t* pImageIndex ) {

	auto self = static_cast<swp_direct_data_o* const>( base->data );

	VkPresentInfoKHR presentInfo{
	    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	    .pNext              = nullptr, // optional
	    .waitSemaphoreCount = 1,       // optional
	    .pWaitSemaphores    = &renderCompleteSemaphore_,
	    .swapchainCount     = 1,
	    .pSwapchains        = &self->swapchainKHR,
	    .pImageIndices      = pImageIndex,
	    .pResults           = 0, // optional
	};

	;

	auto result = vkQueuePresentKHR( queue_, &presentInfo );

	if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
		return false;
	}

	return true;
};

// ----------------------------------------------------------------------

static VkImage swapchain_direct_get_image( le_swapchain_o* base, uint32_t index ) {

	auto self = static_cast<swp_direct_data_o* const>( base->data );

#ifndef NDEBUG
	assert( index < self->mImageRefs.size() );
#endif
	return self->mImageRefs[ index ];
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR* swapchain_direct_get_surface_format( le_swapchain_o* base ) {
	auto self = static_cast<swp_direct_data_o* const>( base->data );
	return &reinterpret_cast<VkSurfaceFormatKHR&>( self->mSurfaceProperties.windowSurfaceFormat );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_direct_get_image_width( le_swapchain_o* base ) {
	auto self = static_cast<swp_direct_data_o* const>( base->data );
	return self->mSwapchainExtent.width;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_direct_get_image_height( le_swapchain_o* base ) {
	auto self = static_cast<swp_direct_data_o* const>( base->data );
	return self->mSwapchainExtent.height;
}

// ----------------------------------------------------------------------

static size_t swapchain_direct_get_swapchain_images_count( le_swapchain_o* base ) {
	auto self = static_cast<swp_direct_data_o* const>( base->data );
	return self->mImagecount;
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_instance_extensions( const le_swapchain_settings_t* ) {
	using namespace le_backend_vk;
	api->le_backend_settings_i.add_required_instance_extension( VK_KHR_DISPLAY_EXTENSION_NAME );
	api->le_backend_settings_i.add_required_instance_extension( "VK_EXT_direct_mode_display" );
	api->le_backend_settings_i.add_required_instance_extension( "VK_KHR_xlib_surface" );
	api->le_backend_settings_i.add_required_instance_extension( "VK_KHR_surface" );
	api->le_backend_settings_i.add_required_instance_extension( "VK_EXT_acquire_xlib_display" );
	api->le_backend_settings_i.add_required_instance_extension( "VK_EXT_display_surface_counter" );
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_device_extensions( const le_swapchain_settings_t* ) {
	using namespace le_backend_vk;
	api->le_backend_settings_i.add_required_device_extension( "VK_EXT_display_control" );
	api->le_backend_settings_i.add_required_device_extension( "VK_KHR_swapchain" );
}

// ----------------------------------------------------------------------

void register_le_swapchain_direct_api( void* api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api*>( api_ );
	auto& swapchain_i = api->swapchain_direct_i;

	swapchain_i.create                              = swapchain_direct_create;
	swapchain_i.destroy                             = swapchain_direct_destroy;
	swapchain_i.reset                               = swapchain_direct_reset;
	swapchain_i.acquire_next_image                  = swapchain_direct_acquire_next_image;
	swapchain_i.get_image                           = swapchain_direct_get_image;
	swapchain_i.get_image_width                     = swapchain_direct_get_image_width;
	swapchain_i.get_image_height                    = swapchain_direct_get_image_height;
	swapchain_i.get_surface_format                  = swapchain_direct_get_surface_format;
	swapchain_i.get_images_count                    = swapchain_direct_get_swapchain_images_count;
	swapchain_i.present                             = swapchain_direct_present;
	swapchain_i.get_required_vk_instance_extensions = swapchain_get_required_vk_instance_extensions;
	swapchain_i.get_required_vk_device_extensions   = swapchain_get_required_vk_device_extensions;
}
