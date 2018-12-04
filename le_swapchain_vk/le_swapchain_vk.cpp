#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_backend_vk/le_backend_vk.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>
#include <assert.h>

// ----------------------------------------------------------------------

struct SurfaceProperties {
	vk::SurfaceFormatKHR                windowSurfaceFormat;
	vk::SurfaceCapabilitiesKHR          surfaceCapabilities;
	VkBool32                            presentSupported = VK_FALSE;
	std::vector<::vk::PresentModeKHR>   presentmodes;
	std::vector<::vk::SurfaceFormatKHR> availableSurfaceFormats;
};

struct le_swapchain_o {
	le_swapchain_vk_settings_t mSettings;
	le_backend_o *             backend;
	uint32_t                   mImagecount      = 0;
	uint32_t                   mImageIndex      = uint32_t( ~0 ); // current image index
	vk::SwapchainKHR           swapchainKHR     = nullptr;
	vk::Extent2D               mSwapchainExtent = {};
	vk::PresentModeKHR         mPresentMode     = vk::PresentModeKHR::eFifo;
	uint32_t                   referenceCount   = 0;
	SurfaceProperties          mSurfaceProperties;
	std::vector<vk::Image>     mImageRefs; // owned by SwapchainKHR, don't delete
	vk::Device                 device                         = nullptr;
	vk::PhysicalDevice         physicalDevice                 = nullptr;
	uint32_t                   vk_graphics_queue_family_index = 0;
};

// ----------------------------------------------------------------------

static void swapchain_query_surface_capabilities( le_swapchain_o *self ) {

	// we need to find out if the current physical device supports PRESENT

	using namespace le_backend_vk;

	const auto &settings          = self->mSettings;
	auto &      surfaceProperties = self->mSurfaceProperties;

	self->physicalDevice.getSurfaceSupportKHR( self->vk_graphics_queue_family_index,
	                                           settings.vk_surface,
	                                           &surfaceProperties.presentSupported );

	// Get list of supported surface formats
	surfaceProperties.availableSurfaceFormats = self->physicalDevice.getSurfaceFormatsKHR( settings.vk_surface );
	surfaceProperties.surfaceCapabilities     = self->physicalDevice.getSurfaceCapabilitiesKHR( settings.vk_surface );
	surfaceProperties.presentmodes            = self->physicalDevice.getSurfacePresentModesKHR( settings.vk_surface );

	size_t selectedSurfaceFormatIndex = 0;
	auto   preferredSurfaceFormat     = vk::Format::eB8G8R8A8Unorm;

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
			if ( surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].format == preferredSurfaceFormat ) {
				selectedSurfaceFormatIndex = i;
				break;
			}
		}
		surfaceProperties.windowSurfaceFormat.format = surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].format;
	}

	// always select the first available color space
	surfaceProperties.windowSurfaceFormat.colorSpace = surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].colorSpace;
}

// ----------------------------------------------------------------------

vk::PresentModeKHR get_khr_presentmode( const le::Swapchain::Presentmode &presentmode_hint_ ) {
	switch ( presentmode_hint_ ) {
	case ( le::Swapchain::Presentmode::eDefault ):
	    return vk::PresentModeKHR::eFifo;
	case ( le::Swapchain::Presentmode::eImmediate ):
	    return vk::PresentModeKHR::eImmediate;
	case ( le::Swapchain::Presentmode::eMailbox ):
	    return vk::PresentModeKHR::eMailbox;
	case ( le::Swapchain::Presentmode::eFifo ):
	    return vk::PresentModeKHR::eFifo;
	case ( le::Swapchain::Presentmode::eFifoRelaxed ):
	    return vk::PresentModeKHR::eFifoRelaxed;
	case ( le::Swapchain::Presentmode::eSharedDemandRefresh ):
	    return vk::PresentModeKHR::eSharedDemandRefresh;
	case ( le::Swapchain::Presentmode::eSharedContinuousRefresh ):
	    return vk::PresentModeKHR::eSharedContinuousRefresh;
	}
	assert( false ); // something's wrong: control should never come here, switch needs to cover all cases.
	return vk::PresentModeKHR::eFifo;
}

// ----------------------------------------------------------------------

static void swapchain_attach_images( le_swapchain_o *self ) {
	self->mImageRefs  = self->device.getSwapchainImagesKHR( self->swapchainKHR );
	self->mImagecount = uint32_t( self->mImageRefs.size() );
}

// ----------------------------------------------------------------------

template <typename T>
static inline auto clamp( const T &val_, const T &min_, const T &max_ ) {
	return std::max( min_, ( std::min( val_, max_ ) ) );
}

// ----------------------------------------------------------------------

