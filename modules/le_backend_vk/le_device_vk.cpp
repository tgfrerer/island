#include "le_backend_vk.h"
#include "le_backend_types_internal.h"
#include "le_log.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <set>
#include <map>

static constexpr auto LOGGER_LABEL = "le_backend";

#ifdef _WIN32
#	define __PRETTY_FUNCTION__ __FUNCSIG__
#endif //

struct le_device_o {

	VkDevice         vkDevice         = nullptr;
	VkPhysicalDevice vkPhysicalDevice = nullptr;

	struct Properties {
		VkPhysicalDeviceProperties2                     device_properties;
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracing_properties;
		VkPhysicalDeviceVulkan11Properties              vk_11_physical_device_properties;
		VkPhysicalDeviceVulkan12Properties              vk_12_physical_device_properties;
		VkPhysicalDeviceVulkan13Properties              vk_13_physical_device_properties;
		//
		VkPhysicalDeviceMemoryProperties2 memory_properties;
		//
		std::vector<VkQueueFamilyProperties2> queue_family_properties;

	} properties;

	struct TimelineSemaphore {
		VkSemaphore semaphore;
		uint64_t    wait_value; // highest value which this semaphore is going to signal - others may wait on this, defaults to 0
	};

	// This may be set externally- it defines how many queues will be created, and what their capabilities must include.
	// queues will be created so that if no exact fit can be found, a queue will be created from the next available family
	// which closest fits requested capabilities.
	//
	std::vector<VkQueueFlags>      queuesWithCapabilitiesRequest = { VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT };
	std::vector<uint32_t>          queueFamilyIndices;
	std::vector<VkQueue>           queues;
	std::vector<TimelineSemaphore> queue_timeline_semaphores; // one timeline semaphore per queue

	struct DefaultQueueIndices {
		uint32_t graphics      = ~uint32_t( 0 );
		uint32_t compute       = ~uint32_t( 0 );
		uint32_t transfer      = ~uint32_t( 0 );
		uint32_t sparseBinding = ~uint32_t( 0 );
	};

	std::set<std::string> requestedDeviceExtensions;

	DefaultQueueIndices defaultQueueIndices;
	VkFormat            defaultDepthStencilFormat;

	uint32_t referenceCount = 0;
};

// ----------------------------------------------------------------------

static std::string le_queue_flags_to_string( le::QueueFlags flags ) {
	std::string result;

	for ( int i = 0; ( flags > 0 ); i++ ) {
		if ( flags & 1 ) {
			result.append( result.empty() ? std::string( le::to_str( le::QueueFlagBits( 1 << i ) ) )
			                              : " | " + std::string( le::to_str( le::QueueFlagBits( 1 << i ) ) ) );
		}
		flags >>= 1;
	}
	return result;
}

// ----------------------------------------------------------------------

