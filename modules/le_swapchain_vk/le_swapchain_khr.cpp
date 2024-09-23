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
#include "le_swapchain_khr.h"

static constexpr auto LOGGER_LABEL = "le_swapchain_khr";

struct SurfaceProperties {
	VkSurfaceFormat2KHR              windowSurfaceFormat;
	VkSurfaceCapabilities2KHR        surfaceCapabilities;
	VkBool32                         presentSupported = VK_FALSE;
	std::vector<VkPresentModeKHR>    presentmodes;
	std::vector<VkSurfaceFormat2KHR> availableSurfaceFormats;
};

struct khr_data_o {
	le_swapchain_windowed_settings_t mSettings                      = {};
	std::vector<VkFence>             vk_present_fences              = {}; // one fence for each presentable image - we use these to protect ourselves by delaying destroying the present semaphores until any in-flight present has completed.
	VkSurfaceKHR                     vk_surface                     = nullptr;
	le_backend_o*                    backend                        = nullptr;
	uint32_t                         mImagecount                    = 0;
	uint32_t                         mImageIndex                    = uint32_t( ~0 ); // current image index
	VkSwapchainKHR                   swapchainKHR                   = nullptr;
	VkExtent2D                       mSwapchainExtent               = {};
	VkPresentModeKHR                 mPresentMode                   = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t                         vk_graphics_queue_family_index = 0;
	SurfaceProperties                mSurfaceProperties             = {};
	std::vector<VkImage>             mImageRefs                     = {}; // owned by SwapchainKHR, don't delete
	VkInstance                       instance                       = nullptr;
	VkDevice                         device                         = nullptr;
	VkPhysicalDevice                 physicalDevice                 = nullptr;
	VkResult                         lastError                      = VK_SUCCESS; // keep track of last error
	bool                             is_retired                     = false;      // whether this swapchain has been retired
};

// ----------------------------------------------------------------------

static void swapchain_query_surface_capabilities( le_swapchain_o* base ) {

	static auto logger = LeLog( LOGGER_LABEL );
	// we need to find out if the current physical device supports PRESENT

	auto self = static_cast<khr_data_o* const>( base->data );

	using namespace le_backend_vk;

	const auto& settings          = self->mSettings;
	auto&       surfaceProperties = self->mSurfaceProperties;

	vkGetPhysicalDeviceSurfaceSupportKHR( self->physicalDevice, self->vk_graphics_queue_family_index, self->vk_surface, &surfaceProperties.presentSupported );
	// Get list of supported surface formats
	{
		uint32_t count = 0;

		VkPhysicalDeviceSurfaceInfo2KHR surface_info = {
		    .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR, // VkStructureType
		    .pNext   = nullptr,                                              // void *, optional
		    .surface = self->vk_surface,                                     // VkSurfaceKHR, optional
		};

		auto result = vkGetPhysicalDeviceSurfaceFormats2KHR( self->physicalDevice, &surface_info, &count, nullptr );
		assert( result == VK_SUCCESS );
		surfaceProperties.availableSurfaceFormats.resize( count );
		result = vkGetPhysicalDeviceSurfaceFormats2KHR( self->physicalDevice, &surface_info, &count, surfaceProperties.availableSurfaceFormats.data() );
		assert( result == VK_SUCCESS );

		result = vkGetPhysicalDeviceSurfaceCapabilities2KHR( self->physicalDevice, &surface_info, &surfaceProperties.surfaceCapabilities );
		assert( result == VK_SUCCESS );

		result = vkGetPhysicalDeviceSurfacePresentModesKHR( self->physicalDevice, self->vk_surface, &count, nullptr );
		assert( result == VK_SUCCESS );
		surfaceProperties.presentmodes.resize( count );
		result = vkGetPhysicalDeviceSurfacePresentModesKHR( self->physicalDevice, self->vk_surface, &count, surfaceProperties.presentmodes.data() );
		assert( result == VK_SUCCESS );
	}

	size_t selectedSurfaceFormatIndex = 0;
	auto   preferredSurfaceFormat     = VkFormat( self->mSettings.format_hint );

	if ( ( surfaceProperties.availableSurfaceFormats.size() == 1 ) &&
	     ( surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].surfaceFormat.format == VK_FORMAT_UNDEFINED ) ) {

		// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
		// there is no preferred format, and we must assume VkFormat::eB8G8R8A8Unorm.
		surfaceProperties.windowSurfaceFormat.surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;

	} else {

		// Iterate over the list of available surface formats and check for the presence of
		// our preferredSurfaceFormat
		//
		// Select the first available color format if the preferredSurfaceFormat cannot be found.

		for ( size_t i = 0; i != surfaceProperties.availableSurfaceFormats.size(); ++i ) {
			if ( surfaceProperties.availableSurfaceFormats[ i ].surfaceFormat.format == preferredSurfaceFormat ) {
				selectedSurfaceFormatIndex = i;
				break;
			}
		}
		surfaceProperties.windowSurfaceFormat.surfaceFormat.format =
		    surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].surfaceFormat.format;
	}

	if ( preferredSurfaceFormat != surfaceProperties.windowSurfaceFormat.surfaceFormat.format ) {
		logger.warn( "Swapchain surface format was adapted to: %s", to_str( le::Format( surfaceProperties.windowSurfaceFormat.surfaceFormat.format ) ) );
	}

	logger.info( "** Surface queried Extents: %d x %d",
	             surfaceProperties.surfaceCapabilities.surfaceCapabilities.currentExtent.width,
	             surfaceProperties.surfaceCapabilities.surfaceCapabilities.currentExtent.height );

	// always select the corresponding color space
	surfaceProperties.windowSurfaceFormat.surfaceFormat.colorSpace =
	    surfaceProperties.availableSurfaceFormats[ selectedSurfaceFormatIndex ].surfaceFormat.colorSpace;
}

