#include "le_backend_vk/le_backend_vk.h"
#include "le_backend_types_internal.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <set>
#include <map>

struct le_device_o {

	vk::Device                                vkDevice         = nullptr;
	vk::PhysicalDevice                        vkPhysicalDevice = nullptr;
	vk::PhysicalDeviceProperties              vkPhysicalDeviceProperties;
	vk::PhysicalDeviceMemoryProperties        vkPhysicalDeviceMemoryProperties;
	vk::PhysicalDeviceRayTracingPropertiesKHR raytracingProperties;

	// This may be set externally- it defines how many queues will be created, and what their capabilities must include.
	// queues will be created so that if no exact fit can be found, a queue will be created from the next available family
	// which closest fits requested capabilities.
	//
	std::vector<vk::QueueFlags> queuesWithCapabilitiesRequest = { vk::QueueFlagBits::eGraphics, vk::QueueFlagBits::eCompute };
	std::vector<uint32_t>       queueFamilyIndices;
	std::vector<vk::Queue>      queues;

	struct DefaultQueueIndices {
		uint32_t graphics      = ~uint32_t( 0 );
		uint32_t compute       = ~uint32_t( 0 );
		uint32_t transfer      = ~uint32_t( 0 );
		uint32_t sparseBinding = ~uint32_t( 0 );
	};

	std::set<std::string> requestedDeviceExtensions;

	DefaultQueueIndices defaultQueueIndices;
	vk::Format          defaultDepthStencilFormat;

	uint32_t referenceCount = 0;
};

// ----------------------------------------------------------------------

