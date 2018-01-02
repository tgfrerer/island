#include "le_backend_vk/private/le_backend_private.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <set>
#include <map>

// ----------------------------------------------------------------------

uint32_t findClosestMatchingQueueIndex( const std::vector<vk::QueueFlags>& queueFlags_, const vk::QueueFlags & flags ){

	// Find out the queue family index for a queue best matching the given flags.
	// We use this to find out the index of the Graphics Queue for example.

	for ( uint32_t i = 0; i != queueFlags_.size(); i++ ){
		if ( queueFlags_[i] == flags ){
			// First perfect match
			return i;
		}
	}

	for ( uint32_t i = 0; i != queueFlags_.size(); i++ ){
		if ( queueFlags_[i] & flags ){
			// First multi-function queue match
			return i;
		}
	}

	// ---------| invariant: no queue found

	if (flags & vk::QueueFlagBits::eGraphics){
		std::cerr << "Could not find queue family index matching: " << vk::to_string( flags );
	}

	return ~( uint32_t( 0 ) );
}

// ----------------------------------------------------------------------
/// \brief Find best match for a vector or queues defined by queueFamiliyProperties flags
/// \note  For each entry in the result vector the tuple values represent:
///        0.. best matching queue family
///        1.. index within queue family
///        2.. index of queue from props vector (used to keep queue indices
//             consistent between requested queues and queues you will render to)
std::vector<std::tuple<uint32_t, uint32_t, size_t>> findBestMatchForRequestedQueues( const std::vector<vk::QueueFamilyProperties>& props, const std::vector<::vk::QueueFlags>& reqProps ){
	std::vector<std::tuple<uint32_t, uint32_t, size_t>> result;

	std::vector<uint32_t> usedQueues( props.size(), ~( uint32_t(0) ) ); // last used queue, per queue family (initialised at -1)

	size_t reqIdx = 0; // original index for requested queue
	for ( const auto & flags : reqProps ){

		// best match is a queue which does exclusively what we want
		bool foundMatch = false;
		uint32_t foundFamily = 0;
		uint32_t foundIndex = 0;

		for ( uint32_t familyIndex = 0; familyIndex != props.size(); familyIndex++ ){

			// 1. Is there a family that matches our requirements exactly?
			// 2. Is a queue from this family still available?

			if ( props[familyIndex].queueFlags == flags ){
				// perfect match
				if ( usedQueues[familyIndex] + 1 < props[familyIndex].queueCount ){
					foundMatch = true;
					foundFamily = familyIndex;
					foundIndex = usedQueues[familyIndex] + 1;
					std::cout << "Found dedicated queue matching: " << ::vk::to_string( flags ) << std::endl;
				} else{
					std::cout << "No more dedicated queues available matching: " << ::vk::to_string( flags ) << std::endl;
				}
				break;
			}
		}

		if ( foundMatch == false ){

			// If we haven't found a match, we need to find a versatile queue which might
			// be able to fulfill our requirements.

			for ( uint32_t familyIndex = 0; familyIndex != props.size(); familyIndex++ ){

				// 1. Is there a family that has the ability to fulfill our requirements?
				// 2. Is a queue from this family still available?

				if ( props[familyIndex].queueFlags & flags ){
					// versatile queue match
					if ( usedQueues[familyIndex] + 1 < props[familyIndex].queueCount ){
						foundMatch = true;
						foundFamily = familyIndex;
						foundIndex = usedQueues[familyIndex] + 1;
						std::cout << "Found versatile queue matching: " << ::vk::to_string( flags ) << std::endl;
					}
					break;
				}
			}
		}

		if ( foundMatch ){
			result.emplace_back( foundFamily, foundIndex, reqIdx );
			usedQueues[foundFamily] = foundIndex; // mark this queue as used
		} else{
			std::cerr << "No available queue matching requirement: " << ::vk::to_string( flags ) << std::endl;

		}

		++reqIdx;
	}

	return result;
}

// ----------------------------------------------------------------------

