#include "le_swapchain_vk.h"
#include <cassert>

#include "private/le_swapchain_vk/le_swapchain_vk_common.inl"
#include "util/volk/volk.h"
#include "private/le_swapchain_vk/vk_to_string_helpers.inl"

#include "le_backend_vk.h"
#include "le_backend_types_internal.h"
#include "le_window.h"
#include "le_log.h"
#include <iostream>

static constexpr auto LOGGER_LABEL = "le_swapchain_khr";

struct SurfaceProperties {
	VkSurfaceFormatKHR              windowSurfaceFormat;
	VkSurfaceCapabilitiesKHR        surfaceCapabilities;
	VkBool32                        presentSupported = VK_FALSE;
	std::vector<VkPresentModeKHR>   presentmodes;
	std::vector<VkSurfaceFormatKHR> availableSurfaceFormats;
};

struct khr_data_o {
	le_swapchain_settings_t mSettings                      = {};
	le_window_o*            window                         = nullptr;
	le_backend_o*           backend                        = nullptr;
	uint32_t                mImagecount                    = 0;
	uint32_t                mImageIndex                    = uint32_t( ~0 ); // current image index
	VkSwapchainKHR          swapchainKHR                   = nullptr;
	VkExtent2D              mSwapchainExtent               = {};
	VkPresentModeKHR        mPresentMode                   = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t                vk_graphics_queue_family_index = 0;
	SurfaceProperties       mSurfaceProperties             = {};
	std::vector<VkImage>    mImageRefs                     = {}; // owned by SwapchainKHR, don't delete
	VkDevice                device                         = nullptr;
	VkPhysicalDevice        physicalDevice                 = nullptr;
	VkResult                lastError                      = VK_SUCCESS; // keep track of last error
};

// ----------------------------------------------------------------------

static void swapchain_query_surface_capabilities( le_swapchain_o* base ) {

	// we need to find out if the current physical device supports PRESENT

	auto self = static_cast<khr_data_o* const>( base->data );

	using namespace le_backend_vk;

	const auto& settings          = self->mSettings.khr_settings;
	auto&       surfaceProperties = self->mSurfaceProperties;

	vkGetPhysicalDeviceSurfaceSupportKHR( self->physicalDevice, self->vk_graphics_queue_family_index, settings.vk_surface, &surfaceProperties.presentSupported );
	// Get list of supported surface formats
	{
		uint32_t count = 0;

		auto result = vkGetPhysicalDeviceSurfaceFormatsKHR( self->physicalDevice, settings.vk_surface, &count, nullptr );
		assert( result == VK_SUCCESS );
		surfaceProperties.availableSurfaceFormats.resize( count );
		result = vkGetPhysicalDeviceSurfaceFormatsKHR( self->physicalDevice, settings.vk_surface, &count, surfaceProperties.availableSurfaceFormats.data() );
		assert( result == VK_SUCCESS );

		result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( self->physicalDevice, settings.vk_surface, &surfaceProperties.surfaceCapabilities );
		assert( result == VK_SUCCESS );

		result = vkGetPhysicalDeviceSurfacePresentModesKHR( self->physicalDevice, settings.vk_surface, &count, nullptr );
		assert( result == VK_SUCCESS );
		surfaceProperties.presentmodes.resize( count );
		result = vkGetPhysicalDeviceSurfacePresentModesKHR( self->physicalDevice, settings.vk_surface, &count, surfaceProperties.presentmodes.data() );
		assert( result == VK_SUCCESS );
	}

	size_t selectedSurfaceFormatIndex = 0;
	auto   preferredSurfaceFormat     = VkFormat( self->mSettings.format_hint );

	if ( ( surfaceProperties.availableSurfaceFormats.size() == 1 ) &&
	     ( surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].format == VK_FORMAT_UNDEFINED ) ) {

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

static void swapchain_attach_images( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );

	static auto logger = LeLog( LOGGER_LABEL );

	auto result = vkGetSwapchainImagesKHR( self->device, self->swapchainKHR, &self->mImagecount, nullptr );
	assert( result == VK_SUCCESS );

	if ( self->mImagecount ) {
		self->mImageRefs.resize( self->mImagecount );
		result = vkGetSwapchainImagesKHR( self->device, self->swapchainKHR, &self->mImagecount, self->mImageRefs.data() );

		logger.info( "images attached for KHR swapchain: %p", self->swapchainKHR );

		{
			int i = 0;
			for ( auto const& img : self->mImageRefs ) {
				logger.info( "[%d] %p", i, img );
				i++;
			}
		}

		assert( result == VK_SUCCESS );
	} else {
		assert( false && "must have a valid number of images" );
	}
}

// ----------------------------------------------------------------------

template <typename T>
static inline auto clamp( const T& val_, const T& min_, const T& max_ ) {
	return std::max( min_, ( std::min( val_, max_ ) ) );
}

// ----------------------------------------------------------------------