uint32_t findClosestMatchingQueueIndex( const std::vector<vk::QueueFlags> &queueFlags_, const vk::QueueFlags &flags ) {

	// Find out the queue family index for a queue best matching the given flags.
	// We use this to find out the index of the Graphics Queue for example.

	for ( uint32_t i = 0; i != queueFlags_.size(); i++ ) {
		if ( queueFlags_[ i ] == flags ) {
			// First perfect match
			return i;
		}
	}

	for ( uint32_t i = 0; i != queueFlags_.size(); i++ ) {
		if ( queueFlags_[ i ] & flags ) {
			// First multi-function queue match
			return i;
		}
	}

	// ---------| invariant: no queue found

	if ( flags & vk::QueueFlagBits::eGraphics ) {
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
std::vector<std::tuple<uint32_t, uint32_t, size_t>> findBestMatchForRequestedQueues( const std::vector<vk::QueueFamilyProperties> &props, const std::vector<::vk::QueueFlags> &reqProps ) {
	std::vector<std::tuple<uint32_t, uint32_t, size_t>> result;

	std::vector<uint32_t> usedQueues( props.size(), ~( uint32_t( 0 ) ) ); // last used queue, per queue family (initialised at -1)

	size_t reqIdx = 0; // original index for requested queue
	for ( const auto &flags : reqProps ) {

		// best match is a queue which does exclusively what we want
		bool     foundMatch  = false;
		uint32_t foundFamily = 0;
		uint32_t foundIndex  = 0;

		for ( uint32_t familyIndex = 0; familyIndex != props.size(); familyIndex++ ) {

			// 1. Is there a family that matches our requirements exactly?
			// 2. Is a queue from this family still available?

			if ( props[ familyIndex ].queueFlags == flags ) {
				// perfect match
				if ( usedQueues[ familyIndex ] + 1 < props[ familyIndex ].queueCount ) {
					foundMatch  = true;
					foundFamily = familyIndex;
					foundIndex  = usedQueues[ familyIndex ] + 1;
					std::cout << "Found dedicated queue matching: " << ::vk::to_string( flags ) << std::endl;
				} else {
					std::cout << "No more dedicated queues available matching: " << ::vk::to_string( flags ) << std::endl;
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

				if ( props[ familyIndex ].queueFlags & flags ) {
					// versatile queue match
					if ( usedQueues[ familyIndex ] + 1 < props[ familyIndex ].queueCount ) {
						foundMatch  = true;
						foundFamily = familyIndex;
						foundIndex  = usedQueues[ familyIndex ] + 1;
						std::cout << "Found versatile queue matching: " << ::vk::to_string( flags ) << std::endl;
					}
					break;
				}
			}
		}

		if ( foundMatch ) {
			result.emplace_back( foundFamily, foundIndex, reqIdx );
			usedQueues[ foundFamily ] = foundIndex; // mark this queue as used
		} else {
			std::cerr << "No available queue matching requirement: " << ::vk::to_string( flags ) << std::endl;
		}

		++reqIdx;
	}

	return result;
}

// ----------------------------------------------------------------------

le_device_o *device_create( le_backend_vk_instance_o *instance_, const char **extension_names, uint32_t extension_names_count ) {

	le_device_o *self = new le_device_o{};

	using namespace le_backend_vk;

	vk::Instance instance   = vk_instance_i.get_vk_instance( instance_ );
	auto         deviceList = instance.enumeratePhysicalDevices();

	self->vkPhysicalDevice = deviceList.front();

	// If there are more than one physical devices are available, find the
	// first device which is a dedicated GPU, if none of these can be found,
	// fall back to the first physical device.

	for ( auto d = deviceList.begin() + 1; d != deviceList.end(); d++ ) {
		auto props = d->getProperties();
		if ( props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ) {
			self->vkPhysicalDevice = *d;
			break;
		}
	}

	self->vkPhysicalDeviceProperties = self->vkPhysicalDevice.getProperties();

	std::cout << "Selected GPU: " << self->vkPhysicalDeviceProperties.deviceName << std::endl
	          << std::flush;

	auto properties2           = self->vkPhysicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPropertiesKHR>();
	self->raytracingProperties = properties2.get<vk::PhysicalDeviceRayTracingPropertiesKHR>();

	// let's find out the devices' memory properties
	self->vkPhysicalDeviceMemoryProperties = self->vkPhysicalDevice.getMemoryProperties();

	const auto &queueFamilyProperties = self->vkPhysicalDevice.getQueueFamilyProperties();

	// See findBestMatchForRequestedQueues for how this tuple is laid out.
	auto queriedQueueFamilyAndIndex = findBestMatchForRequestedQueues( queueFamilyProperties, self->queuesWithCapabilitiesRequest );

	// Create queues based on queriedQueueFamilyAndIndex
	std::vector<::vk::DeviceQueueCreateInfo> device_queue_creation_infos;
	// Consolidate queues by queue family type - this will also sort by queue family type.
	std::map<uint32_t, uint32_t> queueCountPerFamily; // queueFamily -> count

	for ( const auto &q : queriedQueueFamilyAndIndex ) {
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
		device_queue_creation_infos.emplace_back( std::move( queueCreateInfo ) );
	}

	std::vector<const char *> enabledDeviceExtensionNames;

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

		std::cout << "Enabled Device Extensions:" << std::endl;
		for ( auto const &ext : self->requestedDeviceExtensions ) {
			enabledDeviceExtensionNames.emplace_back( ext.c_str() );
			std::cout << "\t + " << ext << std::endl;
		}
		std::cout << std::flush;
	}

	// Vulkan >= 1.2  has per-version structs

	vk::StructureChain<
	    vk::PhysicalDeviceFeatures2,
	    vk::PhysicalDeviceVulkan11Features,
	    vk::PhysicalDeviceVulkan12Features
#ifdef LE_FEATURE_RTX
	    ,
	    vk::PhysicalDeviceRayTracingFeaturesKHR // Optional, based on #define
#endif
#ifdef LE_FEATURE_RTX
	    ,
	    vk::PhysicalDeviceMeshShaderFeaturesNV // Optional, based on #define
#endif
	    >
	    featuresChain{};

	featuresChain.get<vk::PhysicalDeviceFeatures2>()
	    .setFeatures( vk::PhysicalDeviceFeatures()
	                      .setFillModeNonSolid( true )    // allow drawing as wireframe
	                      .setWideLines( true )           // require enable wide lines
	                      .setRobustBufferAccess( false ) // disable robust buffer access
	                      .setVertexPipelineStoresAndAtomics( true )
	                      .setFragmentStoresAndAtomics( true )
	                      .setSampleRateShading( true ) // enable so that we can use sampleShadingEnable
	                      .setGeometryShader( true )    // we want geometry shaders
	                      .setShaderInt16( true )       //
	                      .setShaderFloat64( true )     //
	    );

#ifdef LE_FEATURE_RTX
	featuresChain.get<vk::PhysicalDeviceVulkan12Features>()
	    .setBufferDeviceAddress( true ) // needed for rtx
	    //	    .setBufferDeviceAddressCaptureReplay( true ) // needed for frame debuggers, when using bufferDeviceAddress
	    ;

	featuresChain.get<vk::PhysicalDeviceRayTracingFeaturesKHR>()
	    .setRayTracing( true );
#endif

#ifdef LE_FEATURE_MESH_SHADER_NV

	featuresChain.get<vk::PhysicalDeviceMeshShaderFeaturesNV>()
	    .setMeshShader( true )
	    .setTaskShader( true );

	// We require 8 bit integers, and 16 bit floats for when we use mesh shaders -
	// because most use cases will want to make use of these.

	featuresChain.get<vk::PhysicalDeviceVulkan12Features>()
	    .setShaderInt8( true )    //
	    .setShaderFloat16( true ) //
	    ;
#endif

	featuresChain.get<vk::PhysicalDeviceVulkan12Features>()
	    .setShaderInt8( true )
	    .setShaderFloat16( true );

	vk::DeviceCreateInfo deviceCreateInfo;
	deviceCreateInfo
	    .setPNext( &featuresChain.get<vk::PhysicalDeviceFeatures2>() )
	    .setQueueCreateInfoCount( uint32_t( device_queue_creation_infos.size() ) )
	    .setPQueueCreateInfos( device_queue_creation_infos.data() )
	    .setEnabledLayerCount( 0 )
	    .setPpEnabledLayerNames( nullptr )
	    .setEnabledExtensionCount( uint32_t( enabledDeviceExtensionNames.size() ) )
	    .setPpEnabledExtensionNames( enabledDeviceExtensionNames.data() )
	    .setPEnabledFeatures( nullptr ); // in vulkan >= 1.1 uses next chain to set features

	// Create device
	self->vkDevice = self->vkPhysicalDevice.createDevice( deviceCreateInfo );

	// Store queue flags, and queue family index per queue into renderer properties,
	// so that queue capabilities and family index may be queried thereafter.

	self->queueFamilyIndices.resize( self->queuesWithCapabilitiesRequest.size() );
	self->queues.resize( queriedQueueFamilyAndIndex.size() );

	// Fetch queue handle into mQueue, matching indices with the original queue request vector
	for ( auto &q : queriedQueueFamilyAndIndex ) {
		const auto &queueFamilyIndex                    = std::get<0>( q );
		const auto &queueIndex                          = std::get<1>( q );
		const auto &requestedQueueIndex                 = std::get<2>( q );
		self->queues[ requestedQueueIndex ]             = self->vkDevice.getQueue( queueFamilyIndex, queueIndex );
		self->queueFamilyIndices[ requestedQueueIndex ] = queueFamilyIndex;
	}

	// Populate indices for default queues - so that default queue may be queried by queue type
	self->defaultQueueIndices.graphics      = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, vk::QueueFlagBits::eGraphics );
	self->defaultQueueIndices.compute       = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, vk::QueueFlagBits::eCompute );
	self->defaultQueueIndices.transfer      = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, vk::QueueFlagBits::eTransfer );
	self->defaultQueueIndices.sparseBinding = findClosestMatchingQueueIndex( self->queuesWithCapabilitiesRequest, vk::QueueFlagBits::eSparseBinding );

	// Query possible depth formats, find the
	// first format that supports attachment as a depth stencil
	//
	// Since all depth formats may be optional, we need to find a suitable depth format to use
	// Start with the highest precision packed format
	std::vector<vk::Format> depthFormats = {
	    vk::Format::eD32SfloatS8Uint,
	    vk::Format::eD32Sfloat,
	    vk::Format::eD24UnormS8Uint,
	    vk::Format::eD16Unorm,
	    vk::Format::eD16UnormS8Uint };

	for ( auto &format : depthFormats ) {
		vk::FormatProperties formatProps = self->vkPhysicalDevice.getFormatProperties( format );
		// Format must support depth stencil attachment for optimal tiling
		if ( formatProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment ) {
			self->defaultDepthStencilFormat = format;
			break;
		}
	}

	return self;
};