// ----------------------------------------------------------------------

static void swapchain_attach_images( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );

	static auto logger = LeLog( LOGGER_LABEL );

	auto result = vkGetSwapchainImagesKHR( self->device, self->swapchainKHR, &self->mImagecount, nullptr );
	assert( result == VK_SUCCESS );

	if ( self->mImagecount ) {
		self->mImageRefs.resize( self->mImagecount );
		VkFenceCreateInfo fence_create_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, // VkStructureType
			.pNext = nullptr,                             // void *, optional
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,        // VkFenceCreateFlags, optional
		};
		self->vk_present_fences.resize( self->mImagecount, {} );
		for ( int i = 0; i != self->mImagecount; i++ ) {
			vkCreateFence( self->device, &fence_create_info, nullptr, &self->vk_present_fences[ i ] );
		}
		result = vkGetSwapchainImagesKHR( self->device, self->swapchainKHR, &self->mImagecount, self->mImageRefs.data() );
		assert( result == VK_SUCCESS );

		// logger.info( "Images attached for KHR swapchain [%p]:", self->swapchainKHR );
		//{
		//	int i = 0;
		//	for ( auto const& img : self->mImageRefs ) {
		//		logger.info( "\t[%d] %p", i, img );
		//		i++;
		//	}
		// }

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

