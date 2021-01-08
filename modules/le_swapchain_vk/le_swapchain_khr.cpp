#include "le_backend_vk/le_backend_vk.h"
#include "le_renderer/private/le_renderer_types.h"
#include "include/internal/le_swapchain_vk_common.h"
#include "le_window/le_window.h"

#define VULKAN_HPP_DISABLE_ENHANCED_MODE
#define VULKAN_HPP_NO_SMART_HANDLE
#define VULKAN_HPP_DISABLE_IMPLICIT_RESULT_VALUE_CAST
#include <vulkan/vulkan.hpp>

#include <iostream>
#include <vector>

struct SurfaceProperties {
	vk::SurfaceFormatKHR              windowSurfaceFormat;
	vk::SurfaceCapabilitiesKHR        surfaceCapabilities;
	VkBool32                          presentSupported = VK_FALSE;
	std::vector<vk::PresentModeKHR>   presentmodes;
	std::vector<vk::SurfaceFormatKHR> availableSurfaceFormats;
};

struct khr_data_o {
	le_swapchain_settings_t mSettings                      = {};
	le_window_o *           window                         = nullptr;
	le_backend_o *          backend                        = nullptr;
	uint32_t                mImagecount                    = 0;
	uint32_t                mImageIndex                    = uint32_t( ~0 ); // current image index
	vk::SwapchainKHR        swapchainKHR                   = nullptr;
	vk::Extent2D            mSwapchainExtent               = {};
	vk::PresentModeKHR      mPresentMode                   = vk::PresentModeKHR::eFifo;
	uint32_t                vk_graphics_queue_family_index = 0;
	SurfaceProperties       mSurfaceProperties             = {};
	std::vector<vk::Image>  mImageRefs                     = {}; // owned by SwapchainKHR, don't delete
	vk::Device              device                         = nullptr;
	vk::PhysicalDevice      physicalDevice                 = nullptr;
};

// ----------------------------------------------------------------------

static inline vk::Format le_format_to_vk( const le::Format &format ) noexcept {
	return vk::Format( format );
}

// ----------------------------------------------------------------------