static void swapchain_khr_reset( le_swapchain_o* base, const le_swapchain_settings_t* settings_ ) {

	static auto logger = LeLog( LOGGER_LABEL );
	auto        self   = static_cast<khr_data_o*>( base->data );

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

	//	VkResult err = VkResult::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	swapchain_query_surface_capabilities( base );

	VkSwapchainKHR oldSwapchain = nullptr;
	std::swap( self->swapchainKHR, oldSwapchain );

	const VkSurfaceCapabilitiesKHR&      surfaceCapabilities = self->mSurfaceProperties.surfaceCapabilities;
	const std::vector<VkPresentModeKHR>& presentModes        = self->mSurfaceProperties.presentmodes;

	// Either set or get the swapchain surface extents

	if ( surfaceCapabilities.currentExtent.width == 0 ) {
		self->mSwapchainExtent.width  = self->mSettings.width_hint;
		self->mSwapchainExtent.height = self->mSettings.height_hint;
	} else {
		// set dimensions from surface extents if surface extents are available
		self->mSwapchainExtent = surfaceCapabilities.currentExtent;
	}

	auto presentModeHint = VkPresentModeKHR( self->mSettings.khr_settings.presentmode_hint );

	for ( auto& p : presentModes ) {
		if ( p == presentModeHint ) {
			self->mPresentMode = p;
			break;
		}
	}

	if ( self->mPresentMode != presentModeHint ) {
		logger.warn( "Coult Could not switch to selected Swapchain Present Mode (%s), falling back to: %s",
		             to_str( presentModeHint ),
		             to_str( self->mPresentMode ) );
	}

	self->mImagecount = clamp( self->mSettings.imagecount_hint,
	                           surfaceCapabilities.minImageCount,
	                           surfaceCapabilities.maxImageCount );

	if ( self->mImagecount != self->mSettings.imagecount_hint ) {
		logger.warn( "Swapchain: Number of swapchain images was adjusted to: %d ", self->mImagecount );
	}

	VkSurfaceTransformFlagBitsKHR preTransform{};

	if ( surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ) {
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	} else {
		preTransform = surfaceCapabilities.currentTransform;
	}

	VkSwapchainCreateInfoKHR swapChainCreateInfo{
	    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	    .pNext                 = nullptr, // optional
	    .flags                 = 0,       // optional
	    .surface               = self->mSettings.khr_settings.vk_surface,
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
	    .oldSwapchain          = oldSwapchain,
	};

	if ( vkCreateSwapchainKHR == nullptr ) {
		logger.error( "Could not find function pointer to create swapchain. \n"
		              "Did you first (via renderer) query required instance and device extensions?\n"
		              "This is done implicitly when creating swapchains by passing renderer settings which contain swapchain settings to le_renderer.setup().\n"
		              "If you, however, decide to explicitly create a swapchain, you must query instance and device extensions **before** you setup the renderer." );
		assert( false );
	}

	self->lastError = vkCreateSwapchainKHR( self->device, &swapChainCreateInfo, nullptr, &self->swapchainKHR );
	assert( self->lastError == VK_SUCCESS );

	swapchain_attach_images( base );
}

// ----------------------------------------------------------------------

static le_swapchain_o* swapchain_khr_create( const le_swapchain_vk_api::swapchain_interface_t& interface, le_backend_o* backend, const le_swapchain_settings_t* settings ) {

	auto base  = new le_swapchain_o( interface );
	base->data = new khr_data_o{};
	auto self  = static_cast<khr_data_o*>( base->data );

	self->backend = backend;

	{
		using namespace le_backend_vk;
		self->device                         = private_backend_vk_i.get_vk_device( backend );
		self->physicalDevice                 = private_backend_vk_i.get_vk_physical_device( backend );
		self->vk_graphics_queue_family_index = private_backend_vk_i.get_default_graphics_queue_info( backend )->queue_family_index;
	}

	self->swapchainKHR = nullptr;

	swapchain_khr_reset( base, settings );

	static auto logger = LeLog( LOGGER_LABEL );
	logger.info( " ** swapchain created: %x, VkSwapchain: %x", base, self->swapchainKHR );

	return base;
}

static void swapchain_khr_destroy( le_swapchain_o* base );

static le_swapchain_o* swapchain_create_from_old_swapchain( le_swapchain_o* old_swapchain ) {

	auto new_swapchain  = new le_swapchain_o( old_swapchain->vtable );
	new_swapchain->data = new khr_data_o{};

	auto new_data = static_cast<khr_data_o*>( new_swapchain->data );
	auto old_data = static_cast<khr_data_o* const>( old_swapchain->data );

	auto old_swapchain_khr = old_data->swapchainKHR;

	*new_data = *old_data;
	// Note that we do not set new_data->swapchainKHR to NULL
	// this is so that is will get used as oldSwapchain when
	// creating a new KHR Swapchain in reset.

	swapchain_khr_reset( new_swapchain, &new_data->mSettings );



	static auto logger = LeLog( LOGGER_LABEL );
	logger.info( " ** swapchain created: %x from %x", new_swapchain, old_swapchain );

	return new_swapchain;
}
// ----------------------------------------------------------------------

