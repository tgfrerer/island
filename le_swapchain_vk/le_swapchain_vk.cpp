#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>

#ifndef NDEBUG
    #include <assert.h>
#endif

// ----------------------------------------------------------------------

struct SurfaceProperties {
	vk::SurfaceFormatKHR                windowSurfaceFormat;
	vk::SurfaceCapabilitiesKHR          surfaceCapabilities;
	VkBool32                            queried          = VK_FALSE;
	VkBool32                            presentSupported = VK_FALSE;
	std::vector<::vk::PresentModeKHR>   presentmodes;
	std::vector<::vk::SurfaceFormatKHR> availableSurfaceFormats;
};

struct le_backend_swapchain_o {
	le_swapchain_vk_api::settings_o mSettings;
	vk::PresentModeKHR              mPresentMode     = vk::PresentModeKHR::eFifo;
	uint32_t                        mImagecount      = 0;
	uint32_t                        mImageIndex      = uint32_t(~0); // current image index
	vk::SwapchainKHR                mSwapchain       = nullptr;
	vk::Extent2D                    mSwapchainExtent = {};
	SurfaceProperties               mSurfaceProperties;
	std::vector<vk::Image>          mImageRefs; // owned by SwapchainKHR, don't delete
	std::vector<vk::ImageView>      mImageViews;
	uint32_t                        referenceCount = 0;
};

// ----------------------------------------------------------------------

static void swapchain_query_surface_capabilities( le_backend_swapchain_o *self ) {

	if ( self->mSurfaceProperties.queried == true ) {
		return;
	}

	// we need to find out if the current physical device supports PRESENT

	const auto &settings          = self->mSettings;
	auto &      surfaceProperties = self->mSurfaceProperties;
	auto        physicalDevice    = vk::PhysicalDevice{settings.vk_physical_device};

	physicalDevice.getSurfaceSupportKHR( settings.vk_graphics_queue_family_index,
	                                     settings.vk_surface,
	                                     &surfaceProperties.presentSupported );

	// Get list of supported surface formats
	surfaceProperties.availableSurfaceFormats = physicalDevice.getSurfaceFormatsKHR( settings.vk_surface );
	surfaceProperties.surfaceCapabilities     = physicalDevice.getSurfaceCapabilitiesKHR( settings.vk_surface );
	surfaceProperties.presentmodes            = physicalDevice.getSurfacePresentModesKHR( settings.vk_surface );

	// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ( ( surfaceProperties.availableSurfaceFormats.size() == 1 ) && ( surfaceProperties.availableSurfaceFormats[ 0 ].format == ::vk::Format::eUndefined ) ) {
		surfaceProperties.windowSurfaceFormat.format = ::vk::Format::eB8G8R8A8Unorm;
	} else {
		// Always select the first available color format
		// If you need a specific format (e.g. SRGB) you'd need to
		// iterate over the list of available surface formats and
		// check for its presence
		surfaceProperties.windowSurfaceFormat.format = surfaceProperties.availableSurfaceFormats[ 0 ].format;
	}

	// always select the first available color space
	surfaceProperties.windowSurfaceFormat.colorSpace = surfaceProperties.availableSurfaceFormats[ 0 ].colorSpace;

	// ofLog() << "Present supported: " << ( mSurfaceProperties.presentSupported ? "TRUE" : "FALSE" );
	self->mSurfaceProperties.queried = true;
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
}

// ----------------------------------------------------------------------

static void swapchain_destroy_image_views( le_backend_swapchain_o *self ) {

	vk::Device device = self->mSettings.vk_device;

	for ( auto &imageView : self->mImageViews ) {
		// If there were any images available at all to iterate over, this means
		// that the swapchain was re-created.
		// This happens on window resize, for example.
		// Therefore we have to destroy old ImageView object(s).
		device.destroyImageView( imageView );
	}

	self->mImageViews.clear();

}

// ----------------------------------------------------------------------

static void swapchain_create_image_views(le_backend_swapchain_o* self){

	vk::Device device = self->mSettings.vk_device;

	self->mImageViews.reserve( self->mImagecount );

	for ( auto &imageRef : self->mImageRefs ) {

		::vk::ImageSubresourceRange subresourceRange;
		subresourceRange
		    .setAspectMask     ( vk::ImageAspectFlagBits::eColor )
		    .setBaseMipLevel   ( 0 )
		    .setLevelCount     ( 1 )
		    .setBaseArrayLayer ( 0 )
		    .setLayerCount     ( 1 );

		::vk::ImageViewCreateInfo imageViewCreateInfo;
		imageViewCreateInfo
		    .setImage           ( imageRef )
		    .setViewType        ( vk::ImageViewType::e2D )
		    .setFormat          ( self->mSurfaceProperties.windowSurfaceFormat.format )
		    .setComponents      ( vk::ComponentMapping() )
		    .setSubresourceRange( subresourceRange );

		// create image view for color image
		self->mImageViews.emplace_back( device.createImageView( imageViewCreateInfo ) );
	}

}

// ----------------------------------------------------------------------

static void swapchain_attach_images(le_backend_swapchain_o *self)
{

	vk::Device device = self->mSettings.vk_device;

	self->mImageRefs  = device.getSwapchainImagesKHR( self->mSwapchain );
	self->mImagecount = uint32_t( self->mImageRefs.size() );

}

// ----------------------------------------------------------------------

