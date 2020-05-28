#include "le_backend_vk/le_backend_vk.h"
#include "le_renderer/private/le_renderer_types.h"
#include "include/internal/le_swapchain_vk_common.h"

#define VULKAN_HPP_DISABLE_IMPLICIT_RESULT_VALUE_CAST
#define VULKAN_HPP_NO_SMART_HANDLE
#define VK_USE_PLATFORM_XLIB_XRANDR_EXT
#define VULKAN_HPP_DISABLE_ENHANCED_MODE // because otherwise it will complain about display
#include <vulkan/vulkan.hpp>

#include <iostream>
#include <vector>
#include <cassert>

#define ASSERT_VK_SUCCESS( x ) \
	assert( x == vk::Result::eSuccess )

struct SurfaceProperties {
	vk::SurfaceFormatKHR              windowSurfaceFormat;
	vk::SurfaceCapabilitiesKHR        surfaceCapabilities;
	VkBool32                          presentSupported = VK_FALSE;
	std::vector<vk::PresentModeKHR>   presentmodes;
	std::vector<vk::SurfaceFormatKHR> availableSurfaceFormats;
};

#define getInstanceProc( instance, procName ) \
	static auto procName = reinterpret_cast<PFN_##procName>( vkGetInstanceProcAddr( instance, #procName ) )

#define getDeviceProc( instance, procName ) \
	static auto procName = reinterpret_cast<PFN_##procName>( vkGetDeviceProcAddr( instance, #procName ) )

struct swp_direct_data_o {
	le_swapchain_settings_t                   mSettings                      = {};
	le_backend_o *                            backend                        = nullptr;
	uint32_t                                  mImagecount                    = 0;
	uint32_t                                  mImageIndex                    = uint32_t( ~0 ); // current image index
	vk::SwapchainKHR                          swapchainKHR                   = nullptr;
	vk::Extent2D                              mSwapchainExtent               = {};
	vk::PresentModeKHR                        mPresentMode                   = vk::PresentModeKHR::eFifo;
	uint32_t                                  vk_graphics_queue_family_index = 0;
	SurfaceProperties                         mSurfaceProperties             = {};
	std::vector<vk::Image>                    mImageRefs                     = {}; // owned by SwapchainKHR, don't delete
	vk::Instance                              instance                       = nullptr;
	vk::Device                                device                         = nullptr;
	vk::PhysicalDevice                        physicalDevice                 = nullptr;
	Display *                                 x11_display                    = nullptr;
	vk::DisplayKHR                            display                        = nullptr;
	vk::SurfaceKHR                            surface                        = nullptr;
	std::vector<vk::DisplayModePropertiesKHR> display_mode_properties        = {};
};

// ----------------------------------------------------------------------

static inline vk::Format le_format_to_vk( const le::Format &format ) noexcept {
	return vk::Format( format );
}

// ----------------------------------------------------------------------