static void swapchain_khr_destroy( le_swapchain_o* base ) {

	static auto logger = LeLog( LOGGER_LABEL );
	auto        self   = static_cast<khr_data_o* const>( base->data );

	VkDevice device = self->device;

	logger.info( " ** VkSwapchain destroy: %x, VkSwapchain: %x", base, self->swapchainKHR );

	if ( self->swapchainKHR ) {
		vkDestroySwapchainKHR( device, self->swapchainKHR, nullptr );
		self->swapchainKHR = nullptr;
	}

	delete self; // delete object's data
	delete base; // delete object
}

// ----------------------------------------------------------------------

static bool swapchain_khr_acquire_next_image( le_swapchain_o* base, VkSemaphore present_complete_semaphore, uint32_t* image_index ) {

	auto self = static_cast<khr_data_o* const>( base->data );
	// This method will return the next avaliable vk image index for this swapchain, possibly
	// before this image is available for writing. Image will be ready for writing when
	// semaphorePresentComplete is signalled.
	static auto logger = LeLog( LOGGER_LABEL );

	if ( self->lastError != VK_SUCCESS &&
	     self->lastError != VK_SUBOPTIMAL_KHR ) {
		logger.warn( "KHR Swapchain %x cannot acquire image because of previous error: %s", base, to_str( self->lastError ) );
		return false;
	}

	self->lastError = vkAcquireNextImageKHR( self->device, self->swapchainKHR, UINT64_MAX, present_complete_semaphore, nullptr, image_index );

	switch ( self->lastError ) {
	case VK_SUCCESS: {
		self->mImageIndex = *image_index;

		// logger.info( "Acquired image %d via swapchain %p, img: %p", *image_index, base, self->mImageRefs[ *image_index ] );
		return true;
	}
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

static VkImage swapchain_khr_get_image( le_swapchain_o* base, uint32_t index ) {

	auto self = static_cast<khr_data_o* const>( base->data );

#ifndef NDEBUG
	assert( index < self->mImageRefs.size() );
#endif
	return self->mImageRefs[ index ];
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR* swapchain_khr_get_surface_format( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );
	return &reinterpret_cast<VkSurfaceFormatKHR&>( self->mSurfaceProperties.windowSurfaceFormat );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_khr_get_image_width( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );
	return self->mSwapchainExtent.width;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_khr_get_image_height( le_swapchain_o* base ) {

	auto self = static_cast<khr_data_o* const>( base->data );
	return self->mSwapchainExtent.height;
}

// ----------------------------------------------------------------------

static size_t swapchain_khr_get_swapchain_images_count( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );
	return self->mImagecount;
}

// ----------------------------------------------------------------------

static bool swapchain_khr_present( le_swapchain_o* base, VkQueue queue_, VkSemaphore renderCompleteSemaphore, uint32_t* pImageIndex ) {

	static auto logger = LeLog( LOGGER_LABEL );
	auto        self   = static_cast<khr_data_o* const>( base->data );

	// if ( self->lastError != VK_SUCCESS &&
	//      self->lastError != VK_SUBOPTIMAL_KHR ) {
	//	static auto logger = LeLog( LOGGER_LABEL );
	//	logger.warn( "KHR Swapchain %x cannot present image because of previous error, %s", base, to_str( self->lastError ) );
	//	return false;
	// }

	VkPresentInfoKHR presentInfo{
	    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	    .pNext              = nullptr, // optional
	    .waitSemaphoreCount = 1,       // optional
	    .pWaitSemaphores    = &renderCompleteSemaphore,
	    .swapchainCount     = 1,
	    .pSwapchains        = &self->swapchainKHR,
	    .pImageIndices      = pImageIndex,
	    .pResults           = nullptr, // optional
	};

	self->lastError = vkQueuePresentKHR( queue_, &presentInfo );

	if ( self->lastError == VK_ERROR_OUT_OF_DATE_KHR ) {
		// FIXME: handle swapchain resize event properly
		logger.warn( "Out of date detected - this commonly indicates surface resize" );
		return false;
	}

	return true;
};

static bool swapchain_get_required_vk_instance_extensions( const le_swapchain_settings_t* ) {
	using namespace le_backend_vk;
	return api->le_backend_settings_i.add_required_instance_extension( VK_KHR_SURFACE_EXTENSION_NAME );
}

// ----------------------------------------------------------------------

static bool swapchain_get_required_vk_device_extensions( const le_swapchain_settings_t* ) {
	using namespace le_backend_vk;
	return api->le_backend_settings_i.add_required_device_extension( "VK_KHR_swapchain" );
}

// ----------------------------------------------------------------------

void register_le_swapchain_khr_api( void* api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api*>( api_ );
	auto& swapchain_i = api->swapchain_khr_i;

	swapchain_i.create                              = swapchain_khr_create;
	swapchain_i.create_from_old_swapchain           = swapchain_create_from_old_swapchain;
	swapchain_i.destroy                             = swapchain_khr_destroy;

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
