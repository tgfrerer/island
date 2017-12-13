#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>

struct le_backend_swapchain_vk_settings_o {
	uint32_t                         width_hint       = 640;
	uint32_t                         height_hint      = 480;
	uint32_t                         imagecount_hint  = 3;
	le_swapchain_vk_api::Presentmode presentmode_hint = le_swapchain_vk_api::Presentmode::eFifo;
	VkDevice                         vk_device_ref    = nullptr; // owned by backend
	VkSurfaceKHR                     vk_surface_ref   = nullptr; // owned by window
};

// ----------------------------------------------------------------------

struct le_backend_swapchain_o {
	le_backend_swapchain_vk_settings_o mSettings;
	vk::PresentModeKHR                 presentMode = vk::PresentModeKHR::eFifo;
	uint32_t                           imagecount  = 0;
	uint32_t                           width       = 0;
	uint32_t                           height      = 0;
	vk::SwapchainKHR                   swapchain   = nullptr;
};

// ----------------------------------------------------------------------

static le_backend_swapchain_vk_settings_o *swapchain_settings_create() {
	auto self = new ( le_backend_swapchain_vk_settings_o );
	return self;
}

// ----------------------------------------------------------------------

static void swapchain_settings_destroy( le_backend_swapchain_vk_settings_o *self_ ) {
	delete ( self_ );
	self_ = nullptr;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_presentmode_hint( le_backend_swapchain_vk_settings_o *self_, const le_swapchain_vk_api::Presentmode &mode_ ) {
	self_->presentmode_hint = mode_;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_width_hint( le_backend_swapchain_vk_settings_o *self_, uint32_t width_ ) {
	self_->width_hint = width_;
}

// ----------------------------------------------------------------------
static void swapchain_settings_set_height_hint( le_backend_swapchain_vk_settings_o *self_, uint32_t height_ ) {
	self_->height_hint = height_;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_imagecount_hint( le_backend_swapchain_vk_settings_o *self_, uint32_t imagecount_hint_ ) {
	self_->imagecount_hint = imagecount_hint_;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_vk_device(le_backend_swapchain_vk_settings_o* self_, VkDevice vk_device_){
	self_->vk_device_ref = vk_device_;
}

// ----------------------------------------------------------------------

static void swapchain_settings_set_vk_surface_khr(le_backend_swapchain_vk_settings_o* self_, VkSurfaceKHR vk_surface_khr_){
	self_->vk_surface_ref = vk_surface_khr_;
}

static void query_surface_capabilities(){
//	if ( mSurfaceProperties.queried == false ){

//		// we need to find out if the current physical device supports PRESENT
//		mRendererProperties.physicalDevice.getSurfaceSupportKHR( mRendererProperties.graphicsFamilyIndex, mSettings.windowSurface, &mSurfaceProperties.presentSupported );

//		// find out which color formats are supported
//		// Get list of supported surface formats
//		mSurfaceProperties.surfaceFormats = mRendererProperties.physicalDevice.getSurfaceFormatsKHR( mSettings.windowSurface );

//		mSurfaceProperties.capabilities = mRendererProperties.physicalDevice.getSurfaceCapabilitiesKHR( mSettings.windowSurface );
//		mSurfaceProperties.presentmodes = mRendererProperties.physicalDevice.getSurfacePresentModesKHR( mSettings.windowSurface );

//		// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
//		// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
//		if ( ( mSurfaceProperties.surfaceFormats.size() == 1 ) && ( mSurfaceProperties.surfaceFormats[0].format == ::vk::Format::eUndefined ) ){
//			mWindowColorFormat.format = ::vk::Format::eB8G8R8A8Unorm;
//		} else{
//			// Always select the first available color format
//			// If you need a specific format (e.g. SRGB) you'd need to
//			// iterate over the list of available surface format and
//			// check for its presence
//			mWindowColorFormat.format = mSurfaceProperties.surfaceFormats[0].format;
//		}
//		mWindowColorFormat.colorSpace = mSurfaceProperties.surfaceFormats[0].colorSpace;

//		// ofLog() << "Present supported: " << ( mSurfaceProperties.presentSupported ? "TRUE" : "FALSE" );
//		mSurfaceProperties.queried = true;
//	}
}


// ----------------------------------------------------------------------

static void swapchain_reset(le_backend_swapchain_o* self, const le_backend_swapchain_vk_settings_o* settings_){

	if (settings_){
		self->mSettings = *settings_;
	}


//	::vk::Result err = ::vk::Result::eSuccess;

//		// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
//		// just before this setup() method was called.
//		querySurfaceCapabilities();

//		::vk::SwapchainKHR oldSwapchain = mVkSwapchain;

//		// Get physical device surface properties and formats
//		const ::vk::SurfaceCapabilitiesKHR & surfCaps = mSurfaceProperties.capabilities;

//		// Get available present modes for physical device
//		const std::vector<::vk::PresentModeKHR>& presentModes = mSurfaceProperties.presentmodes;


//		// Either set or get the swapchain surface extents
//		::vk::Extent2D swapchainExtent = {};

//		if ( surfCaps.currentExtent.width == -1 ){
//			swapchainExtent.width = mSettings.width;
//			swapchainExtent.height = mSettings.height;
//		} else {
//			// set dimensions from surface extents if surface extents are available
//			swapchainExtent = surfCaps.currentExtent;
//			const_cast<uint32_t&>(mSettings.width)  = surfCaps.currentExtent.width;
//			const_cast<uint32_t&>(mSettings.height) = surfCaps.currentExtent.height;
//		}

//		// Prefer user-selected present mode,
//		// use guaranteed fallback mode (FIFO) if preferred mode couldn't be found.
//		::vk::PresentModeKHR swapchainPresentMode = ::vk::PresentModeKHR::eFifo;

//		bool presentModeSwitchSuccessful = false;

//		for ( auto & p : presentModes ){
//			if ( p == mSettings.presentMode ){
//				swapchainPresentMode = p;
//				presentModeSwitchSuccessful = true;
//				break;
//			}
//		}

//		if (!presentModeSwitchSuccessful){
//			ofLogWarning() << "Could not switch to selected Swapchain Present Mode. Falling back to FIFO...";
//		}

//		// Write current present mode back to reference from parameter
//		// so caller can find out whether chosen present mode has been
//		// applied successfully.
//		const_cast<::vk::PresentModeKHR&>(mSettings.presentMode) = swapchainPresentMode;

//		uint32_t requestedNumberOfSwapchainImages = std::max<uint32_t>( surfCaps.minImageCount, mSettings.numSwapchainImages );
//		if ( ( surfCaps.maxImageCount > 0 ) && ( requestedNumberOfSwapchainImages > surfCaps.maxImageCount ) ){
//			requestedNumberOfSwapchainImages = surfCaps.maxImageCount;
//		}

//		if ( mSettings.numSwapchainImages != requestedNumberOfSwapchainImages ){
//			ofLogWarning() << "Swapchain: Number of swapchain images was adjusted to: " << requestedNumberOfSwapchainImages;
//		}

//		// Write current value back to parameter reference so caller
//		// has a chance to check if values were applied correctly.
//		const_cast<uint32_t&>(mSettings.numSwapchainImages) = requestedNumberOfSwapchainImages;

//		::vk::SurfaceTransformFlagBitsKHR preTransform;
//		// Note: this will be interesting for mobile devices
//		// - if rotation and mirroring for the final output can
//		// be defined here.

//		if ( surfCaps.supportedTransforms & ::vk::SurfaceTransformFlagBitsKHR::eIdentity){
//			preTransform = ::vk::SurfaceTransformFlagBitsKHR::eIdentity;
//		} else{
//			preTransform = surfCaps.currentTransform;
//		}

//		::vk::SwapchainCreateInfoKHR swapChainCreateInfo;

//		swapChainCreateInfo
//			.setSurface           ( mSettings.windowSurface )
//			.setMinImageCount     ( requestedNumberOfSwapchainImages)
//			.setImageFormat       ( mWindowColorFormat.format)
//			.setImageColorSpace   ( mWindowColorFormat.colorSpace)
//			.setImageExtent       ( swapchainExtent )
//			.setImageArrayLayers  ( 1 )
//			.setImageUsage        ( ::vk::ImageUsageFlagBits::eColorAttachment )
//			.setImageSharingMode  ( ::vk::SharingMode::eExclusive )
//			.setPreTransform      ( preTransform  )
//			.setCompositeAlpha    ( ::vk::CompositeAlphaFlagBitsKHR::eOpaque )
//			.setPresentMode       ( mSettings.presentMode )
//			.setClipped           ( VK_TRUE )
//			.setOldSwapchain      ( oldSwapchain )
//			;

//		mVkSwapchain = mDevice.createSwapchainKHR( swapChainCreateInfo );

//		// If an existing swap chain is re-created, destroy the old swap chain
//		// This also cleans up all the presentable images
//		if ( oldSwapchain ){
//			mDevice.destroySwapchainKHR( oldSwapchain );
//			oldSwapchain = nullptr;
//		}

//		std::vector<::vk::Image> swapchainImages = mDevice.getSwapchainImagesKHR( mVkSwapchain );
//		mImageCount = swapchainImages.size();


//		for ( auto&b : mImages ){
//			// If there were any images available at all to iterate over, this means
//			// that the swapchain was re-created.
//			// This happens on window resize, for example.
//			// Therefore we have to destroy old ImageView object(s).
//			mDevice.destroyImageView( b.view );
//		}

//		mImages.resize( mImageCount );

//		for ( uint32_t i = 0; i < mImageCount; i++ ){

//			mImages[i].imageRef = swapchainImages[i];

//			::vk::ImageSubresourceRange subresourceRange;
//			subresourceRange
//				.setAspectMask       ( ::vk::ImageAspectFlagBits::eColor )
//				.setBaseMipLevel     ( 0 )
//				.setLevelCount       ( 1 )
//				.setBaseArrayLayer   ( 0 )
//				.setLayerCount       ( 1 )
//				;

//			::vk::ImageViewCreateInfo imageViewCreateInfo;
//			imageViewCreateInfo
//				.setImage            ( mImages[i].imageRef )
//				.setViewType         ( ::vk::ImageViewType::e2D )
//				.setFormat           ( mWindowColorFormat.format )
//				.setComponents       ( ::vk::ComponentMapping() )
//				.setSubresourceRange ( subresourceRange )
//				;
//			// create image view for color image
//			mImages[i].view = mDevice.createImageView( imageViewCreateInfo );

//		}

	// TODO: implement.
};

// ----------------------------------------------------------------------

static le_backend_swapchain_o *swapchain_create( const le_backend_swapchain_vk_settings_o *settings_ ) {
	auto self = new ( le_backend_swapchain_o );

	swapchain_reset(self, settings_);

	return self;
}

// ----------------------------------------------------------------------

static void swapchain_destroy( le_backend_swapchain_o *self_ ) {
	delete ( self_ );
	self_ = nullptr;
}

// ----------------------------------------------------------------------

void register_le_swapchain_vk_api( void *api_ ) {
	auto  api           = static_cast<le_swapchain_vk_api *>( api_ );
	auto &swapchain_i   = api->swapchain_i;
	auto &settings_vk_i = api->settings_vk_i;

	swapchain_i.create  = swapchain_create;
	swapchain_i.reset = swapchain_reset;
	swapchain_i.destroy = swapchain_destroy;

	settings_vk_i.create               = swapchain_settings_create;

	settings_vk_i.destroy              = swapchain_settings_destroy;
	settings_vk_i.set_width_hint       = swapchain_settings_set_width_hint;
	settings_vk_i.set_height_hint      = swapchain_settings_set_height_hint;
	settings_vk_i.set_presentmode_hint = swapchain_settings_set_presentmode_hint;
	settings_vk_i.set_image_count_hint = swapchain_settings_set_imagecount_hint;
	settings_vk_i.set_vk_device        = swapchain_settings_set_vk_device;
	settings_vk_i.set_vk_surface_khr   = swapchain_settings_set_vk_surface_khr;

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