uint32_t findClosestMatchingQueueIndex( const std::vector<VkQueueFlags>& haystack, const VkQueueFlags& needle ) {

	// Find out the queue family index for a queue best matching the given flags.
	// We use this to find out the index of the Graphics Queue for example.

	for ( uint32_t i = 0; i != haystack.size(); i++ ) {
		if ( haystack[ i ] == needle ) {
			// First perfect match
			return i;
		}
	}

	for ( uint32_t i = 0; i != haystack.size(); i++ ) {
		if ( haystack[ i ] & needle ) {
			// First multi-function queue match
			return i;
		}
	}

	// ---------| invariant: no queue found

	if ( needle & VK_QUEUE_GRAPHICS_BIT ) {
		static auto logger = LeLog( LOGGER_LABEL );
		logger.error( "Could not find queue family index matching: '%d'", needle );
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
std::vector<std::tuple<uint32_t, uint32_t, size_t>> findBestMatchForRequestedQueues( const std::vector<VkQueueFamilyProperties2>& props, const std::vector<VkQueueFlags>& reqProps ) {

	static auto logger = LeLog( LOGGER_LABEL );

	std::vector<std::tuple<uint32_t, uint32_t, size_t>> result;
	std::vector<uint32_t>                               usedQueues( props.size(), ~( uint32_t( 0 ) ) ); // last used queue, per queue family (initialised at -1)

	size_t reqIdx = 0; // original index for requested queue
	for ( const auto& flags : reqProps ) {

		// best match is a queue which does exclusively what we want
		bool     foundMatch  = false;
		uint32_t foundFamily = 0;
		uint32_t foundIndex  = 0;

		for ( uint32_t familyIndex = 0; familyIndex != props.size(); familyIndex++ ) {

			// 1. Is there a family that matches our requirements exactly?
			// 2. Is a queue from this family still available?

			if ( props[ familyIndex ].queueFamilyProperties.queueFlags == flags ) {
				// perfect match
				if ( usedQueues[ familyIndex ] + 1 < props[ familyIndex ].queueFamilyProperties.queueCount ) {
					foundMatch  = true;
					foundFamily = familyIndex;
					foundIndex  = usedQueues[ familyIndex ] + 1;
					logger.info( "Found dedicated queue matching: '%d'", flags );
				} else {
					logger.info( "No more dedicated queues available matching: '%s'", le_queue_flags_to_string( flags ).c_str() );
				}
				break;
			}
		}

		if ( foundMatch == false ) {

			// If we haven't found a match, we need to find a versatile queue which might
			// be able to fulfill our requirements.

			for ( uint32_t familyIndex = 0; familyIndex != props.size(); familyIndex++ ) {

				// 1. Is there a family that has the ability to fulfill our requirements?
				// 2. Is a queue from this family still available?

				if ( props[ familyIndex ].queueFamilyProperties.queueFlags & flags ) {
					// versatile queue matchvkGetPhysicalDeviceQueueFamilyProperties2
					if ( usedQueues[ familyIndex ] + 1 < props[ familyIndex ].queueFamilyProperties.queueCount ) {
						foundMatch  = true;
						foundFamily = familyIndex;
						foundIndex  = usedQueues[ familyIndex ] + 1;
						logger.info( "Found versatile queue matching: '%s'", le_queue_flags_to_string( flags ).c_str() );
					}
					break;
				}
			}
		}

		if ( foundMatch ) {
			result.emplace_back( foundFamily, foundIndex, reqIdx );
			usedQueues[ foundFamily ] = foundIndex; // mark this queue as used
		} else {
			logger.error( "No available queue matching requirement: '%d'", flags );
		}

		++reqIdx;
	}

	return result;
}

// ----------------------------------------------------------------------

le_device_o* device_create( le_backend_vk_instance_o* backend_instance, const char** extension_names, uint32_t extension_names_count ) {

	static auto logger = LeLog( LOGGER_LABEL );

	le_device_o* self = new le_device_o{};

	{
		self->properties.device_properties = {
		    .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		    .pNext      = &self->properties.vk_11_physical_device_properties,
		    .properties = {},
		};

		self->properties.vk_11_physical_device_properties = {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
		    .pNext = &self->properties.vk_12_physical_device_properties,
		};

		self->properties.vk_12_physical_device_properties = {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
		    .pNext = &self->properties.vk_13_physical_device_properties,
		};

		self->properties.vk_13_physical_device_properties = {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES,
		    .pNext = &self->properties.raytracing_properties,
		};

		self->properties.raytracing_properties = {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
		    .pNext = nullptr,
		};

		self->properties.memory_properties = {
		    .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
		    .pNext            = nullptr, // optional
		    .memoryProperties = {},
		};
	}

	using namespace le_backend_vk;

	VkInstance instance = vk_instance_i.get_vk_instance( backend_instance );

	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );
	std::vector<VkPhysicalDevice> deviceList( deviceCount );
	vkEnumeratePhysicalDevices( instance, &deviceCount, deviceList.data() );

	if ( deviceCount == 0 ) {
		logger.error( "No physical Vulkan device found. Quitting." );
		exit( 1 );
	}
	// ---------| invariant: there is at least one physical device

	{
		// Find the first device which is a dedicated GPU, if none of these can be found,
		// fall back to the first physical device.

		self->vkPhysicalDevice = deviceList.front(); // select the first device as a fallback

		for ( auto d = deviceList.begin(); d != deviceList.end(); d++ ) {
			VkPhysicalDeviceProperties device_properties{};
			vkGetPhysicalDeviceProperties( self->vkPhysicalDevice, &device_properties );
			if ( device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
				self->vkPhysicalDevice = *d;
				break;
			}
		}

		// Fetch extended device properties for the currently selected physical device
		vkGetPhysicalDeviceProperties2( self->vkPhysicalDevice, &self->properties.device_properties );

		logger.info( "Selected GPU: %s", self->properties.device_properties.properties.deviceName );
	}

	// Let's find out the devices' memory properties
	vkGetPhysicalDeviceMemoryProperties2( self->vkPhysicalDevice, &self->properties.memory_properties );

	{
		uint32_t numQueueFamilyProperties = 0;
		vkGetPhysicalDeviceQueueFamilyProperties2( self->vkPhysicalDevice, &numQueueFamilyProperties, nullptr );
		self->properties.queue_family_properties.resize(
		    numQueueFamilyProperties,
		    {
		        .sType                 = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2,
		        .pNext                 = nullptr, // optional
		        .queueFamilyProperties = {},
		    } );
		vkGetPhysicalDeviceQueueFamilyProperties2( self->vkPhysicalDevice, &numQueueFamilyProperties, self->properties.queue_family_properties.data() );
	}

	const auto& queueFamilyProperties = self->properties.queue_family_properties;
	// See findBestMatchForRequestedQueues for how this tuple is laid out.
	auto queriedQueueFamilyAndIndex = findBestMatchForRequestedQueues( queueFamilyProperties, self->queuesWithCapabilitiesRequest );

	// Create queues based on queriedQueueFamilyAndIndex
	std::vector<VkDeviceQueueCreateInfo> device_queue_creation_infos;
	// Consolidate queues by queue family type - this will also sort by queue family type.
	std::map<uint32_t, uint32_t> queueCountPerFamily; // queueFamily -> count

	for ( const auto& q : queriedQueueFamilyAndIndex ) {
		// Attempt to insert family to map

		auto insertResult = queueCountPerFamily.insert( { std::get<0>( q ), 1 } );
		if ( false == insertResult.second ) {
			// Increment count if family entry already existed in map.
			insertResult.first->second++;
		}
	}

	device_queue_creation_infos.reserve( queriedQueueFamilyAndIndex.size() );

	// We must store this in a map so that the pointer stays
	// alive until we call the api.
	std::map<uint32_t, std::vector<float>> prioritiesPerFamily; // todo: use an arena for this

	for ( auto& q : queueCountPerFamily ) {
		VkDeviceQueueCreateInfo queueCreateInfo;
		const auto&             queueFamily = q.first;
		const auto&             queueCount  = q.second;
		prioritiesPerFamily[ queueFamily ].resize( queueCount, 1.f ); // all queues have the same priority, 1.

		queueCreateInfo = {
		    .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		    .pNext            = nullptr,
		    .flags            = 0,
		    .queueFamilyIndex = queueFamily,
		    .queueCount       = queueCount,
		    .pQueuePriorities = prioritiesPerFamily[ queueFamily ].data(),
		};

		device_queue_creation_infos.emplace_back( std::move( queueCreateInfo ) );
	}

	std::vector<const char*> enabledDeviceExtensionNames;

	{
		// We consolidate all requested device extensions in a set,
		// so that we can be sure that each of the names requested
		// is unique.

		auto const extensions_names_end = extension_names + extension_names_count;

		for ( auto ext = extension_names; ext != extensions_names_end; ++ext ) {
			self->requestedDeviceExtensions.insert( *ext );
		}

		// We then copy the strings with the names for requested extensions
		// into this object's storage, so that we can be sure the pointers
		// will not go stale.

		enabledDeviceExtensionNames.reserve( self->requestedDeviceExtensions.size() );

		logger.info( "Enabled Device Extensions:" );
		for ( auto const& ext : self->requestedDeviceExtensions ) {
			enabledDeviceExtensionNames.emplace_back( ext.c_str() );
			logger.info( "\t + %s", ext.c_str() );
		}
	}

	auto features = le_backend_vk::settings_i.get_requested_physical_device_features_chain();

	VkDeviceCreateInfo deviceCreateInfo = {
	    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	    .pNext                   = features, // this contains the features chain from settings
	    .flags                   = 0,
	    .queueCreateInfoCount    = uint32_t( device_queue_creation_infos.size() ),
	    .pQueueCreateInfos       = device_queue_creation_infos.data(),
	    .enabledLayerCount       = 0,
	    .ppEnabledLayerNames     = 0,
	    .enabledExtensionCount   = uint32_t( enabledDeviceExtensionNames.size() ),
	    .ppEnabledExtensionNames = enabledDeviceExtensionNames.data(),
	    .pEnabledFeatures        = nullptr, // must be nullptr, as we're using pNext for the features chain
	};

	// Create device
	vkCreateDevice( self->vkPhysicalDevice, &deviceCreateInfo, nullptr, &self->vkDevice );

	// load device pointers directly, to bypass the device dispatcher for better performance
	volkLoadDevice( self->vkDevice );

	// Store queue flags, and queue family index per queue into renderer properties,
	// so that queue capabilities and family index may be queried thereafter.

	self->queueFamilyIndices.resize( self->queuesWithCapabilitiesRequest.size() );
	self->queues.resize( queriedQueueFamilyAndIndex.size() );

	// Fetch queue handle into mQueue, matching indices with the original queue request vector
	for ( auto& q : queriedQueueFamilyAndIndex ) {
		const auto& queueFamilyIndex    = std::get<0>( q );
		const auto& queueIndex          = std::get<1>( q );
		const auto& requestedQueueIndex = std::get<2>( q );

		vkGetDeviceQueue( self->vkDevice, queueFamilyIndex, queueIndex, &self->queues[ requestedQueueIndex ] );
		self->queueFamilyIndices[ requestedQueueIndex ] = queueFamilyIndex;
	}

	// Populate indices for default queues - so that default queue may be queried by queue type
	self->defaultQueueIndices.graphics      = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, VK_QUEUE_GRAPHICS_BIT );
	self->defaultQueueIndices.compute       = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, VK_QUEUE_COMPUTE_BIT );
	self->defaultQueueIndices.transfer      = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, VK_QUEUE_TRANSFER_BIT );
	self->defaultQueueIndices.sparseBinding = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, VK_QUEUE_SPARSE_BINDING_BIT );

	{
		VkSemaphoreTypeCreateInfo type_info = {
		    .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		    .pNext         = nullptr, // optional
		    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		    .initialValue  = 0,
		};
		VkSemaphoreCreateInfo info = {
		    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		    .pNext = &type_info, // optional
		    .flags = 0,          // optional
		};

		self->queue_timeline_semaphores.resize( self->queues.size() );

		for ( size_t i = 0; i != self->queue_timeline_semaphores.size(); i++ ) {
			vkCreateSemaphore( self->vkDevice, &info, nullptr, &self->queue_timeline_semaphores[ i ].semaphore );
		}
	}
	// Query possible depth formats, find the
	// first format that supports attachment as a depth stencil
	//
	// Since all depth formats may be optional, we need to find a suitable depth format to use
	// Start with the highest precision packed format
	std::vector<VkFormat> depthFormats = {
	    VK_FORMAT_D32_SFLOAT_S8_UINT,
	    VK_FORMAT_D32_SFLOAT,
	    VK_FORMAT_D24_UNORM_S8_UINT,
	    VK_FORMAT_D16_UNORM,
	    VK_FORMAT_D16_UNORM_S8_UINT };

	for ( auto& format : depthFormats ) {
		VkFormatProperties2 props2 = {
		    .sType            = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
		    .pNext            = nullptr, // optional
		    .formatProperties = {},
		};
		;
		vkGetPhysicalDeviceFormatProperties2( self->vkPhysicalDevice, format, &props2 );
		// Format must support depth stencil attachment for optimal tiling
		if ( props2.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) {
			self->defaultDepthStencilFormat = VkFormat( format );
			break;
		}
	}

	return self;
};