static bool swapchain_khr_reset( le_swapchain_o* base, const le_swapchain_windowed_settings_t* settings_ ) {

	static auto logger = LeLog( LOGGER_LABEL );
	auto        self   = static_cast<khr_data_o*>( base->data );

	if ( settings_ ) {
		self->mSettings = *settings_;
	}

	{
		using namespace le_window;

		self->mSettings.width_hint  = window_i.get_surface_width( self->mSettings.window );
		self->mSettings.height_hint = window_i.get_surface_height( self->mSettings.window );
	}

	// `settings_` may have been a nullptr in which case this operation is only valid
	// if self->mSettings has been fully set before.

	// If there is not yet a surface associated with this swapchain, we must create one
	// by using the window API.
	if ( nullptr == self->vk_surface ) {

		if ( nullptr == self->mSettings.window ) {
			logger.error( "No window associated with LE_KHR_SWAPCHAIN %p.", base );
			return false;
		}

		// ---------| invariant: self->mSettings.window is not nullptr

		// we must additionally make sure that the window has not got any other swapchains
		// currently associated with it.

		// we take a shared pointer on the window, so that we can enforce that the window
		// is alive for as long as this swapchain holds a reference to its surface.
		le_window::window_i.increase_reference_count( self->mSettings.window );

		self->vk_surface = le_window::window_i.create_surface( self->mSettings.window, self->instance );

		if ( nullptr == self->vk_surface ) {
			// acquiring the surface didn't work, we don't want to keep holding part-ownership of the
			// window, as we only need that for the sake of the surface.
			le_window::window_i.decrease_reference_count( self->mSettings.window );
			return false;
		}
	}

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	swapchain_query_surface_capabilities( base );

	VkSwapchainKHR oldSwapchain = nullptr;
	std::swap( self->swapchainKHR, oldSwapchain );

	const VkSurfaceCapabilitiesKHR&      surfaceCapabilities = self->mSurfaceProperties.surfaceCapabilities.surfaceCapabilities;
	const std::vector<VkPresentModeKHR>& presentModes        = self->mSurfaceProperties.presentmodes;

	// Either set or get the swapchain surface extents

	if ( surfaceCapabilities.currentExtent.width == 0 || surfaceCapabilities.currentExtent.width == ( uint32_t( -1 ) ) ) {
		self->mSwapchainExtent.width  = le_window_api_i->window_i.get_surface_width( self->mSettings.window );
		self->mSwapchainExtent.height = le_window_api_i->window_i.get_surface_height( self->mSettings.window );
	} else {
		// set dimensions from surface extents if surface extents are available
		self->mSwapchainExtent = surfaceCapabilities.currentExtent;
	}

	auto presentModeHint = VkPresentModeKHR( self->mSettings.presentmode_hint );

	for ( auto& p : presentModes ) {
		if ( p == presentModeHint ) {
			self->mPresentMode = p;
			break;
		}
	}

	if ( self->mPresentMode != presentModeHint ) {
		logger.warn( "Could not switch to selected Swapchain Present Mode (%s), falling back to: %s",
					 to_str( presentModeHint ),
					 to_str( self->mPresentMode ) );
	}

	// We require a minimum of minImageCount+1, so that in case minImageCount
	// is 3 we can still acquire 2 images without blocking.
	//
	self->mImagecount = clamp( self->mSettings.base.imagecount_hint,
							   surfaceCapabilities.minImageCount + 1,
							   surfaceCapabilities.maxImageCount );

	if ( self->mImagecount != self->mSettings.base.imagecount_hint ) {
		logger.warn( "Number of swapchain images was adjusted to: %d. minImageCount: %d, maxImageCount: %d",
					 self->mImagecount,
					 surfaceCapabilities.minImageCount,
					 surfaceCapabilities.maxImageCount );
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
	    .surface               = self->vk_surface,
	    .minImageCount         = self->mImagecount,
	    .imageFormat           = self->mSurfaceProperties.windowSurfaceFormat.surfaceFormat.format,
	    .imageColorSpace       = self->mSurfaceProperties.windowSurfaceFormat.surfaceFormat.colorSpace,
	    .imageExtent           = self->mSwapchainExtent,
	    .imageArrayLayers      = 1,
	    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0, // optional
	    .pQueueFamilyIndices   = nullptr,
	    .preTransform          = preTransform,
	    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	    .presentMode           = self->mPresentMode,
	    .clipped               = VK_TRUE,
	    .oldSwapchain          = oldSwapchain,
	};

	if ( vkCreateSwapchainKHR == nullptr ) {
		logger.error( "Could not find function pointer to create swapchain. \n"
					  "\t )\n"
					  "\t ) Most likely you forgot to request the required Vulkan extensions before setting up the renderer. \n"
					  "\t )\n"
					  "\t ) Fix this by calling renderer.request_backend_capabilities() with any settings for swapchains you will want to use.\n"
					  "\t ) This is done implicitly when creating swapchains by passing renderer settings (which contain swapchain settings) to le_renderer.setup().\n"
					  "\t ) If you, however, decide to explicitly create a swapchain, you must query instance and device extensions **before** you setup the renderer." );
		assert( false );
	}

	self->lastError = vkCreateSwapchainKHR( self->device, &swapChainCreateInfo, nullptr, &self->swapchainKHR );
	assert( self->lastError == VK_SUCCESS );

	swapchain_attach_images( base );
	return true;
}

// ----------------------------------------------------------------------
static void swapchain_khr_destroy( le_swapchain_o* base );