le_backend_vk_device_o *device_create( le_backend_vk_instance_o *instance_ ) {

	le_backend_vk_device_o *device = new ( le_backend_vk_device_o );

	auto &instance   = instance_->vkInstance;
	auto  deviceList = instance.enumeratePhysicalDevices();

	// CONSIDER: find the best appropriate GPU
	// Select a physical device (GPU) from the above queried list of options.
	// For now, we assume the first one to be the best one.
	device->vkPhysicalDevice = deviceList.front();

	// query the gpu for more info about itself
	device->vkPhysicalDeviceProperties = device->vkPhysicalDevice.getProperties();

//	ofLog() << "GPU Type: " << mPhysicalDeviceProperties.deviceName;

//	{
//		of::vk::RendererSettings tmpSettings;
//		tmpSettings.vkVersion = mPhysicalDeviceProperties.apiVersion;
//		ofLog() << "GPU API Version: " << tmpSettings.getVkVersionMajor() << "."
//		        << tmpSettings.getVersionMinor() << "." << tmpSettings.getVersionPatch();

//		uint32_t driverVersion = mPhysicalDeviceProperties.driverVersion;
//		ofLog() << "GPU Driver Version: " << std::hex << driverVersion;
//	}

	// let's find out the devices' memory properties
	device->vkPhysicalDeviceMemoryProperties = device->vkPhysicalDevice.getMemoryProperties();

	// Check which features must be switched on for default operations.
	// For now, we just make sure we can draw with lines.
	//
	// We should put this into the renderer setttings.
	vk::PhysicalDeviceFeatures deviceFeatures = device->vkPhysicalDevice.getFeatures();
	deviceFeatures
	    .setFillModeNonSolid( VK_TRUE ) // allow wireframe drawing
	    ;

	const auto &queueFamilyProperties = device->vkPhysicalDevice.getQueueFamilyProperties();

	// See findBestMatchForRequestedQueues for how this tuple is laid out.
	auto queriedQueueFamilyAndIndex = findBestMatchForRequestedQueues( queueFamilyProperties, device->queuesWithCapabilitiesRequest );

	// Consolidate queues by queue family type - this will also sort by queue family type.
	{
		std::map<uint32_t, uint32_t> queueCountPerFamily; // queueFamily -> count

		for ( const auto &q : queriedQueueFamilyAndIndex ) {
			// Attempt to insert family to map

			auto insertResult = queueCountPerFamily.insert( {std::get<0>( q ), 1} );
			if ( false == insertResult.second ) {
				// Increment count if family entry already existed in map.
				insertResult.first->second++;
			}
		}

		// Create queues based on queriedQueueFamilyAndIndex
		std::vector<::vk::DeviceQueueCreateInfo> createInfos;
		createInfos.reserve( queriedQueueFamilyAndIndex.size() );

		// We must store this in a map so that the pointer stays
		// alive until we call the api.
		std::map<uint32_t, std::vector<float>> prioritiesPerFamily;

		for ( auto &q : queueCountPerFamily ) {
			vk::DeviceQueueCreateInfo queueCreateInfo;
			const auto &              queueFamily = q.first;
			const auto &              queueCount  = q.second;
			prioritiesPerFamily[ queueFamily ].resize( queueCount, 1.f ); // all queues have the same priority, 1.
			queueCreateInfo
			    .setQueueFamilyIndex( queueFamily )
			    .setQueueCount( queueCount )
			    .setPQueuePriorities( prioritiesPerFamily[ queueFamily ].data() );
			createInfos.emplace_back( std::move( queueCreateInfo ) );
		}

		// TODO: make this optional - get this from outside world.

		std::vector<const char *> enabledDeviceExtensionNames     = {
		    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		};

		vk::DeviceCreateInfo deviceCreateInfo;
		deviceCreateInfo
		    .setQueueCreateInfoCount    ( uint32_t( createInfos.size() ) )
		    .setPQueueCreateInfos       ( createInfos.data() )
		    .setEnabledLayerCount       ( 0 )
		    .setPpEnabledLayerNames     ( nullptr )
		    .setEnabledExtensionCount   ( uint32_t( enabledDeviceExtensionNames.size() ) )
		    .setPpEnabledExtensionNames ( enabledDeviceExtensionNames.data() )
		    .setPEnabledFeatures        ( &deviceFeatures )
		    ;

		// Create device
		device->vkDevice = device->vkPhysicalDevice.createDevice( deviceCreateInfo );
	}

	// Store queue flags, and queue family index per queue into renderer properties,
	// so that queue capabilities and family index may be queried thereafter.

	device->queueFamilyIndices.resize( device->queuesWithCapabilitiesRequest.size() );
	device->queues.resize( queriedQueueFamilyAndIndex.size() );

	// Fetch queue handle into mQueue, matching indices with the original queue request vector
	for ( auto &q : queriedQueueFamilyAndIndex ) {
		const auto &queueFamilyIndex                      = std::get<0>( q );
		const auto &queueIndex                            = std::get<1>( q );
		const auto &requestedQueueIndex                   = std::get<2>( q );
		device->queues[ requestedQueueIndex ]             = device->vkDevice.getQueue( queueFamilyIndex, queueIndex );
		device->queueFamilyIndices[ requestedQueueIndex ] = queueFamilyIndex;
	}


	// Populate indices for default queues - so that default queue may be queried by queue type
	device->defaultQueueIndices.graphics      = findClosestMatchingQueueIndex( device->queuesWithCapabilitiesRequest, ::vk::QueueFlagBits::eGraphics );
	device->defaultQueueIndices.compute       = findClosestMatchingQueueIndex( device->queuesWithCapabilitiesRequest, ::vk::QueueFlagBits::eCompute );
	device->defaultQueueIndices.transfer      = findClosestMatchingQueueIndex( device->queuesWithCapabilitiesRequest, ::vk::QueueFlagBits::eTransfer );
	device->defaultQueueIndices.sparseBinding = findClosestMatchingQueueIndex( device->queuesWithCapabilitiesRequest, ::vk::QueueFlagBits::eSparseBinding );

//	// Create mutexes to protect each queue
//	mQueueMutex = std::vector<mutex>( mQueues.size() );

//	// Query possible depth formats, find the
//	// first format that supports attachment as a depth stencil
//	//
//	// Since all depth formats may be optional, we need to find a suitable depth format to use
//	// Start with the highest precision packed format
//	std::vector<vk::Format> depthFormats = {
//	    vk::Format::eD32SfloatS8Uint,
//	    vk::Format::eD32Sfloat,
//	    vk::Format::eD24UnormS8Uint,
//	    vk::Format::eD16Unorm,
//	    vk::Format::eD16UnormS8Uint};

//	for ( auto &format : depthFormats ) {
//		vk::FormatProperties formatProps = mPhysicalDevice.getFormatProperties( format );
//		// Format must support depth stencil attachment for optimal tiling
//		if ( formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment ) {
//			mDepthFormat = format;
//			break;
//		}
//	}

	return device;
};