// ----------------------------------------------------------------------

le_device_o* device_increase_reference_count( le_device_o* self ) {
	++self->referenceCount;
	return self;
}

// ----------------------------------------------------------------------

void device_destroy( le_device_o* self ) {
	for ( size_t i = 0; i != self->queue_timeline_semaphores.size(); i++ ) {
		vkDestroySemaphore( self->vkDevice, self->queue_timeline_semaphores[ i ].semaphore, nullptr );
	}
	self->queue_timeline_semaphores.clear();
	vkDestroyDevice( self->vkDevice, nullptr );
	delete ( self );
};

// ----------------------------------------------------------------------

le_device_o* device_decrease_reference_count( le_device_o* self ) {

	--self->referenceCount;

	if ( self->referenceCount == 0 ) {
		device_destroy( self );
		return nullptr;
	} else {
		return self;
	}
}

// ----------------------------------------------------------------------

uint32_t device_get_reference_count( le_device_o* self ) {
	return self->referenceCount;
}

// ----------------------------------------------------------------------

VkDevice device_get_vk_device( le_device_o* self_ ) {
	return self_->vkDevice;
}

// ----------------------------------------------------------------------

VkPhysicalDevice device_get_vk_physical_device( le_device_o* self_ ) {
	return self_->vkPhysicalDevice;
}