static le_swapchain_o* swapchain_khr_create( le_backend_o* backend, const le_swapchain_settings_t* settings ) {
	static auto logger = LeLog( LOGGER_LABEL );

	auto base  = new le_swapchain_o( le_swapchain_vk::api->swapchain_khr_i );
	base->data = new khr_data_o{};
	auto self  = static_cast<khr_data_o*>( base->data );

	self->backend = backend;

	{
		using namespace le_backend_vk;
		self->device                         = private_backend_vk_i.get_vk_device( backend );
		self->instance                       = vk_instance_i.get_vk_instance( private_backend_vk_i.get_instance( backend ) );
		self->physicalDevice                 = private_backend_vk_i.get_vk_physical_device( backend );
		self->vk_graphics_queue_family_index = private_backend_vk_i.get_default_graphics_queue_info( backend )->queue_family_index;
	}

	self->swapchainKHR = nullptr;

	assert( settings->type == le_swapchain_settings_t::Type::LE_KHR_SWAPCHAIN );

	if ( swapchain_khr_reset( base, reinterpret_cast<le_swapchain_windowed_settings_t const*>( settings ) ) ) {
		logger.info( "Created Swapchain: %p, VkSwapchain: %p", base, self->swapchainKHR );
		return base;
	}

	// -----------| invariant: Something went wrong - we must clean up

	logger.warn( "Could not create swapchain" );
	swapchain_khr_destroy( base );
	return nullptr;
}

// ----------------------------------------------------------------------

// NOTE: This needs to be an atomic operation
// retire the old swapchain, and create a new swapchain
// using the old swapchain's settings and surface.
static le_swapchain_o* swapchain_create_from_old_swapchain( le_swapchain_o* old_swapchain, uint32_t width, uint32_t height ) {

	auto new_swapchain  = new le_swapchain_o( old_swapchain->vtable );
	new_swapchain->data = new khr_data_o{};

	auto new_data = static_cast<khr_data_o*>( new_swapchain->data );
	auto old_data = static_cast<khr_data_o* const>( old_swapchain->data );

	*new_data = *old_data;

	// Note that we do not set new_data->swapchainKHR to NULL
	// - this is so that it will get used as oldSwapchain when
	// creating a new KHR Swapchain in swapchain_khr_reset.

	old_data->is_retired = true;

	new_data->mSettings.width_hint  = width;
	new_data->mSettings.height_hint = height;

	swapchain_khr_reset( new_swapchain, &new_data->mSettings );

	static auto logger = LeLog( LOGGER_LABEL );
	logger.info( "Created Swapchain %p from old Swapchain %p", new_swapchain, old_swapchain );

	return new_swapchain;
}
// ----------------------------------------------------------------------
static void swapchain_khr_release( le_swapchain_o* base ) {

	// You can call release explicitly to sever the connection between
	// window and surface when removing a swapchain from the renderer.
	//
	// This is so that we can immediately re-use the window to associate
	// a new swapchain with it.
	//
	// `release` is implicitly called by `destroy`

	static auto logger = LeLog( LOGGER_LABEL );
	auto        self   = static_cast<khr_data_o* const>( base->data );

	VkDevice device = self->device;

	// we must wait for the images that are in-flight for present to be complete.
	vkWaitForFences( self->device, self->vk_present_fences.size(), self->vk_present_fences.data(), true, 1'000'000'000 );

	if ( self->swapchainKHR ) {

		vkDestroySwapchainKHR( device, self->swapchainKHR, nullptr );
		logger.info( "Destroyed VkSwapchain: %p", base, self->swapchainKHR );
		self->swapchainKHR = nullptr;
	}

	if ( !self->is_retired && self->vk_surface ) {
		// In case a swapchain was retired, ownership of the surface has
		// moved to the currently non-retired instance of the swapchain.
		//
		// We only want to destroy a surface if it belonged to a non-retired swapchain.
		vkDestroySurfaceKHR( self->instance, self->vk_surface, nullptr );
		logger.info( "Destroyed VkSurface: %p", self->vk_surface );

		le_window_api_i->window_i.notify_destroy_surface( self->mSettings.window );

		// Since we increased the reference count on the window when we created a
		// surface, we can nor decrease the reference count again, as we release
		// our part-ownership on the window as soon as the surface is destroyed.
		le_window_api_i->window_i.decrease_reference_count( self->mSettings.window );

		self->vk_surface = nullptr;
	}
}

// ----------------------------------------------------------------------

static void swapchain_khr_destroy( le_swapchain_o* base ) {

	static auto logger = LeLog( LOGGER_LABEL );
	auto        self   = static_cast<khr_data_o* const>( base->data );

	swapchain_khr_release( base );

	for ( auto& f : self->vk_present_fences ) {
		vkDestroyFence( self->device, f, nullptr );
	}
	self->vk_present_fences.clear();

	logger.info( "Destroyed Swapchain %p", self );

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

	if ( self->is_retired ) {
		assert( false );
	}

	if ( self->lastError != VK_SUCCESS &&
		 self->lastError != VK_SUBOPTIMAL_KHR ) {
		logger.warn( "KHR Swapchain %p cannot acquire image because of previous error: %s", base, to_str( self->lastError ) );
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
		logger.warn( "Could not acquire next image: %s", to_str( self->lastError ) );
		return false;
	}
	default:
		logger.error( "Could not acquire next image: %s", to_str( self->lastError ) );
		return false;
	}
}