// ----------------------------------------------------------------------

void device_increase_reference_count(le_backend_vk_device_o* self){
	++self->referenceCount;
}

// ----------------------------------------------------------------------

void device_decrease_reference_count(le_backend_vk_device_o* self){
	--self->referenceCount;
}

// ----------------------------------------------------------------------

uint32_t device_get_reference_count(le_backend_vk_device_o* self){
	return self->referenceCount;
}

// ----------------------------------------------------------------------

VkDevice device_get_vk_device(le_backend_vk_device_o* self_){
	return self_->vkDevice;
}

// ----------------------------------------------------------------------

VkPhysicalDevice device_get_vk_physical_device(le_backend_vk_device_o* self_){
	return self_->vkPhysicalDevice;
}

uint32_t device_get_default_graphics_queue_family_index(le_backend_vk_device_o *self_){
	return self_->queueFamilyIndices[ self_->defaultQueueIndices.graphics];
};


uint32_t device_get_default_compute_queue_family_index(le_backend_vk_device_o *self_){
	return self_->queueFamilyIndices[self_->defaultQueueIndices.compute];
}

VkQueue device_get_default_graphics_queue(le_backend_vk_device_o* self_){
	return self_->queues[self_->defaultQueueIndices.graphics];
}

VkQueue device_get_default_compute_queue(le_backend_vk_device_o* self_){
	return self_->queues[self_->defaultQueueIndices.compute];
}

// ----------------------------------------------------------------------

void device_destroy( le_backend_vk_device_o *self_ ) {
	self_->vkDevice.destroy();
	delete ( self_ );
};

// ----------------------------------------------------------------------