static void swapchain_query_surface_capabilities( le_swapchain_o *base ) {

	auto vk_result_assert_success = []( vk::Result const &&result ) {
		if ( result != vk::Result::eSuccess ) {
			std::cerr << "Error: Vulkan operation returned: " << vk::to_string( result ) << ", but we expected vk::Result::eSuccess"
			          << std::endl;
		}
		assert( result == vk::Result::eSuccess && "Vulkan operation must succeed" );
	};

	// we need to find out if the current physical device supports PRESENT

	auto self = static_cast<khr_data_o *const>( base->data );

	using namespace le_backend_vk;

	const auto &settings          = self->mSettings.khr_settings;
	auto &      surfaceProperties = self->mSurfaceProperties;

	vk_result_assert_success( self->physicalDevice.getSurfaceSupportKHR(
	    self->vk_graphics_queue_family_index,
	    settings.vk_surface,
	    &surfaceProperties.presentSupported ) );

	// Get list of supported surface formats
	{
		uint32_t count = 0;

		vk_result_assert_success( self->physicalDevice.getSurfaceFormatsKHR( settings.vk_surface, &count, nullptr ) );
		surfaceProperties.availableSurfaceFormats.resize( count );
		vk_result_assert_success( self->physicalDevice.getSurfaceFormatsKHR( settings.vk_surface, &count, surfaceProperties.availableSurfaceFormats.data() ) );

		vk_result_assert_success( self->physicalDevice.getSurfaceCapabilitiesKHR( settings.vk_surface, &surfaceProperties.surfaceCapabilities ) );
		vk_result_assert_success( self->physicalDevice.getSurfacePresentModesKHR( settings.vk_surface, &count, nullptr ) );
		surfaceProperties.presentmodes.resize( count );
		vk_result_assert_success( self->physicalDevice.getSurfacePresentModesKHR( settings.vk_surface, &count, surfaceProperties.presentmodes.data() ) );
	}

	size_t selectedSurfaceFormatIndex = 0;
	auto   preferredSurfaceFormat     = le_format_to_vk( self->mSettings.format_hint );

	if ( ( surfaceProperties.availableSurfaceFormats.size() == 1 ) &&
	     ( surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].format == vk::Format::eUndefined ) ) {

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

static vk::PresentModeKHR get_khr_presentmode( const le_swapchain_settings_t::khr_settings_t::Presentmode &presentmode_hint_ ) {
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

static void check_vk_result( vk::Result &&result ) {
	if ( result != vk::Result::eSuccess ) {
		std::cerr << "Error: Vulkan operation returned: " << vk::to_string( result ) << ", but we expected vk::Result::eSuccess"
		          << std::endl;
	}
	assert( result == vk::Result::eSuccess && "Vulkan operation must succeed" );
};
// ----------------------------------------------------------------------

static void swapchain_attach_images( le_swapchain_o *base ) {
	auto self = static_cast<khr_data_o *const>( base->data );
	check_vk_result( self->device.getSwapchainImagesKHR( self->swapchainKHR, &self->mImagecount, nullptr ) );
	if ( self->mImagecount ) {
		self->mImageRefs.resize( self->mImagecount );
		check_vk_result( self->device.getSwapchainImagesKHR( self->swapchainKHR, &self->mImagecount, self->mImageRefs.data() ) );
	}
}

// ----------------------------------------------------------------------

template <typename T>
static inline auto clamp( const T &val_, const T &min_, const T &max_ ) {
	return std::max( min_, ( std::min( val_, max_ ) ) );
}

// ----------------------------------------------------------------------

static void swapchain_khr_reset( le_swapchain_o *base, const le_swapchain_settings_t *settings_ ) {

	auto self = static_cast<khr_data_o *>( base->data );

	if ( settings_ ) {
		self->mSettings = *settings_;
	}

	{
		using namespace le_window;

		self->mSettings.width_hint  = window_i.get_surface_width( self->mSettings.khr_settings.window );
		self->mSettings.height_hint = window_i.get_surface_height( self->mSettings.khr_settings.window );
	}

	// `settings_` may have been a nullptr in which case this operation is only valid
	// if self->mSettings has been fully set before.

	assert( self->mSettings.type == le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN );

	//	vk::Result err = ::vk::Result::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	swapchain_query_surface_capabilities( base );

	VkSwapchainKHR oldSwapchain = self->swapchainKHR;

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

	auto presentModeHint = get_khr_presentmode( self->mSettings.khr_settings.presentmode_hint );

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
	    .setSurface( self->mSettings.khr_settings.vk_surface )
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

	//	self->device.createSwapchainKHR( &swapChainCreateInfo, nullptr, &self->swapchainKHR );
	{
		VkSwapchainCreateInfoKHR info = swapChainCreateInfo;
		VkSwapchainKHR           swapchain;
		vkCreateSwapchainKHR( self->device, &info, nullptr, &swapchain );
		self->swapchainKHR = swapchain;
	}
	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ( oldSwapchain ) {
		self->device.destroySwapchainKHR( oldSwapchain, nullptr );
		oldSwapchain = nullptr;
	}

	swapchain_attach_images( base );
}

// ----------------------------------------------------------------------

static le_swapchain_o *swapchain_khr_create( const le_swapchain_vk_api::swapchain_interface_t &interface, le_backend_o *backend, const le_swapchain_settings_t *settings ) {

	auto base  = new le_swapchain_o( interface );
	base->data = new khr_data_o{};
	auto self  = static_cast<khr_data_o *>( base->data );

	self->backend = backend;

	{
		using namespace le_backend_vk;
		self->device                         = private_backend_vk_i.get_vk_device( backend );
		self->physicalDevice                 = private_backend_vk_i.get_vk_physical_device( backend );
		auto le_device                       = private_backend_vk_i.get_le_device( backend );
		self->vk_graphics_queue_family_index = vk_device_i.get_default_graphics_queue_family_index( le_device );
	}

	self->swapchainKHR = nullptr;

	swapchain_khr_reset( base, settings );

	return base;
}

// ----------------------------------------------------------------------

static void swapchain_khr_destroy( le_swapchain_o *base ) {

	auto self = static_cast<khr_data_o *const>( base->data );

	vk::Device device = self->device;

	vkDestroySwapchainKHR( device, self->swapchainKHR, nullptr );
	self->swapchainKHR = nullptr;

	delete self; // delete object's data
	delete base; // delete object
}

// ----------------------------------------------------------------------

static bool swapchain_khr_acquire_next_image( le_swapchain_o *base, VkSemaphore semaphorePresentComplete_, uint32_t &imageIndex_ ) {

	auto self = static_cast<khr_data_o *const>( base->data );
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

static VkImage swapchain_khr_get_image( le_swapchain_o *base, uint32_t index ) {

	auto self = static_cast<khr_data_o *const>( base->data );

#ifndef NDEBUG
	assert( index < self->mImageRefs.size() );
#endif
	return self->mImageRefs[ index ];
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR *swapchain_khr_get_surface_format( le_swapchain_o *base ) {
	auto self = static_cast<khr_data_o *const>( base->data );
	return &reinterpret_cast<VkSurfaceFormatKHR &>( self->mSurfaceProperties.windowSurfaceFormat );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_khr_get_image_width( le_swapchain_o *base ) {
	auto self = static_cast<khr_data_o *const>( base->data );
	return self->mSwapchainExtent.width;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_khr_get_image_height( le_swapchain_o *base ) {

	auto self = static_cast<khr_data_o *const>( base->data );
	return self->mSwapchainExtent.height;
}

// ----------------------------------------------------------------------

static size_t swapchain_khr_get_swapchain_images_count( le_swapchain_o *base ) {
	auto self = static_cast<khr_data_o *const>( base->data );
	return self->mImagecount;
}

// ----------------------------------------------------------------------

static bool swapchain_khr_present( le_swapchain_o *base, VkQueue queue_, VkSemaphore renderCompleteSemaphore_, uint32_t *pImageIndex ) {

	auto self = static_cast<khr_data_o *const>( base->data );

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
		// FIXME: handle swapchain resize event properly
		//		std::cout << "Out of date detected - this most commonly indicates surface resize." << std::endl
		//		          << std::flush;
		return false;
	}

	return true;
};

static void swapchain_get_required_vk_instance_extensions( const le_swapchain_settings_t *, char const ***exts, size_t *num_exts ) {

	static std::array<char const *, 1> extensions = {
	    VK_KHR_SURFACE_EXTENSION_NAME,
	};

	*exts     = extensions.data();
	*num_exts = extensions.size();
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_device_extensions( const le_swapchain_settings_t *, char const ***exts, size_t *num_exts ) {

	static std::array<char const *, 1> extensions = {
	    "VK_KHR_swapchain",
	};

	*exts     = extensions.data();
	*num_exts = extensions.size();
}

// ----------------------------------------------------------------------

void register_le_swapchain_khr_api( void *api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i = api->swapchain_khr_i;

	swapchain_i.create                              = swapchain_khr_create;
	swapchain_i.destroy                             = swapchain_khr_destroy;
	swapchain_i.reset                               = swapchain_khr_reset;
	swapchain_i.acquire_next_image                  = swapchain_khr_acquire_next_image;
	swapchain_i.get_image                           = swapchain_khr_get_image;
	swapchain_i.get_image_width                     = swapchain_khr_get_image_width;
	swapchain_i.get_image_height                    = swapchain_khr_get_image_height;
	swapchain_i.get_surface_format                  = swapchain_khr_get_surface_format;
	swapchain_i.get_images_count                    = swapchain_khr_get_swapchain_images_count;
	swapchain_i.present                             = swapchain_khr_present;
	swapchain_i.get_required_vk_instance_extensions = swapchain_get_required_vk_instance_extensions;
	swapchain_i.get_required_vk_device_extensions   = swapchain_get_required_vk_device_extensions;
}