// ----------------------------------------------------------------------

static VkImage swapchain_khr_get_image( le_swapchain_o* base, uint32_t index ) {

	auto self = static_cast<khr_data_o* const>( base->data );

	if ( self->is_retired ) {
		assert( false );
	}
#ifndef NDEBUG
	assert( index < self->mImageRefs.size() );
#endif
	return self->mImageRefs[ index ];
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR* swapchain_khr_get_surface_format( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );
	return &reinterpret_cast<VkSurfaceFormatKHR&>( self->mSurfaceProperties.windowSurfaceFormat );
	if ( self->is_retired ) {
		assert( false );
	}
}

// ----------------------------------------------------------------------

static uint32_t swapchain_khr_get_image_width( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );
	if ( self->is_retired ) {
		assert( false );
	}
	return self->mSwapchainExtent.width;
}

// ----------------------------------------------------------------------

static uint32_t swapchain_khr_get_image_height( le_swapchain_o* base ) {

	auto self = static_cast<khr_data_o* const>( base->data );
	if ( self->is_retired ) {
		assert( false );
	}
	return self->mSwapchainExtent.height;
}

// ----------------------------------------------------------------------

static size_t swapchain_khr_get_swapchain_images_count( le_swapchain_o* base ) {
	auto self = static_cast<khr_data_o* const>( base->data );
	if ( self->is_retired ) {
		assert( false );
	}
	return self->mImagecount;
}

// ----------------------------------------------------------------------