// ----------------------------------------------------------------------

const VkPhysicalDeviceProperties* device_get_vk_physical_device_properties( le_device_o* self ) {
	return &self->properties.device_properties.properties;
}

// ----------------------------------------------------------------------

const VkPhysicalDeviceMemoryProperties* device_get_vk_physical_device_memory_properties( le_device_o* self ) {
	return &self->properties.memory_properties.memoryProperties;
}

// ----------------------------------------------------------------------

void device_get_physical_device_ray_tracing_properties( le_device_o* self, VkPhysicalDeviceRayTracingPipelinePropertiesKHR* properties ) {
	*properties = self->properties.raytracing_properties;
}

// ----------------------------------------------------------------------

uint32_t device_get_default_graphics_queue_family_index( le_device_o* self_ ) {
	return self_->queueFamilyIndices[ self_->defaultQueueIndices.graphics ];
};

// ----------------------------------------------------------------------

uint32_t device_get_default_compute_queue_family_index( le_device_o* self_ ) {
	return self_->queueFamilyIndices[ self_->defaultQueueIndices.compute ];
}

// ----------------------------------------------------------------------

VkQueue device_get_default_graphics_queue( le_device_o* self_ ) {
	return self_->queues[ self_->defaultQueueIndices.graphics ];
}

// ----------------------------------------------------------------------