static void swapchain_reset( le_swapchain_o *self, const le_swapchain_vk_settings_t *settings_ ) {

	if ( settings_ ) {
		self->mSettings = *settings_;
	}

	//	::vk::Result err = ::vk::Result::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	swapchain_query_surface_capabilities( self );

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

	auto presentModeHint = get_khr_presentmode( self->mSettings.presentmode_hint );

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

	::vk::SurfaceTransformFlagBitsKHR preTransform;
	// Note: this will be interesting for mobile devices
	// - if rotation and mirroring for the final output can
	// be defined here.

	if ( surfaceCapabilities.supportedTransforms & ::vk::SurfaceTransformFlagBitsKHR::eIdentity ) {
		preTransform = ::vk::SurfaceTransformFlagBitsKHR::eIdentity;
	} else {
		preTransform = surfaceCapabilities.currentTransform;
	}

	::vk::SwapchainCreateInfoKHR swapChainCreateInfo;

	swapChainCreateInfo
	    .setSurface( self->mSettings.vk_surface )
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

	self->swapchainKHR = self->device.createSwapchainKHR( swapChainCreateInfo );

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ( oldSwapchain ) {
		self->device.destroySwapchainKHR( oldSwapchain );
		oldSwapchain = nullptr;
	}

	swapchain_attach_images( self );
}

// ----------------------------------------------------------------------

static le_swapchain_o *swapchain_create( le_backend_o *backend, const le_swapchain_vk_settings_t *settings_ ) {
	auto self = new ( le_swapchain_o );

	self->backend = backend;

	{
		using namespace le_backend_vk;
		self->device                         = private_backend_vk_i.get_vk_device( backend );
		self->physicalDevice                 = private_backend_vk_i.get_vk_physical_device( backend );
		auto le_device                       = private_backend_vk_i.get_le_device( backend );
		self->vk_graphics_queue_family_index = vk_device_i.get_default_graphics_queue_family_index( le_device );
	}

	swapchain_reset( self, settings_ );

	return self;
}

// ----------------------------------------------------------------------

static bool swapchain_acquire_next_image( le_swapchain_o *self, VkSemaphore semaphorePresentComplete_, uint32_t &imageIndex_ ) {

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

static VkImage swapchain_get_image( le_swapchain_o *self, uint32_t index ) {
#ifndef NDEBUG
	assert( index < self->mImageRefs.size() );
#endif
	return self->mImageRefs[ index ];
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR *swapchain_get_surface_format( le_swapchain_o *self ) {
	return &reinterpret_cast<VkSurfaceFormatKHR &>( self->mSurfaceProperties.windowSurfaceFormat );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_width( le_swapchain_o *self ) {
	return self->mSwapchainExtent.width;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_height( le_swapchain_o *self ) {
	return self->mSwapchainExtent.height;
}

// ----------------------------------------------------------------------

static size_t swapchain_get_swapchain_images_count( le_swapchain_o *self ) {
	return self->mImagecount;
}

// ----------------------------------------------------------------------

static void swapchain_destroy( le_swapchain_o *self_ ) {

	vk::Device device = self_->device;

	device.destroySwapchainKHR( self_->swapchainKHR );
	self_->swapchainKHR = nullptr;

	delete ( self_ );
	self_ = nullptr;
}

// ----------------------------------------------------------------------

static bool swapchain_present( le_swapchain_o *self, VkQueue queue_, VkSemaphore renderCompleteSemaphore_, uint32_t *pImageIndex ) {
	vk::PresentInfoKHR presentInfo;

	auto renderCompleteSemaphore = vk::Semaphore{renderCompleteSemaphore_};

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
		std::cout << "Out of date detected - this most commonly indicates surface resize." << std::endl
		          << std::flush;
		return false;
	}

	return true;
};

// ----------------------------------------------------------------------

static void swapchain_increase_reference_count( le_swapchain_o *self ) {
	++self->referenceCount;
}

// ----------------------------------------------------------------------

static void swapchain_decrease_reference_count( le_swapchain_o *self ) {
	--self->referenceCount;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_reference_count( le_swapchain_o *self ) {
	return self->referenceCount;
}

// ----------------------------------------------------------------------

void register_le_swapchain_vk_api( void *api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i = api->swapchain_i;

	swapchain_i.create             = swapchain_create;
	swapchain_i.destroy            = swapchain_destroy;
	swapchain_i.reset              = swapchain_reset;
	swapchain_i.acquire_next_image = swapchain_acquire_next_image;
	swapchain_i.get_image          = swapchain_get_image;
	swapchain_i.get_image_width    = swapchain_get_image_width;
	swapchain_i.get_image_height   = swapchain_get_image_height;
	swapchain_i.get_surface_format = swapchain_get_surface_format;
	swapchain_i.get_images_count   = swapchain_get_swapchain_images_count;
	swapchain_i.present            = swapchain_present;

	swapchain_i.increase_reference_count = swapchain_increase_reference_count;
	swapchain_i.decrease_reference_count = swapchain_decrease_reference_count;
	swapchain_i.get_reference_count      = swapchain_get_reference_count;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