// ----------------------------------------------------------------------

le_device_o *device_increase_reference_count( le_device_o *self ) {
	++self->referenceCount;
	return self;
}

// ----------------------------------------------------------------------

void device_destroy( le_device_o *self ) {
	self->vkDevice.destroy();
	delete ( self );
};

// ----------------------------------------------------------------------

le_device_o *device_decrease_reference_count( le_device_o *self ) {

	--self->referenceCount;

	if ( self->referenceCount == 0 ) {
		device_destroy( self );
		return nullptr;
	} else {
		return self;
	}
}

// ----------------------------------------------------------------------

uint32_t device_get_reference_count( le_device_o *self ) {
	return self->referenceCount;
}

// ----------------------------------------------------------------------

VkDevice device_get_vk_device( le_device_o *self_ ) {
	return self_->vkDevice;
}

// ----------------------------------------------------------------------

VkPhysicalDevice device_get_vk_physical_device( le_device_o *self_ ) {
	return self_->vkPhysicalDevice;
}

// ----------------------------------------------------------------------

const VkPhysicalDeviceProperties &device_get_vk_physical_device_properties( le_device_o *self ) {
	return self->vkPhysicalDeviceProperties;
}

// ----------------------------------------------------------------------

const VkPhysicalDeviceMemoryProperties &device_get_vk_physical_device_memory_properties( le_device_o *self ) {
	return self->vkPhysicalDeviceMemoryProperties;
}