static void swapchain_reset( le_backend_swapchain_o *self, const le_swapchain_vk_api::settings_o *settings_ ) {

	if ( settings_ ) {
		self->mSettings = *settings_;
	}

	//	::vk::Result err = ::vk::Result::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	swapchain_query_surface_capabilities( self );

	vk::SwapchainKHR                         oldSwapchain        = self->mSwapchain;
	vk::Device                               device              = self->mSettings.vk_device;
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
		          << "falling back to: " << vk::to_string( self->mPresentMode );
	}

	self->mImagecount = std::clamp( self->mSettings.imagecount_hint,
	                                surfaceCapabilities.minImageCount,
	                                surfaceCapabilities.maxImageCount );

	if ( self->mImagecount != self->mSettings.imagecount_hint ) {
		std::cout << " WARNING: Swapchain: Number of swapchain images was adjusted to: " << self->mImagecount;
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
	    .setSurface          ( self->mSettings.vk_surface )
	    .setMinImageCount    ( self->mImagecount )
	    .setImageFormat      ( self->mSurfaceProperties.windowSurfaceFormat.format )
	    .setImageColorSpace  ( self->mSurfaceProperties.windowSurfaceFormat.colorSpace )
	    .setImageExtent      ( self->mSwapchainExtent )
	    .setImageArrayLayers ( 1 )
	    .setImageUsage       ( vk::ImageUsageFlagBits::eColorAttachment )
	    .setImageSharingMode ( vk::SharingMode::eExclusive )
	    .setPreTransform     ( preTransform )
	    .setCompositeAlpha   ( vk::CompositeAlphaFlagBitsKHR::eOpaque )
	    .setPresentMode      ( self->mPresentMode )
	    .setClipped          ( VK_TRUE )
	    .setOldSwapchain     ( oldSwapchain )
	    ;

	self->mSwapchain = device.createSwapchainKHR( swapChainCreateInfo );

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ( oldSwapchain ) {
		device.destroySwapchainKHR( oldSwapchain );
		oldSwapchain = nullptr;
	}

	swapchain_destroy_image_views(self);
	swapchain_attach_images( self );
	swapchain_create_image_views(self);
}

// ----------------------------------------------------------------------

static le_backend_swapchain_o *swapchain_create( const le_swapchain_vk_api::settings_o *settings_ ) {
	auto self = new ( le_backend_swapchain_o );

	swapchain_reset( self, settings_ );

	return self;
}

// ----------------------------------------------------------------------

static bool swapchain_acquire_next_image( le_backend_swapchain_o* self, VkSemaphore semaphorePresentComplete_, uint32_t& imageIndex_ ){

	// This method will return the next avaliable vk image index for this swapchain, possibly
	// before this image is available for writing. Image will be ready for writing when
	// semaphorePresentComplete is signalled.

	auto result = vkAcquireNextImageKHR( self->mSettings.vk_device, self->mSwapchain, 0, semaphorePresentComplete_, nullptr, &imageIndex_ );

	switch ( result ) {
	case VK_SUCCESS:
		self->mImageIndex = imageIndex_;
	    return true;
	    break;
	case VK_SUBOPTIMAL_KHR:         // | fall-through
	case VK_ERROR_SURFACE_LOST_KHR: // |
	case VK_ERROR_OUT_OF_DATE_KHR:  // |
		swapchain_reset( self, nullptr );
	    return false;
	    break;
	default:
	    return false;
	}

}

// ----------------------------------------------------------------------

static VkImage swapchain_get_image( le_backend_swapchain_o *self, uint32_t index ) {
#ifndef NDEBUG
	assert( index < self->mImageRefs.size() );
#endif
	return self->mImageRefs[ index ];
}

// ----------------------------------------------------------------------

static VkImageView swapchain_get_image_view( le_backend_swapchain_o *self, uint32_t index ) {
#ifndef NDEBUG
	assert( index < self->mImageViews.size() );
#endif
	return self->mImageViews[ index ];
}

// ----------------------------------------------------------------------

static size_t swapchain_get_swapchain_images_count( le_backend_swapchain_o *self ) {
	return self->mImagecount;
}

// ----------------------------------------------------------------------

static void swapchain_destroy( le_backend_swapchain_o *self_ ) {

	vk::Device device = self_->mSettings.vk_device;

	swapchain_destroy_image_views(self_);

	device.destroySwapchainKHR(self_->mSwapchain);
	self_->mSwapchain = nullptr;

	delete ( self_ );
	self_ = nullptr;
}

// ----------------------------------------------------------------------

static void swapchain_increase_reference_count( le_backend_swapchain_o *self ) {
	++self->referenceCount;
}

// ----------------------------------------------------------------------

static void swapchain_decrease_reference_count( le_backend_swapchain_o *self ) {
	--self->referenceCount;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_reference_count( le_backend_swapchain_o *self ) {
	return self->referenceCount;
}

// ----------------------------------------------------------------------

void register_le_swapchain_vk_api( void *api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i = api->swapchain_i;

	swapchain_i.create                     = swapchain_create;
	swapchain_i.reset                      = swapchain_reset;
	swapchain_i.acquire_next_image         = swapchain_acquire_next_image;
	swapchain_i.get_image                  = swapchain_get_image;
	swapchain_i.get_image_view             = swapchain_get_image_view;
	swapchain_i.destroy                    = swapchain_destroy;
	swapchain_i.increase_reference_count   = swapchain_increase_reference_count;
	swapchain_i.decrease_reference_count   = swapchain_decrease_reference_count;
	swapchain_i.get_reference_count        = swapchain_get_reference_count;
	swapchain_i.get_swapchain_images_count = swapchain_get_swapchain_images_count;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