static bool swapchain_khr_present( le_swapchain_o* base, VkQueue queue_, VkSemaphore renderCompleteSemaphore, uint32_t* pImageIndex ) {

	static auto logger = LeLog( LOGGER_LABEL );
	auto        self   = static_cast<khr_data_o* const>( base->data );

	if ( self->is_retired ) {
		logger.warn( "Present called on retired swapchain" );
		// return false;
		// assert( false );
	}

	uint32_t image_index = *pImageIndex;

	VkSwapchainPresentFenceInfoEXT present_fence_info = {
		.sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT, // VkStructureType
		.pNext          = nullptr,                                            // void *, optional
		.swapchainCount = 1,                                                  // uint32_t
		.pFences        = &self->vk_present_fences[ *pImageIndex ],           // VkFence const *
	};

	vkWaitForFences( self->device, 1, present_fence_info.pFences, true, 1'000'000'000 );
	vkResetFences( self->device, 1, present_fence_info.pFences );

	VkPresentInfoKHR presentInfo{
		.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext              = &present_fence_info, // optional
		.waitSemaphoreCount = 1,                   // optional
		.pWaitSemaphores    = &renderCompleteSemaphore,
		.swapchainCount     = 1,
		.pSwapchains        = &self->swapchainKHR,
		.pImageIndices      = pImageIndex,
		.pResults           = nullptr, // optional
	};

	self->lastError = vkQueuePresentKHR( queue_, &presentInfo );

	if ( self->lastError != VK_SUCCESS ) {

		/*
		 * Note that Semaphore waits still execute regardless of whether the command is successful or not:
		 *
		 * https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/vkQueuePresentKHR.html
		 *
		 * > However, if the presentation request is rejected by the presentation engine with an error
		 * > VK_ERROR_OUT_OF_DATE_KHR, VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT, or
		 * > VK_ERROR_SURFACE_LOST_KHR, the set of queue operations are still considered to be enqueued
		 * > and thus any semaphore wait operation specified in VkPresentInfoKHR will execute when the
		 * > corresponding queue operation is complete.
		 *
		 */

		logger.warn( "Present returned error: %s", to_str( self->lastError ) );
		return false;
	}

	return true;
};

// ----------------------------------------------------------------------

static bool swapchain_request_backend_capabilities( const le_swapchain_settings_t* ) {
	using namespace le_backend_vk;

	static VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT swapchain_maintenance_features = {
		.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT, // VkStructureType
		.pNext                 = nullptr,                                                                // void *, optional
		.swapchainMaintenance1 = 1,                                                                      // VkBool32
	};

	auto p_maintenance_features =
		( VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT* )
			api->le_backend_settings_i.get_or_append_features_chain_link(
				( GenericVkStruct* )( &swapchain_maintenance_features ) );

	p_maintenance_features->swapchainMaintenance1 = true;

	return api->le_backend_settings_i.add_required_device_extension( "VK_KHR_swapchain" ) &&
		   api->le_backend_settings_i.add_required_device_extension( "VK_EXT_swapchain_maintenance1" ) &&
		   api->le_backend_settings_i.add_required_instance_extension( "VK_EXT_surface_maintenance1" ) &&
		   api->le_backend_settings_i.add_required_instance_extension( VK_KHR_SURFACE_EXTENSION_NAME );
	;
}

// ----------------------------------------------------------------------

static le_swapchain_settings_t* swapchain_settings_create( le_swapchain_settings_t::Type type ) {
	assert( type == le_swapchain_settings_t::LE_KHR_SWAPCHAIN );
	auto ret = new le_swapchain_windowed_settings_t{};
	return reinterpret_cast<le_swapchain_settings_t*>( ret );
}

// ----------------------------------------------------------------------

static void swapchain_settings_destroy( le_swapchain_settings_t* settings ) {
	assert( settings->type == le_swapchain_settings_t::LE_KHR_SWAPCHAIN );
	auto obj = reinterpret_cast<le_swapchain_windowed_settings_t*>( settings );
	delete ( obj );
}

// ----------------------------------------------------------------------

static le_swapchain_settings_t* swapchain_settings_clone( le_swapchain_settings_t const* settings ) {
	assert( settings->type == le_swapchain_settings_t::LE_KHR_SWAPCHAIN );
	auto obj = reinterpret_cast<le_swapchain_windowed_settings_t const*>( settings );
	auto ret = new le_swapchain_windowed_settings_t{ *obj };

	return reinterpret_cast<le_swapchain_settings_t*>( ret );
}

// ----------------------------------------------------------------------

void register_le_swapchain_khr_api( void* api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api*>( api_ );
	auto& swapchain_i = api->swapchain_khr_i;

	swapchain_i.create                    = swapchain_khr_create;
	swapchain_i.create_from_old_swapchain = swapchain_create_from_old_swapchain;
	swapchain_i.destroy                   = swapchain_khr_destroy;
	swapchain_i.release                   = swapchain_khr_release;

	swapchain_i.acquire_next_image           = swapchain_khr_acquire_next_image;
	swapchain_i.get_image                    = swapchain_khr_get_image;
	swapchain_i.get_image_width              = swapchain_khr_get_image_width;
	swapchain_i.get_image_height             = swapchain_khr_get_image_height;
	swapchain_i.get_surface_format           = swapchain_khr_get_surface_format;
	swapchain_i.get_image_count              = swapchain_khr_get_swapchain_images_count;
	swapchain_i.present                      = swapchain_khr_present;
	swapchain_i.request_backend_capabilities = swapchain_request_backend_capabilities;

	swapchain_i.settings_create  = swapchain_settings_create;
	swapchain_i.settings_clone   = swapchain_settings_clone;
	swapchain_i.settings_destroy = swapchain_settings_destroy;
}