// ----------------------------------------------------------------------

bool device_get_physical_device_ray_tracing_properties( le_device_o *self, VkPhysicalDeviceRayTracingPropertiesKHR *properties ) {
	*properties = self->raytracingProperties;
#ifdef LE_FEATURE_RTX
	return true;
#else
	return false;
#endif
}

// ----------------------------------------------------------------------

uint32_t device_get_default_graphics_queue_family_index( le_device_o *self_ ) {
	return self_->queueFamilyIndices[ self_->defaultQueueIndices.graphics ];
};

// ----------------------------------------------------------------------

uint32_t device_get_default_compute_queue_family_index( le_device_o *self_ ) {
	return self_->queueFamilyIndices[ self_->defaultQueueIndices.compute ];
}

// ----------------------------------------------------------------------

VkQueue device_get_default_graphics_queue( le_device_o *self_ ) {
	return self_->queues[ self_->defaultQueueIndices.graphics ];
}

// ----------------------------------------------------------------------

VkQueue device_get_default_compute_queue( le_device_o *self_ ) {
	return self_->queues[ self_->defaultQueueIndices.compute ];
}

// ----------------------------------------------------------------------

VkFormatEnum device_get_default_depth_stencil_format( le_device_o *self ) {
	return { self->defaultDepthStencilFormat };
}

// ----------------------------------------------------------------------

// get memory allocation info for best matching memory type that matches any of the type bits and flags
static bool device_get_memory_allocation_info( le_device_o *               self,
                                               const VkMemoryRequirements &memReqs,
                                               const VkFlags &             memPropsRef,
                                               VkMemoryAllocateInfo *      pMemoryAllocationInfo ) {

	if ( !memReqs.size ) {
		pMemoryAllocationInfo->allocationSize  = 0;
		pMemoryAllocationInfo->memoryTypeIndex = ~0u;
		return true;
	}

	const auto &physicalMemProperties = self->vkPhysicalDeviceMemoryProperties;

	const vk::MemoryPropertyFlags memProps{ reinterpret_cast<const vk::MemoryPropertyFlags &>( memPropsRef ) };

	// Find an available memory type that satisfies the requested properties.
	uint32_t memoryTypeIndex;
	for ( memoryTypeIndex = 0; memoryTypeIndex < physicalMemProperties.memoryTypeCount; ++memoryTypeIndex ) {
		if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
		     ( physicalMemProperties.memoryTypes[ memoryTypeIndex ].propertyFlags & memProps ) == memProps ) {
			break;
		}
	}
	if ( memoryTypeIndex >= physicalMemProperties.memoryTypeCount ) {
		std::cout << "ERROR " << __PRETTY_FUNCTION__ << ": MemoryTypeIndex not found" << std::endl;
		std::cout << std::flush;
		return false;
	}

	pMemoryAllocationInfo->allocationSize  = memReqs.size;
	pMemoryAllocationInfo->memoryTypeIndex = memoryTypeIndex;

	return true;
}

// ----------------------------------------------------------------------

static bool device_is_extension_available( le_device_o *self, char const *extension_name ) {
	return self->requestedDeviceExtensions.find( extension_name ) != self->requestedDeviceExtensions.end();
}

// ----------------------------------------------------------------------

// ----------------------------------------------------------------------

void register_le_device_vk_api( void *api_ ) {
	auto  api_i    = static_cast<le_backend_vk_api *>( api_ );
	auto &device_i = api_i->vk_device_i;

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