VkQueue device_get_default_compute_queue( le_device_o* self_ ) {
	return self_->queues[ self_->defaultQueueIndices.compute ];
}

// ----------------------------------------------------------------------

VkFormatEnum const* device_get_default_depth_stencil_format( le_device_o* self ) {
	return reinterpret_cast<VkFormatEnum const*>( &self->defaultDepthStencilFormat );
}

// ----------------------------------------------------------------------

// get memory allocation info for best matching memory type that matches any of the type bits and flags
static bool device_get_memory_allocation_info( le_device_o*                self,
                                               const VkMemoryRequirements& memReqs,
                                               const VkFlags&              memPropsRef,
                                               VkMemoryAllocateInfo*       pMemoryAllocationInfo ) {

	if ( !memReqs.size ) {
		pMemoryAllocationInfo->allocationSize  = 0;
		pMemoryAllocationInfo->memoryTypeIndex = ~0u;
		return true;
	}

	const auto& physicalMemProperties = self->properties.memory_properties.memoryProperties;

	const VkMemoryPropertyFlags memProps{ reinterpret_cast<const VkMemoryPropertyFlags&>( memPropsRef ) };

	// Find an available memory type that satisfies the requested properties.
	uint32_t memoryTypeIndex;
	for ( memoryTypeIndex = 0; memoryTypeIndex < physicalMemProperties.memoryTypeCount; ++memoryTypeIndex ) {
		if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
		     ( physicalMemProperties.memoryTypes[ memoryTypeIndex ].propertyFlags & memProps ) == memProps ) {
			break;
		}
	}
	if ( memoryTypeIndex >= physicalMemProperties.memoryTypeCount ) {
		static auto logger = LeLog( LOGGER_LABEL );
		logger.error( "%s: MemoryTypeIndex not found", __PRETTY_FUNCTION__ );
		return false;
	}

	pMemoryAllocationInfo->allocationSize  = memReqs.size;
	pMemoryAllocationInfo->memoryTypeIndex = memoryTypeIndex;

	return true;
}

// ----------------------------------------------------------------------

static bool device_is_extension_available( le_device_o* self, char const* extension_name ) {
	return self->requestedDeviceExtensions.find( extension_name ) != self->requestedDeviceExtensions.end();
}

// ----------------------------------------------------------------------

// ----------------------------------------------------------------------

void register_le_device_vk_api( void* api_ ) {
	auto  api_i    = static_cast<le_backend_vk_api*>( api_ );
	auto& device_i = api_i->vk_device_i;

	device_i.create                                        = device_create;
	device_i.destroy                                       = device_destroy;
	device_i.decrease_reference_count                      = device_decrease_reference_count;
	device_i.increase_reference_count                      = device_increase_reference_count;
	device_i.get_reference_count                           = device_get_reference_count;
	device_i.get_default_graphics_queue_family_index       = device_get_default_graphics_queue_family_index;
	device_i.get_default_compute_queue_family_index        = device_get_default_compute_queue_family_index;
	device_i.get_default_graphics_queue                    = device_get_default_graphics_queue;
	device_i.get_default_compute_queue                     = device_get_default_compute_queue;
	device_i.get_default_depth_stencil_format              = device_get_default_depth_stencil_format;
	device_i.get_vk_physical_device                        = device_get_vk_physical_device;
	device_i.get_vk_device                                 = device_get_vk_device;
	device_i.get_vk_physical_device_properties             = device_get_vk_physical_device_properties;
	device_i.get_vk_physical_device_memory_properties      = device_get_vk_physical_device_memory_properties;
	device_i.get_vk_physical_device_ray_tracing_properties = device_get_physical_device_ray_tracing_properties;
	device_i.get_memory_allocation_info                    = device_get_memory_allocation_info;
	device_i.is_extension_available                        = device_is_extension_available;
}