static void swapchain_query_surface_capabilities( le_swapchain_o *base ) {

	// we need to find out if the current physical device supports PRESENT

	auto self = static_cast<swp_direct_data_o *const>( base->data );

	using namespace le_backend_vk;

	auto &surfaceProperties = self->mSurfaceProperties;

	self->physicalDevice.getSurfaceSupportKHR( self->vk_graphics_queue_family_index,
	                                           self->surface,
	                                           &surfaceProperties.presentSupported );

	// Get list of supported surface formats

	{
		uint32_t num_elements{};
		auto     result = self->physicalDevice.getSurfaceFormatsKHR( self->surface, &num_elements, nullptr );
		ASSERT_VK_SUCCESS( result );
		surfaceProperties.availableSurfaceFormats.resize( num_elements );
		result = self->physicalDevice.getSurfaceFormatsKHR( self->surface, &num_elements, surfaceProperties.availableSurfaceFormats.data() );
	}
	{
		auto result = self->physicalDevice.getSurfaceCapabilitiesKHR( self->surface, &surfaceProperties.surfaceCapabilities );
		ASSERT_VK_SUCCESS( result );
	}
	{
		uint32_t num_elements{};
		auto     result = self->physicalDevice.getSurfacePresentModesKHR( self->surface, &num_elements, nullptr );
		ASSERT_VK_SUCCESS( result );
		surfaceProperties.presentmodes.resize( num_elements );
		result = self->physicalDevice.getSurfacePresentModesKHR( self->surface, &num_elements, surfaceProperties.presentmodes.data() );
		ASSERT_VK_SUCCESS( result );
	}
	size_t selectedSurfaceFormatIndex = 0;
	auto   preferredSurfaceFormat     = le_format_to_vk( self->mSettings.format_hint );

	if ( ( surfaceProperties.availableSurfaceFormats.size() == 1 ) && ( surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].format == vk::Format::eUndefined ) ) {

		// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
		// there is no preferred format, and we must assume vk::Format::eB8G8R8A8Unorm.
		surfaceProperties.windowSurfaceFormat.format = vk::Format::eB8G8R8A8Unorm;

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

static vk::PresentModeKHR get_direct_presentmode( const le_swapchain_settings_t::khr_settings_t::Presentmode &presentmode_hint_ ) {
	using PresentMode = le_swapchain_settings_t::khr_settings_t::Presentmode;
	switch ( presentmode_hint_ ) {
	case ( PresentMode::eDefault ):
		return vk::PresentModeKHR::eFifo;
	case ( PresentMode::eImmediate ):
		return vk::PresentModeKHR::eImmediate;
	case ( PresentMode::eMailbox ):
		return vk::PresentModeKHR::eMailbox;
	case ( PresentMode::eFifo ):
		return vk::PresentModeKHR::eFifo;
	case ( PresentMode::eFifoRelaxed ):
		return vk::PresentModeKHR::eFifoRelaxed;
	case ( PresentMode::eSharedDemandRefresh ):
		return vk::PresentModeKHR::eSharedDemandRefresh;
	case ( PresentMode::eSharedContinuousRefresh ):
		return vk::PresentModeKHR::eSharedContinuousRefresh;
	}
	assert( false ); // something's wrong: control should never come here, switch needs to cover all cases.
	return vk::PresentModeKHR::eFifo;
}

// ----------------------------------------------------------------------

static void swapchain_attach_images( le_swapchain_o *base ) {
	auto self = static_cast<swp_direct_data_o *const>( base->data );

	auto result = self->device.getSwapchainImagesKHR( self->swapchainKHR, &self->mImagecount, nullptr );
	ASSERT_VK_SUCCESS( result );

	self->mImageRefs.resize( self->mImagecount );

	result = self->device.getSwapchainImagesKHR( self->swapchainKHR, &self->mImagecount, self->mImageRefs.data() );
	ASSERT_VK_SUCCESS( result );
}

// ----------------------------------------------------------------------

template <typename T>
static inline auto clamp( const T &val_, const T &min_, const T &max_ ) {
	return std::max( min_, ( std::min( val_, max_ ) ) );
}

// ----------------------------------------------------------------------

static void swapchain_direct_reset( le_swapchain_o *base, const le_swapchain_settings_t *settings_ ) {

	auto self = static_cast<swp_direct_data_o *const>( base->data );

	if ( settings_ ) {
		self->mSettings = *settings_;
	}

	// `settings_` may have been a nullptr in which case this operation is only valid
	// if self->mSettings has been fully set before.

	assert( self->mSettings.type == le_swapchain_settings_t::Type::LE_DIRECT_SWAPCHAIN );

	//	vk::Result err = ::vk::Result::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	swapchain_query_surface_capabilities( base );

	vk::SwapchainKHR oldSwapchain = self->swapchainKHR;

	const vk::SurfaceCapabilitiesKHR &       surfaceCapabilities = self->mSurfaceProperties.surfaceCapabilities;
	const std::vector<::vk::PresentModeKHR> &presentModes        = self->mSurfaceProperties.presentmodes;

	// Either set or get the swapchain surface extents

	if ( surfaceCapabilities.currentExtent.width == 0 ) {
		self->mSwapchainExtent.width  = self->mSettings.width_hint;
		self->mSwapchainExtent.height = self->mSettings.height_hint;
	} else {
		// set dimensions from surface extents if surface extents are available
		self->mSwapchainExtent = surfaceCapabilities.currentExtent;
	}

	auto presentModeHint = get_direct_presentmode( self->mSettings.khr_settings.presentmode_hint );

	for ( auto &p : presentModes ) {
		if ( p == presentModeHint ) {
			self->mPresentMode = p;
			break;
		}
	}

	if ( self->mPresentMode != presentModeHint ) {
		std::cout << "WARNING: Could not switch to selected Swapchain Present Mode ("
		          << vk::to_string( presentModeHint ) << "), "
		          << "falling back to: " << vk::to_string( self->mPresentMode ) << std::endl
		          << std::flush;
	}

	self->mImagecount = clamp( self->mSettings.imagecount_hint,
	                           surfaceCapabilities.minImageCount,
	                           surfaceCapabilities.maxImageCount );

	if ( self->mImagecount != self->mSettings.imagecount_hint ) {
		std::cout << " WARNING: Swapchain: Number of swapchain images was adjusted to: " << self->mImagecount << std::endl
		          << std::flush;
	}

	vk::SurfaceTransformFlagBitsKHR preTransform;
	// Note: this will be interesting for mobile devices
	// - if rotation and mirroring for the final output can
	// be defined here.

	if ( surfaceCapabilities.supportedTransforms & ::vk::SurfaceTransformFlagBitsKHR::eIdentity ) {
		preTransform = ::vk::SurfaceTransformFlagBitsKHR::eIdentity;
	} else {
		preTransform = surfaceCapabilities.currentTransform;
	}

	vk::SwapchainCreateInfoKHR swapChainCreateInfo;

	swapChainCreateInfo
	    .setSurface( self->surface )
	    .setMinImageCount( self->mImagecount )
	    .setImageFormat( self->mSurfaceProperties.windowSurfaceFormat.format )
	    .setImageColorSpace( self->mSurfaceProperties.windowSurfaceFormat.colorSpace )
	    .setImageExtent( self->mSwapchainExtent )
	    .setImageArrayLayers( 1 )
	    .setImageUsage( vk::ImageUsageFlagBits::eColorAttachment )
	    .setImageSharingMode( vk::SharingMode::eExclusive )
	    .setPreTransform( preTransform )
	    .setCompositeAlpha( vk::CompositeAlphaFlagBitsKHR::eOpaque )
	    .setPresentMode( self->mPresentMode )
	    .setClipped( VK_TRUE )
	    .setOldSwapchain( oldSwapchain );

	self->device.createSwapchainKHR( &swapChainCreateInfo, nullptr, &self->swapchainKHR );

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ( oldSwapchain ) {
		self->device.destroySwapchainKHR( oldSwapchain, nullptr );
		oldSwapchain = nullptr;
	}

	swapchain_attach_images( base );
}

// ----------------------------------------------------------------------

static le_swapchain_o *swapchain_direct_create( const le_swapchain_vk_api::swapchain_interface_t &interface, le_backend_o *backend, const le_swapchain_settings_t *settings ) {

	auto base  = new le_swapchain_o( interface );
	base->data = new swp_direct_data_o{};
	auto self  = static_cast<swp_direct_data_o *>( base->data );

	self->backend = backend;

	{
		using namespace le_backend_vk;
		self->device                         = private_backend_vk_i.get_vk_device( backend );
		self->physicalDevice                 = private_backend_vk_i.get_vk_physical_device( backend );
		self->instance                       = vk_instance_i.get_vk_instance( private_backend_vk_i.get_instance( backend ) );
		self->vk_graphics_queue_family_index = vk_device_i.get_default_graphics_queue_family_index( private_backend_vk_i.get_le_device( backend ) );
	}

	self->x11_display = XOpenDisplay( nullptr );

	auto phyDevice = vk::PhysicalDevice( self->physicalDevice );

	std::vector<vk::DisplayPropertiesKHR> display_props;
	uint32_t                              prop_count = 0;

	auto result = self->physicalDevice.getDisplayPropertiesKHR( &prop_count, nullptr ); // place properties data

	ASSERT_VK_SUCCESS( result );
	display_props.resize( prop_count );
	result = self->physicalDevice.getDisplayPropertiesKHR( &prop_count, display_props.data() ); // place properties data
	ASSERT_VK_SUCCESS( result );

	// We want to find out which display is secondary display
	// but we assume that the primary display will be listed first.
	self->display = display_props.back().display;

	getInstanceProc( self->instance, vkAcquireXlibDisplayEXT );
	auto vk_result = vkAcquireXlibDisplayEXT( phyDevice, self->x11_display, self->display );

	if ( vk_result != VK_SUCCESS ) {
		std::cerr << "ERROR: Unable to acquire display: '" << display_props.back().displayName << "'" << std::endl
		          << std::flush;
	}

	assert( vk_result == VK_SUCCESS );

	{
		uint32_t num_props{};
		result = phyDevice.getDisplayModePropertiesKHR( self->display, &num_props, nullptr );
		self->display_mode_properties.resize( num_props );
		result = phyDevice.getDisplayModePropertiesKHR( self->display, &num_props, self->display_mode_properties.data() );
	}
	// let's try to acquire this screen

	{
		vk::DisplaySurfaceCreateInfoKHR info;

		info
		    .setFlags( {} )
		    .setDisplayMode( self->display_mode_properties[ 0 ].displayMode )
		    .setPlaneIndex( 0 )
		    .setPlaneStackIndex( 0 )
		    .setTransform( vk::SurfaceTransformFlagBitsKHR::eRotate90 )
		    .setGlobalAlpha( 1.f )
		    .setAlphaMode( vk::DisplayPlaneAlphaFlagBitsKHR::eOpaque )
		    .setImageExtent( self->display_mode_properties[ 0 ].parameters.visibleRegion );

		auto instance = vk::Instance( self->instance );
		result        = instance.createDisplayPlaneSurfaceKHR( &info, nullptr, &self->surface );

		ASSERT_VK_SUCCESS( result );

		self->mSwapchainExtent.height = uint32_t( info.imageExtent.height );
		self->mSwapchainExtent.width  = uint32_t( info.imageExtent.width );
	}

	swapchain_direct_reset( base, settings );

	return base;
}

// ----------------------------------------------------------------------

static void swapchain_direct_destroy( le_swapchain_o *base ) {

	auto self = static_cast<swp_direct_data_o *const>( base->data );

	vk::Device device = self->device;

	device.destroySwapchainKHR( self->swapchainKHR, nullptr );
	self->swapchainKHR = nullptr;

	vk::Instance instance = self->instance;
	instance.destroySurfaceKHR( self->surface, nullptr );
	self->surface = nullptr;

	getInstanceProc( self->instance, vkReleaseDisplayEXT );
	vkReleaseDisplayEXT( self->physicalDevice, self->display );

	XCloseDisplay( self->x11_display );

	delete self; // delete object's data
	delete base; // delete object
}

// ----------------------------------------------------------------------

static bool swapchain_direct_acquire_next_image( le_swapchain_o *base, VkSemaphore semaphorePresentComplete_, uint32_t &imageIndex_ ) {

	auto self = static_cast<swp_direct_data_o *const>( base->data );
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

static bool swapchain_direct_present( le_swapchain_o *base, VkQueue queue_, VkSemaphore renderCompleteSemaphore_, uint32_t *pImageIndex ) {

	auto self = static_cast<swp_direct_data_o *const>( base->data );

	vk::PresentInfoKHR presentInfo;

	auto renderCompleteSemaphore = vk::Semaphore{ renderCompleteSemaphore_ };

	presentInfo
	    .setWaitSemaphoreCount( 1 )
	    .setPWaitSemaphores( &renderCompleteSemaphore )
	    .setSwapchainCount( 1 )
	    .setPSwapchains( &self->swapchainKHR )
	    .setPImageIndices( pImageIndex )
	    .setPResults( nullptr );

	auto result = vkQueuePresentKHR( queue_, reinterpret_cast<VkPresentInfoKHR *>( &presentInfo ) );

	if ( vk::Result( result ) == vk::Result::eErrorOutOfDateKHR ) {
		return false;
	}

	return true;
};

// ----------------------------------------------------------------------

static VkImage swapchain_direct_get_image( le_swapchain_o *base, uint32_t index ) {

	auto self = static_cast<swp_direct_data_o *const>( base->data );

#ifndef NDEBUG
	assert( index < self->mImageRefs.size() );
#endif
	return self->mImageRefs[ index ];
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR *swapchain_direct_get_surface_format( le_swapchain_o *base ) {
	auto self = static_cast<swp_direct_data_o *const>( base->data );
	return &reinterpret_cast<VkSurfaceFormatKHR &>( self->mSurfaceProperties.windowSurfaceFormat );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_direct_get_image_width( le_swapchain_o *base ) {
	auto self = static_cast<swp_direct_data_o *const>( base->data );
	return self->mSwapchainExtent.width;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_direct_get_image_height( le_swapchain_o *base ) {
	auto self = static_cast<swp_direct_data_o *const>( base->data );
	return self->mSwapchainExtent.height;
}

// ----------------------------------------------------------------------

static size_t swapchain_direct_get_swapchain_images_count( le_swapchain_o *base ) {
	auto self = static_cast<swp_direct_data_o *const>( base->data );
	return self->mImagecount;
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_instance_extensions( const le_swapchain_settings_t *, char const ***exts, size_t *num_exts ) {

	static std::array<char const *, 6> extensions = {
	    VK_KHR_DISPLAY_EXTENSION_NAME,
	    "VK_EXT_direct_mode_display",
	    "VK_KHR_xlib_surface",
	    "VK_KHR_surface",
	    "VK_EXT_acquire_xlib_display",
	    "VK_EXT_display_surface_counter",
	};

	*exts     = extensions.data();
	*num_exts = extensions.size();
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_device_extensions( const le_swapchain_settings_t *, char const ***exts, size_t *num_exts ) {

	static std::array<char const *, 2> extensions = {
	    "VK_EXT_display_control",
	    "VK_KHR_swapchain",
	};

	*exts     = extensions.data();
	*num_exts = extensions.size();
}

// ----------------------------------------------------------------------

void register_le_swapchain_direct_api( void *api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i = api->swapchain_direct_i;

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
