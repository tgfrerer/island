#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h"
#include "le_backend_vk/private/le_device_vk.h"
#include "le_backend_vk/private/le_instance_vk.h"
#include "le_backend_vk/private/le_allocator.h"

#include "le_backend_vk/util/spooky/SpookyV2.h" // for hashing pso state

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include "le_swapchain_vk/le_swapchain_vk.h"

#include "pal_window/pal_window.h"

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/hash_util.h"
#include "le_renderer/private/le_renderpass.h"
#include "le_renderer/private/le_renderer_types.h"
#include "le_renderer/private/le_pipeline_types.h"

#include "experimental/filesystem" // for parsing shader source file paths
#include <fstream>                 // for reading shader source files
#include <cstring>                 // for memcpy

#include <vector>
#include <unordered_map>
#include <forward_list>
#include <iostream>
#include <iomanip>
#include <list>

#ifndef PRINT_DEBUG_MESSAGES
#define PRINT_DEBUG_MESSAGES false
#endif

namespace std {
using namespace experimental;
}

struct le_shader_module_o {
	uint64_t              hash_id = 0; ///< hash taken from spirv code
	std::vector<uint32_t> spirv;
	std::filesystem::path filepath; ///< path to source file
	vk::ShaderModule      module = nullptr;
};

struct AbstractPhysicalResource {
	enum Type : uint64_t {
		eUndefined = 0,
		eBuffer,
		eImage,
		eImageView,
		eFramebuffer,
		eRenderPass,
	};
	union {
		uint64_t      asRawData;
		VkBuffer      asBuffer;
		VkImage       asImage;
		VkImageView   asImageView;
		VkFramebuffer asFramebuffer;
		VkRenderPass  asRenderPass;
	};
	Type type;
};

struct AttachmentInfo {
	uint64_t              resource_id;   ///< which resource to look up for resource state
	vk::Image             physicalImage; ///< non-owning reference to physical image
	vk::Format            format;
	vk::AttachmentLoadOp  loadOp;
	vk::AttachmentStoreOp storeOp;
	uint16_t              initialStateOffset; ///< state of resource before entering the renderpass
	uint16_t              finalStateOffset;   ///< state of resource after exiting the renderpass
};

struct Pass {

	AttachmentInfo attachments[ 16 ]; // maximum of 16 color output attachments
	uint32_t       numAttachments;

	LeRenderPassType type;

	vk::Framebuffer framebuffer;
	vk::RenderPass  renderPass;
	uint32_t        width;
	uint32_t        height;
	uint64_t        hash; ///< spooky hash of elements that could influence renderpass compatibility

	struct le_command_buffer_encoder_o *encoder;
};

// herein goes all data which is associated with the current frame
// backend keeps track of multiple frames, exactly one per renderer::FrameData frame.
struct BackendFrameData {
	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	uint32_t                       padding                  = 0; // NOTICE: remove if needed.
	std::vector<vk::CommandBuffer> commandBuffers;

	// ResourceState keeps track of the resource stage *before* a barrier
	struct ResourceState {
		vk::AccessFlags        visible_access; // which memory access must be be visible - if any of these are WRITE accesses, these must be made available(flushed) before next access
		vk::PipelineStageFlags write_stage;    // current or last stage at which write occurs
		vk::ImageLayout        layout;         // current layout
	};

	// With `syncChainTable` and image_attachment_info_o.syncState, we should
	// be able to create renderpasses. Each resource has a sync chain, and each attachment_info
	// has a struct which holds indices into the sync chain telling us where to look
	// up the sync state for a resource at different stages of renderpass construction.
	std::unordered_map<uint64_t, std::vector<ResourceState>, IdentityHash> syncChainTable;

	static_assert( sizeof( VkBuffer ) == sizeof( VkImageView ) && sizeof( VkBuffer ) == sizeof( VkImage ), "size of AbstractPhysicalResource components must be identical" );

	// Todo: clarify ownership of physical resources inside FrameData
	// Q: Does this table actually own the resources?
	// A: It must not: as it is used to map external resources as well.
	std::unordered_map<uint64_t, AbstractPhysicalResource> physicalResources; // map from renderer resource id to physical resources - only contains resources this frame uses.

	std::forward_list<AbstractPhysicalResource> ownedResources; // vk resources retained and destroyed with BackendFrameData

	std::vector<Pass> passes;

	uint32_t backBufferWidth;  // dimensions of swapchain backbuffer, queried on acquire backendresources.
	uint32_t backBufferHeight; // dimensions of swapchain backbuffer, queried on acquire backendresources.

	// todo: one allocator per command buffer eventually -
	// one allocator per frame for now.
	std::vector<le_allocator_o *> allocators;
};

// ----------------------------------------------------------------------

/// \brief backend data object
struct le_backend_o {

	le_backend_vk_settings_t settings;

	std::unique_ptr<le::Instance> instance;
	std::unique_ptr<le::Device>   device;

	std::unique_ptr<pal::Window>   window; // non-owning
	std::unique_ptr<le::Swapchain> swapchain;

	std::vector<BackendFrameData> mFrames;

	std::vector<le_shader_module_o *> shaderModules;

	// These resources are potentially in-flight, and may be used read-only
	// by more than one frame.
	vk::PipelineCache       debugPipelineCache        = nullptr;
	vk::ShaderModule        debugVertexShaderModule   = nullptr;
	vk::ShaderModule        debugFragmentShaderModule = nullptr;
	vk::PipelineLayout      debugPipelineLayout       = nullptr;
	vk::DescriptorSetLayout debugDescriptorSetLayout  = nullptr;

	vk::DeviceMemory debugTransientVertexDeviceMemory = nullptr;
	vk::Buffer       debugTransientVertexBuffer       = nullptr;
};

// ----------------------------------------------------------------------
static inline VkAttachmentStoreOp le_to_vk( const LeAttachmentStoreOp &lhs ) {
	switch ( lhs ) {
	case ( LE_ATTACHMENT_STORE_OP_STORE ):
	    return VK_ATTACHMENT_STORE_OP_STORE;
	case ( LE_ATTACHMENT_STORE_OP_DONTCARE ):
	    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
}

// ----------------------------------------------------------------------
static inline VkAttachmentLoadOp le_to_vk( const LeAttachmentLoadOp &lhs ) {
	switch ( lhs ) {
	case ( LE_ATTACHMENT_LOAD_OP_LOAD ):
	    return VK_ATTACHMENT_LOAD_OP_LOAD;
	case ( LE_ATTACHMENT_LOAD_OP_CLEAR ):
	    return VK_ATTACHMENT_LOAD_OP_CLEAR;
	case ( LE_ATTACHMENT_LOAD_OP_DONTCARE ):
	    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}
}

// ----------------------------------------------------------------------

static inline bool is_depth_stencil_format( vk::Format format_ ) {
	return ( format_ >= vk::Format::eD16Unorm && format_ <= vk::Format::eD32SfloatS8Uint );
}

// ----------------------------------------------------------------------
/// \brief   file loader utility method
/// \details loads file given by filepath and returns a vector of chars if successful
/// \note    returns an empty vector if not successful
static std::vector<char> load_file( const std::filesystem::path &file_path, bool *success ) {

	std::vector<char> contents;

	size_t        fileSize = 0;
	std::ifstream file( file_path, std::ios::in | std::ios::binary | std::ios::ate );

	if ( !file.is_open() ) {
		std::cerr << "Unable to open file: " << std::filesystem::canonical( file_path ) << std::endl
		          << std::flush;
		*success = false;
		return contents;
	}

	std::cout << "OK Opened file:" << std::filesystem::canonical( file_path ) << std::endl
	          << std::flush;

	// ----------| invariant: file is open

	auto endOfFilePos = file.tellg();

	if ( endOfFilePos > 0 ) {
		fileSize = endOfFilePos;
	} else {
		*success = false;
		return contents;
	}

	// ----------| invariant: file has some bytes to read
	contents.resize( fileSize );

	file.seekg( 0, std::ios::beg );
	file.read( contents.data(), endOfFilePos );
	file.close();

	*success = true;
	return contents;
}

// ----------------------------------------------------------------------
/// \brief create vulkan shader module based on file path
static le_shader_module_o *backend_create_shader_module( le_backend_o *self, char const *path ) {
	bool loadSuccessful = false;
	auto raw_spirv      = load_file( path, &loadSuccessful ); // returns a raw byte vector

	size_t numInts = raw_spirv.size() / sizeof( uint32_t );

	if ( !loadSuccessful ) {
		return nullptr;
	}

	// ---------| invariant: load was successful

	if ( raw_spirv.size() != numInts * sizeof( uint32_t ) ) {
		return nullptr;
	}

	// ---------| invariant: source code can be expressed in uint32_t

	le_shader_module_o *module = new le_shader_module_o{};

	module->filepath = path;
	module->hash_id  = fnv_hash64( raw_spirv.data(), raw_spirv.size() );

	module->spirv.resize( numInts );
	std::memcpy( module->spirv.data(), raw_spirv.data(), raw_spirv.size() );

	{
		// -- create vulkan shader object
		vk::Device device = self->device->getVkDevice();
		// flags must be 0 (reserved for future use), size is given in bytes
		vk::ShaderModuleCreateInfo createInfo( vk::ShaderModuleCreateFlags(), module->spirv.size() * sizeof( uint32_t ), module->spirv.data() );

		module->module = device.createShaderModule( createInfo );
	}

	// TODO (shader): -- Check if module is already present in renderer

	// -- retain module in renderer
	self->shaderModules.push_back( module );

	return module;
}

// ----------------------------------------------------------------------

static le_backend_o *backend_create( le_backend_vk_settings_t *settings ) {
	auto self = new le_backend_o; // todo: leDevice must have been introduced here...

	self->settings = *settings;

	self->instance = std::make_unique<le::Instance>( self->settings.requestedExtensions, self->settings.numRequestedExtensions );
	self->device   = std::make_unique<le::Device>( *self->instance );

	return self;
}

// ----------------------------------------------------------------------

static void backend_destroy( le_backend_o *self ) {

	vk::Device device = self->device->getVkDevice();

	// -- destroy retained shader modules

	for ( auto &s : self->shaderModules ) {
		if ( s->module ) {
			device.destroyShaderModule( s->module );
			s->module = nullptr;
		}
		delete ( s );
	}
	self->shaderModules.clear();

	// -- destroy renderpasses

	static auto allocatorInterface = ( *Registry::getApi<le_backend_vk_api>() ).le_allocator_linear_i;

	if ( self->debugDescriptorSetLayout ) {
		device.destroyDescriptorSetLayout( self->debugDescriptorSetLayout );
	}

	if ( self->debugPipelineLayout ) {
		device.destroyPipelineLayout( self->debugPipelineLayout );
	}

	// TODO (pipeline): destroy pipelines
	//if ( self->debugPipeline ) {
	//	device.destroyPipeline( self->debugPipeline );
	//}

	if ( self->debugPipelineCache ) {
		device.destroyPipelineCache( self->debugPipelineCache );
	}

	if ( self->debugVertexShaderModule ) {
		device.destroyShaderModule( self->debugVertexShaderModule );
	}

	if ( self->debugFragmentShaderModule ) {
		device.destroyShaderModule( self->debugFragmentShaderModule );
	}

	for ( auto &frameData : self->mFrames ) {
		device.destroyFence( frameData.frameFence );
		device.destroySemaphore( frameData.semaphorePresentComplete );
		device.destroySemaphore( frameData.semaphoreRenderComplete );
		device.destroyCommandPool( frameData.commandPool );

		for ( auto &a : frameData.allocators ) {
			allocatorInterface.destroy( a );
		}

		frameData.allocators.clear();
	}

	self->mFrames.clear();

	// Note: teardown for transient buffer and memory.
	device.destroyBuffer( self->debugTransientVertexBuffer );
	device.freeMemory( self->debugTransientVertexDeviceMemory );

	delete self;
}

// ----------------------------------------------------------------------

static bool backend_create_window_surface( le_backend_o *self, pal_window_o *window_ ) {
	self->window = std::make_unique<pal::Window>( window_ );
	assert( self->instance );
	assert( self->instance->getVkInstance() );
	bool success = self->window->createSurface( self->instance->getVkInstance() );
	return success;
}

// ----------------------------------------------------------------------

static void backend_create_swapchain( le_backend_o *self, le_swapchain_vk_settings_o *swapchainSettings_ ) {

	assert( self->window );

	le_swapchain_vk_settings_o tmpSwapchainSettings;

	tmpSwapchainSettings.imagecount_hint                = 3;
	tmpSwapchainSettings.presentmode_hint               = le::Swapchain::Presentmode::eFifo;
	tmpSwapchainSettings.width_hint                     = self->window->getSurfaceWidth();
	tmpSwapchainSettings.height_hint                    = self->window->getSurfaceHeight();
	tmpSwapchainSettings.vk_device                      = self->device->getVkDevice();
	tmpSwapchainSettings.vk_physical_device             = self->device->getVkPhysicalDevice();
	tmpSwapchainSettings.vk_surface                     = self->window->getVkSurfaceKHR();
	tmpSwapchainSettings.vk_graphics_queue_family_index = self->device->getDefaultGraphicsQueueFamilyIndex();

	self->swapchain = std::make_unique<le::Swapchain>( &tmpSwapchainSettings );
}

// ----------------------------------------------------------------------

static size_t backend_get_num_swapchain_images( le_backend_o *self ) {
	assert( self->swapchain );
	return self->swapchain->getImagesCount();
}

// ----------------------------------------------------------------------

void backend_reset_swapchain( le_backend_o *self ) {
	self->swapchain->reset();
}

// ----------------------------------------------------------------------
// TODO (pipeline): allow us to pass in a renderpass, renderpass must have its own hash,
// which is a hash which includes only fields which contribute to renderpass compatibility
// (so that compatible renderpasses have the same hash.)
static vk::Pipeline backend_create_pipeline( le_backend_o *self, le_graphics_pipeline_state_o const *pso, const vk::RenderPass &renderpass, uint32_t subpass ) {
	vk::Device vkDevice = self->device->getVkDevice();

	std::array<vk::PipelineShaderStageCreateInfo, 2> pipelineStages;
	pipelineStages[ 0 ]
	    .setFlags( vk::PipelineShaderStageCreateFlags() ) // must be 0 - "reserved for future use"
	    .setStage( vk::ShaderStageFlagBits::eVertex )
	    .setModule( pso->shaderModuleVert->module )
	    .setPName( "main" )
	    .setPSpecializationInfo( nullptr );
	pipelineStages[ 1 ]
	    .setFlags( vk::PipelineShaderStageCreateFlags() ) // must be 0 - "reserved for future use"
	    .setStage( vk::ShaderStageFlagBits::eFragment )
	    .setModule( pso->shaderModuleFrag->module )
	    .setPName( "main" )
	    .setPSpecializationInfo( nullptr );

	vk::VertexInputBindingDescription   vertexBindingDescrition{0, sizeof( float ) * 4};
	vk::VertexInputAttributeDescription vertexAttributeDescription{0, 0, vk::Format::eR32G32B32A32Sfloat, 0};

	vk::PipelineVertexInputStateCreateInfo vertexInputStageInfo;
	vertexInputStageInfo
	    .setFlags( vk::PipelineVertexInputStateCreateFlags() )
	    .setVertexBindingDescriptionCount( 1 )
	    .setPVertexBindingDescriptions( &vertexBindingDescrition )
	    //		    .setVertexBindingDescriptionCount( 0 )
	    //		    .setPVertexBindingDescriptions( nullptr )
	    .setVertexAttributeDescriptionCount( 1 )
	    .setPVertexAttributeDescriptions( &vertexAttributeDescription )
	    //		    .setVertexAttributeDescriptionCount( 0 )
	    //	    .setPVertexAttributeDescriptions( nullptr )
	    ;

	// todo: get layout from shader

	vk::DescriptorSetLayoutCreateInfo setLayoutInfo;
	setLayoutInfo
	    .setFlags( vk::DescriptorSetLayoutCreateFlags() )
	    .setBindingCount( 0 )
	    .setPBindings( nullptr );

	// FIXME: don't overwrite debug layout - only do this for debug pipeline
	if ( !self->debugDescriptorSetLayout ) {
		self->debugDescriptorSetLayout = vkDevice.createDescriptorSetLayout( setLayoutInfo );
	}

	vk::PipelineLayoutCreateInfo layoutCreateInfo;
	layoutCreateInfo
	    .setFlags( vk::PipelineLayoutCreateFlags() ) // "reserved for future use"
	    .setSetLayoutCount( 1 )
	    .setPSetLayouts( &self->debugDescriptorSetLayout )
	    .setPushConstantRangeCount( 0 )
	    .setPPushConstantRanges( nullptr );

	// FIXME: don't overwrite debug layout - only do this for debug pipeline
	if ( !self->debugPipelineLayout ) {
		self->debugPipelineLayout = vkDevice.createPipelineLayout( layoutCreateInfo );
	}

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
	inputAssemblyState
	    .setTopology( ::vk::PrimitiveTopology::eTriangleList )
	    .setPrimitiveRestartEnable( VK_FALSE );

	vk::PipelineTessellationStateCreateInfo tessellationState;
	tessellationState
	    .setPatchControlPoints( 3 );

	// viewport and scissor are tracked as dynamic states, so this object
	// will not get used.
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState
	    .setViewportCount( 1 )
	    .setPViewports( nullptr )
	    .setScissorCount( 1 )
	    .setPScissors( nullptr );

	vk::PipelineRasterizationStateCreateInfo rasterizationState;
	rasterizationState
	    .setDepthClampEnable( VK_FALSE )
	    .setRasterizerDiscardEnable( VK_FALSE )
	    .setPolygonMode( ::vk::PolygonMode::eFill )
	    .setCullMode( ::vk::CullModeFlagBits::eFront )
	    .setFrontFace( ::vk::FrontFace::eCounterClockwise )
	    .setDepthBiasEnable( VK_FALSE )
	    .setDepthBiasConstantFactor( 0.f )
	    .setDepthBiasClamp( 0.f )
	    .setDepthBiasSlopeFactor( 1.f )
	    .setLineWidth( 1.f );

	vk::PipelineMultisampleStateCreateInfo multisampleState;
	multisampleState
	    .setRasterizationSamples( ::vk::SampleCountFlagBits::e1 )
	    .setSampleShadingEnable( VK_FALSE )
	    .setMinSampleShading( 0.f )
	    .setPSampleMask( nullptr )
	    .setAlphaToCoverageEnable( VK_FALSE )
	    .setAlphaToOneEnable( VK_FALSE );

	vk::StencilOpState stencilOpState;
	stencilOpState
	    .setFailOp( ::vk::StencilOp::eKeep )
	    .setPassOp( ::vk::StencilOp::eKeep )
	    .setDepthFailOp( ::vk::StencilOp::eKeep )
	    .setCompareOp( ::vk::CompareOp::eNever )
	    .setCompareMask( 0 )
	    .setWriteMask( 0 )
	    .setReference( 0 );

	vk::PipelineDepthStencilStateCreateInfo depthStencilState;
	depthStencilState
	    .setDepthTestEnable( VK_FALSE )
	    .setDepthWriteEnable( VK_FALSE )
	    .setDepthCompareOp( ::vk::CompareOp::eLessOrEqual )
	    .setDepthBoundsTestEnable( VK_FALSE )
	    .setStencilTestEnable( VK_FALSE )
	    .setFront( stencilOpState )
	    .setBack( stencilOpState )
	    .setMinDepthBounds( 0.f )
	    .setMaxDepthBounds( 0.f );

	std::array<vk::PipelineColorBlendAttachmentState, 1> blendAttachmentStates;
	blendAttachmentStates.fill( vk::PipelineColorBlendAttachmentState() );

	blendAttachmentStates[ 0 ]
	    .setBlendEnable( VK_TRUE )
	    .setColorBlendOp( ::vk::BlendOp::eAdd )
	    .setAlphaBlendOp( ::vk::BlendOp::eAdd )
	    .setSrcColorBlendFactor( ::vk::BlendFactor::eOne ) // eOne, because we require premultiplied alpha!
	    .setDstColorBlendFactor( ::vk::BlendFactor::eOneMinusSrcAlpha )
	    .setSrcAlphaBlendFactor( ::vk::BlendFactor::eOne )
	    .setDstAlphaBlendFactor( ::vk::BlendFactor::eZero )
	    .setColorWriteMask(
	        ::vk::ColorComponentFlagBits::eR |
	        ::vk::ColorComponentFlagBits::eG |
	        ::vk::ColorComponentFlagBits::eB |
	        ::vk::ColorComponentFlagBits::eA );

	vk::PipelineColorBlendStateCreateInfo colorBlendState;
	colorBlendState
	    .setLogicOpEnable( VK_FALSE )
	    .setLogicOp( ::vk::LogicOp::eClear )
	    .setAttachmentCount( blendAttachmentStates.size() )
	    .setPAttachments( blendAttachmentStates.data() )
	    .setBlendConstants( {{0.f, 0.f, 0.f, 0.f}} );

	std::array<vk::DynamicState, 2> dynamicStates = {{
	    ::vk::DynamicState::eScissor,
	    ::vk::DynamicState::eViewport,
	}};

	vk::PipelineDynamicStateCreateInfo dynamicState;
	dynamicState
	    .setDynamicStateCount( dynamicStates.size() )
	    .setPDynamicStates( dynamicStates.data() );

	// setup pipeline
	vk::GraphicsPipelineCreateInfo gpi;
	gpi
	    .setFlags( vk::PipelineCreateFlagBits::eAllowDerivatives )
	    .setStageCount( uint32_t( pipelineStages.size() ) )
	    .setPStages( pipelineStages.data() )
	    .setPVertexInputState( &vertexInputStageInfo )
	    .setPInputAssemblyState( &inputAssemblyState )
	    .setPTessellationState( nullptr )
	    .setPViewportState( &viewportState )
	    .setPRasterizationState( &rasterizationState )
	    .setPMultisampleState( &multisampleState )
	    .setPDepthStencilState( &depthStencilState )
	    .setPColorBlendState( &colorBlendState )
	    .setPDynamicState( &dynamicState )
	    .setLayout( self->debugPipelineLayout )
	    .setRenderPass( renderpass ) // must be a valid renderpass.
	    .setSubpass( subpass )
	    .setBasePipelineHandle( nullptr )
	    .setBasePipelineIndex( 0 ) // -1 signals not to use a base pipeline index
	    ;

	auto pipeline = vkDevice.createGraphicsPipeline( self->debugPipelineCache, gpi );
	return pipeline;
}

/// \brief Creates - or returns a pipeline from cache based on current pipeline state
/// \note this method does lock the pipeline cache and is therefore costly.
static vk::Pipeline backend_fetch_pipeline( le_backend_o *self, le_graphics_pipeline_state_o const *pso, const Pass &pass, uint32_t subpass ) {

	uint64_t pso_renderpass_hashes[ 2 ] = {};

	//	pso_renderpass_hashes[ 0 ] = backend_pso_get_hash( pso );
	//	pso_renderpass_hashes[ 1 ] = backend_pass_get_compatible_hash( pass ); // returns hash for compatible renderpass

	// -- create combined hash for pipeline, renderpass

	uint64_t pipeline_hash = SpookyHash::Hash64( pso_renderpass_hashes, sizeof( pso_renderpass_hashes ), 0 );

	// -- look up if pipeline with this hash already exists

	// if not, create pipeline and store it in hash map

	// else return pipeline found in hash map

	return backend_create_pipeline( self, pso, pass.renderPass, subpass );
}

// ----------------------------------------------------------------------

static void backend_setup( le_backend_o *self ) {

	static auto  backendVkAPi = *Registry::getApi<le_backend_vk_api>();
	static auto &allocatorI   = backendVkAPi.le_allocator_linear_i;
	static auto &deviceI      = backendVkAPi.vk_device_i;

	auto frameCount = backend_get_num_swapchain_images( self );

	self->mFrames.reserve( frameCount );

	vk::Device vkDevice = self->device->getVkDevice();

	LE_AllocatorCreateInfo allocatorCreateInfo;

	// allocate & map some memory which we may use for scratch buffers.
	{
		// This memory will be owned by the frame, and we hand out chunks from it
		// via allocators, so that each allocator can only sub-allocate the chunk
		// it actually owns.
		//
		// when the frame is transitioned to be submitted, we can either flush
		// that memory, or, realistically, the memory will be made automatically
		// available throught the implicit synchronisation guarantee that any
		// memory writes are made available when submission occurs. (we probably
		// have to look up how implicit synchronisation works with buffers which
		// are written to outside a regular command buffer, but which are mapped)
		//
		// It's probably easiest for now to allocate the memory for scratch buffers
		// from COHERENT memory - so that we don't have to worry about flushes.
		//

		uint64_t memSize = 4096;
		// TODO: check which alignment we need to consider for a vertex/index buffer
		uint64_t memAlignment = deviceI.get_vk_physical_device_properties( *self->device ).limits.minUniformBufferOffsetAlignment;
		// TODO: check alignment-based memSize calculation is correct
		memSize = frameCount * memAlignment * ( ( memSize + memAlignment - 1 ) / memAlignment );

		uint32_t graphicsQueueFamilyIndex = self->device->getDefaultGraphicsQueueFamilyIndex();

		vk::BufferCreateInfo bufferCreateInfo;
		bufferCreateInfo
		    .setSize( memSize )
		    .setUsage( vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer ) // TODO: add missing flag bits if you want to use this buffer for uniforms, and storage, too.
		    .setSharingMode( vk::SharingMode::eExclusive )                                              // this means only one queue may access this buffer at a time
		    .setQueueFamilyIndexCount( 1 )
		    .setPQueueFamilyIndices( &graphicsQueueFamilyIndex );

		// NOTE: store buffer with the frame - needs to be deleted on teardown.
		self->debugTransientVertexBuffer = vkDevice.createBuffer( bufferCreateInfo );

		vk::MemoryRequirements memReqs = vkDevice.getBufferMemoryRequirements( self->debugTransientVertexBuffer );

		auto memFlags = vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible;

		// TODO: this is not elegant! we must initialise the object first, then
		// pass it as a pointer, otherwise it will not have sane defaults.
		vk::MemoryAllocateInfo memAllocateInfo;

		deviceI.get_memory_allocation_info( *self->device, memReqs,
		                                    reinterpret_cast<VkFlags &>( memFlags ),
		                                    &reinterpret_cast<VkMemoryAllocateInfo &>( memAllocateInfo ) );

		// store allocated memory with backend
		// TODO: each frame may allocate their own memory
		self->debugTransientVertexDeviceMemory = vkDevice.allocateMemory( memAllocateInfo );

		vkDevice.bindBufferMemory( self->debugTransientVertexBuffer, self->debugTransientVertexDeviceMemory, 0 );

		// TODO: track lifetime for bufferHandle

		allocatorCreateInfo.alignment               = memAlignment;
		allocatorCreateInfo.bufferBaseMemoryAddress = static_cast<uint8_t *>( vkDevice.mapMemory( self->debugTransientVertexDeviceMemory, 0, VK_WHOLE_SIZE ) );
		allocatorCreateInfo.resourceId              = RESOURCE_BUFFER_ID( "scratch" );
		allocatorCreateInfo.capacity                = memSize;
	}

	assert( vkDevice ); // device must come from somewhere! It must have been introduced to backend before, or backend must create device used by everyone else...

	for ( size_t i = 0; i != frameCount; ++i ) {
		auto frameData = BackendFrameData();

		frameData.frameFence               = vkDevice.createFence( {} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = vkDevice.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = vkDevice.createSemaphore( {} );
		frameData.commandPool              = vkDevice.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->device->getDefaultGraphicsQueueFamilyIndex()} );

		LE_AllocatorCreateInfo allocInfo = allocatorCreateInfo;
		allocInfo.capacity /= frameCount;
		allocInfo.bufferBaseOffsetInBytes = i * allocInfo.capacity;

		auto allocator = allocatorI.create( allocInfo );

		frameData.allocators.emplace_back( std::move( allocator ) );

		self->mFrames.emplace_back( std::move( frameData ) );
	}

	vk::PipelineCacheCreateInfo pipelineCacheInfo;
	pipelineCacheInfo
	    .setFlags( vk::PipelineCacheCreateFlags() ) // "reserved for future use"
	    .setInitialDataSize( 0 )
	    .setPInitialData( nullptr );

	self->debugPipelineCache = vkDevice.createPipelineCache( pipelineCacheInfo );
}

// ----------------------------------------------------------------------

static void backend_track_resource_state( BackendFrameData &frame, le_renderpass_o *passes, size_t numRenderPasses ) {

	// track resource state

	// we should mark persistent resources which are not frame-local with special flags, so that they
	// come with an initial element in their sync chain, this element signals their last (frame-crossing) state
	// this naturally applies to "backbuffer", for example.

	// a pipeline barrier is defined as a combination of execution dependency and
	// memory dependency.
	// An EXECUTION DEPENDENCY tells us which stage needs to be complete (srcStage) before another named stage (dstStage) may execute.
	// A MEMORY DEPENDECY tells us which memory needs to be made available/flushed (srcAccess) after srcStage
	// before another memory can be made visible/invalidated (dstAccess) before dstStage

	auto &syncChainTable = frame.syncChainTable;

	{
		// TODO: frame-external ("persistent") resources such as backbuffer
		// need to be correctly initialised:
		//

		auto backbufferIt = syncChainTable.find( RESOURCE_IMAGE_ID( "backbuffer" ) );
		if ( backbufferIt != syncChainTable.end() ) {
			auto &backbufferState          = backbufferIt->second.front();
			backbufferState.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput; // we need this, since semaphore waits on this stage
			backbufferState.visible_access = vk::AccessFlagBits( 0 );                           // semaphore took care of availability - we can assume memory is already available
		} else {
			std::cout << "warning: no reference to backbuffer found in renderpasses" << std::flush;
		}
	}

	// * sync state: ready to enter renderpass: colorattachmentOutput=visible *

	// Renderpass implicit sync (per resource):
	// + enter renderpass : INITIAL LAYOUT (layout must match)
	// + layout transition if initial layout and attachment reference layout differ for subpass [ attachment memory is automatically made AVAILABLE | see Spec 6.1.1]
	//   [layout transition happens-before any LOAD OPs: source: amd open source driver | https://github.com/GPUOpen-Drivers/xgl/blob/aa330d8e9acffb578c88193e4abe017c8fe15426/icd/api/renderpass/renderpass_builder.cpp#L819]
	// + load/clear op (executed using INITIAL LAYOUT once before first use per-resource) [ attachment memory must be AVAILABLE ]
	// + enter subpass
	// + command execution [attachment memory must be VISIBLE ]
	// + store op
	// + exit subpass : final layout
	// + exit renderpass
	// + layout transform (if final layout differs)

	frame.passes.reserve( numRenderPasses );

	// TODO: move pass creation to its own method.

	for ( auto pass = passes; pass != passes + numRenderPasses; pass++ ) {

		Pass currentPass{};
		currentPass.type   = pass->type;
		currentPass.width  = frame.backBufferWidth;
		currentPass.height = frame.backBufferHeight;

		for ( auto imageAttachment = pass->imageAttachments; imageAttachment != pass->imageAttachments + pass->imageAttachmentCount; imageAttachment++ ) {

			auto &syncChain = syncChainTable[ imageAttachment->resource_id ];

			bool isDepthStencil = is_depth_stencil_format( imageAttachment->format );

			AttachmentInfo *currentAttachment = currentPass.attachments + currentPass.numAttachments;
			currentPass.numAttachments++;

			currentAttachment->resource_id = imageAttachment->resource_id;
			currentAttachment->format      = imageAttachment->format;
			currentAttachment->loadOp      = vk::AttachmentLoadOp( le_to_vk( imageAttachment->loadOp ) );
			currentAttachment->storeOp     = vk::AttachmentStoreOp( le_to_vk( imageAttachment->storeOp ) );

			{
				// track resource state before entering a subpass

				auto &previousSyncState = syncChain.back();
				auto  beforeFirstUse{previousSyncState};

				switch ( imageAttachment->access_flags ) {
				case le::AccessFlagBits::eReadWrite:
					// resource.loadOp must be LOAD

					// we must now specify which stages need to be visible for which coming memory access
					if ( isDepthStencil ) {
						beforeFirstUse.visible_access = vk::AccessFlagBits::eDepthStencilAttachmentRead;
						beforeFirstUse.write_stage    = vk::PipelineStageFlagBits::eEarlyFragmentTests;

					} else {
						// we need to make visible the information from color attachment output stage
						// to anyone using read or write on the color attachment.
						beforeFirstUse.visible_access = vk::AccessFlagBits::eColorAttachmentRead;
						beforeFirstUse.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
					}
				    break;

				case le::AccessFlagBits::eWrite:
					// resource.loadOp must be either CLEAR / or DONT_CARE
					beforeFirstUse.write_stage    = isDepthStencil ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eColorAttachmentOutput;
					beforeFirstUse.visible_access = vk::AccessFlagBits( 0 );
					beforeFirstUse.layout         = vk::ImageLayout::eUndefined; // override to undefined to invalidate attachment which will be cleared.
				    break;

				case le::AccessFlagBits::eRead:
				    break;
				}

				currentAttachment->initialStateOffset = uint16_t( syncChain.size() );
				syncChain.emplace_back( std::move( beforeFirstUse ) ); // attachment initial state for a renderpass - may be loaded/cleared on first use
				                                                       // * sync state: ready for load/store *
			}

			{
				// track resource state before subpass

				auto &previousSyncState = syncChain.back();
				auto  beforeSubpass{previousSyncState};

				if ( imageAttachment->access_flags == le::AccessFlagBits::eReadWrite ) {
					// resource.loadOp most be LOAD

					// we must now specify which stages need to be visible for which coming memory access
					if ( isDepthStencil ) {
						beforeSubpass.visible_access = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eEarlyFragmentTests;
						beforeSubpass.layout         = vk::ImageLayout::eDepthStencilAttachmentOptimal;
					} else {
						// we need to make visible the information from color attachment output stage
						// to anyone using read or write on the color attachment.
						beforeSubpass.visible_access = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						beforeSubpass.layout         = vk::ImageLayout::eColorAttachmentOptimal;
					}

				} else if ( imageAttachment->access_flags & le::AccessFlagBits::eRead ) {
				} else if ( imageAttachment->access_flags & le::AccessFlagBits::eWrite ) {

					if ( isDepthStencil ) {
						beforeSubpass.visible_access = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eEarlyFragmentTests;
						beforeSubpass.layout         = vk::ImageLayout::eDepthStencilAttachmentOptimal;
					} else {
						beforeSubpass.visible_access = vk::AccessFlagBits::eColorAttachmentWrite;
						beforeSubpass.write_stage    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						beforeSubpass.layout         = vk::ImageLayout::eColorAttachmentOptimal;
					}
				}

				syncChain.emplace_back( std::move( beforeSubpass ) );
			}

			// TODO: here, go through command instructions for renderpass and update resource chain

			// ... NOTE: if resource is modified by commands inside the renderpass, this needs to be added to the sync chain here.

			{
				// Whichever next resource state will be in the sync chain will be the resource state we should transition to
				// when defining the last_subpass_to_external dependency
				// which is why, optimistically, we designate the index of the next, not yet written state here -
				currentAttachment->finalStateOffset = uint16_t( syncChain.size() );
			}

			// print out info for this resource at this pass.
		}

		// note that we "steal" the encoder from the renderer pass -
		// it becomes our job now to destroy it.
		currentPass.encoder = pass->encoder;
		pass->encoder       = nullptr;

		frame.passes.emplace_back( std::move( currentPass ) );
	}

	// TODO: add final states for resources which are permanent - or are used on another queue
	// this includes backbuffer, and makes sure the backbuffer transitions to the correct state in its last
	// subpass dependency.

	for ( auto &syncChainPair : syncChainTable ) {
		const auto &id        = syncChainPair.first;
		auto &      syncChain = syncChainPair.second;

		auto finalState{syncChain.back()};

		if ( id == RESOURCE_IMAGE_ID( "backbuffer" ) ) {
			finalState.write_stage    = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.visible_access = vk::AccessFlagBits::eMemoryRead;
			finalState.layout         = vk::ImageLayout::ePresentSrcKHR;
		} else {
			// we mimick implicit dependency here, which exists for a final subpass
			// see p.210 vk spec (chapter 7, render pass)
			finalState.write_stage    = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.visible_access = vk::AccessFlagBits( 0 );
		}

		syncChain.emplace_back( std::move( finalState ) );
	}
}

// ----------------------------------------------------------------------

static bool backend_clear_frame( le_backend_o *self, size_t frameIndex ) {

	static auto &backendI   = *Registry::getApi<le_backend_vk_api>();
	static auto &allocatorI = backendI.le_allocator_linear_i;

	auto &     frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	auto result = device.waitForFences( {frame.frameFence}, true, 100'000'000 );

	if ( result != vk::Result::eSuccess ) {
		return false;
	}

	// -------- Invariant: fence has been crossed, all resources protected by fence
	//          can now be claimed back.

	device.resetFences( {frame.frameFence} );

	for ( auto &alloc : frame.allocators ) {
		allocatorI.reset( alloc );
	}

	{ // clear resources owned exclusively by this frame

		for ( auto &r : frame.ownedResources ) {
			switch ( r.type ) {
			case AbstractPhysicalResource::eBuffer:
				device.destroyBuffer( r.asBuffer );
			    break;
			case AbstractPhysicalResource::eFramebuffer:
				device.destroyFramebuffer( r.asFramebuffer );
			    break;
			case AbstractPhysicalResource::eImage:
				device.destroyImage( r.asImage );
			    break;
			case AbstractPhysicalResource::eImageView:
				device.destroyImageView( r.asImageView );
			    break;
			case AbstractPhysicalResource::eRenderPass:
				device.destroyRenderPass( r.asRenderPass );
			    break;
			case AbstractPhysicalResource::eUndefined:
				std::cout << __PRETTY_FUNCTION__ << ": abstract physical resource has unknown type (" << std::hex << r.type << ") and cannot be deleted. leaking...";
			    break;
			}
		}
		frame.ownedResources.clear();
	}

	device.freeCommandBuffers( frame.commandPool, frame.commandBuffers );
	frame.commandBuffers.clear();

	// todo: we should probably notify anyone who wanted to recycle these
	// physical resources that they are not in use anymore.
	frame.physicalResources.clear();
	frame.syncChainTable.clear();

	static const auto &le_encoder_api = ( *Registry::getApi<le_renderer_api>() ).le_command_buffer_encoder_i;

	for ( auto &f : frame.passes ) {
		if ( f.encoder ) {
			le_encoder_api.destroy( f.encoder );
			f.encoder = nullptr;
		}
	}
	frame.passes.clear();

	// frame.ownedResources.clear();

	device.resetCommandPool( frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources );

	return true;
};

// ----------------------------------------------------------------------

static void backend_create_renderpasses( BackendFrameData &frame, vk::Device &device ) {

	// create renderpasses
	const auto &syncChainTable = frame.syncChainTable;

	// we use this to mask out any reads in srcAccess, as it never makes sense to flush reads
	const auto ANY_WRITE_ACCESS_FLAGS = ( vk::AccessFlagBits::eColorAttachmentWrite |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eDepthStencilAttachmentWrite |
	                                      vk::AccessFlagBits::eHostWrite |
	                                      vk::AccessFlagBits::eMemoryWrite |
	                                      vk::AccessFlagBits::eShaderWrite |
	                                      vk::AccessFlagBits::eTransferWrite );

	for ( size_t i = 0; i != frame.passes.size(); ++i ) {

		auto &pass = frame.passes[ i ];

		if ( pass.type != LE_RENDER_PASS_TYPE_DRAW ) {
			continue;
		}

		std::vector<vk::AttachmentDescription> attachments;
		attachments.reserve( pass.numAttachments );

		std::vector<vk::AttachmentReference> colorAttachmentReferences;

		// We must accumulate these flags over all attachments - they are the
		// union of all flags required by all attachments in a pass.
		vk::PipelineStageFlags srcStageFromExternalFlags;
		vk::PipelineStageFlags dstStageFromExternalFlags;
		vk::AccessFlags        srcAccessFromExternalFlags;
		vk::AccessFlags        dstAccessFromExternalFlags;

		vk::PipelineStageFlags srcStageToExternalFlags;
		vk::PipelineStageFlags dstStageToExternalFlags;
		vk::AccessFlags        srcAccessToExternalFlags;
		vk::AccessFlags        dstAccessToExternalFlags;

		for ( AttachmentInfo const *attachment = pass.attachments; attachment != pass.attachments + pass.numAttachments; attachment++ ) {

			assert( attachment->resource_id != 0 ); // resource id must not be zero.

			auto &syncChain = syncChainTable.at( attachment->resource_id );

			const auto &syncInitial = syncChain.at( attachment->initialStateOffset );
			const auto &syncSubpass = syncChain.at( attachment->initialStateOffset + 1 );
			const auto &syncFinal   = syncChain.at( attachment->finalStateOffset );

			bool isDepthStencil = is_depth_stencil_format( attachment->format );

			vk::AttachmentDescription attachmentDescription;
			attachmentDescription
			    .setFlags( vk::AttachmentDescriptionFlags() ) // relevant for compatibility
			    .setFormat( attachment->format )              // relevant for compatibility
			    .setSamples( vk::SampleCountFlagBits::e1 )    // relevant for compatibility
			    .setLoadOp( isDepthStencil ? vk::AttachmentLoadOp::eDontCare : attachment->loadOp )
			    .setStoreOp( isDepthStencil ? vk::AttachmentStoreOp::eDontCare : attachment->storeOp )
			    .setStencilLoadOp( isDepthStencil ? attachment->loadOp : vk::AttachmentLoadOp::eDontCare )
			    .setStencilStoreOp( isDepthStencil ? attachment->storeOp : vk::AttachmentStoreOp::eDontCare )
			    .setInitialLayout( syncInitial.layout )
			    .setFinalLayout( syncFinal.layout );

			if ( PRINT_DEBUG_MESSAGES ) {
				std::cout << "attachment: " << std::hex << attachment->resource_id << std::endl;
				std::cout << "layout initial: " << vk::to_string( syncInitial.layout ) << std::endl;
				std::cout << "layout subpass: " << vk::to_string( syncSubpass.layout ) << std::endl;
				std::cout << "layout   final: " << vk::to_string( syncFinal.layout ) << std::endl;
			}

			attachments.emplace_back( attachmentDescription );
			colorAttachmentReferences.emplace_back( attachments.size() - 1, syncSubpass.layout );

			srcStageFromExternalFlags |= syncInitial.write_stage;
			dstStageFromExternalFlags |= syncSubpass.write_stage;
			srcAccessFromExternalFlags |= ( syncInitial.visible_access & ANY_WRITE_ACCESS_FLAGS );
			dstAccessFromExternalFlags |= syncSubpass.visible_access; // & ~(syncInitial.visible_access ); // this would make only changes in availability operations happen. it should only happen if there are no src write_access_flags. we leave this out so as to give the driver more info

			// TODO: deal with other subpasses ...

			srcStageToExternalFlags |= syncChain.at( attachment->finalStateOffset - 1 ).write_stage;
			dstStageToExternalFlags |= syncFinal.write_stage;
			srcAccessToExternalFlags |= ( syncChain.at( attachment->finalStateOffset - 1 ).visible_access & ANY_WRITE_ACCESS_FLAGS );
			dstAccessToExternalFlags |= syncFinal.visible_access;
		}

		std::vector<vk::SubpassDescription> subpasses;
		subpasses.reserve( 1 );

		vk::SubpassDescription subpassDescription;
		subpassDescription
		    .setFlags( vk::SubpassDescriptionFlags() )
		    .setPipelineBindPoint( vk::PipelineBindPoint::eGraphics )
		    .setInputAttachmentCount( 0 )
		    .setPInputAttachments( nullptr )
		    .setColorAttachmentCount( uint32_t( colorAttachmentReferences.size() ) )
		    .setPColorAttachments( colorAttachmentReferences.data() )
		    .setPResolveAttachments( nullptr ) // must be NULL or have same length as colorAttachments
		    .setPDepthStencilAttachment( nullptr )
		    .setPreserveAttachmentCount( 0 )
		    .setPPreserveAttachments( nullptr );

		subpasses.emplace_back( subpassDescription );

		std::vector<vk::SubpassDependency> dependencies;
		dependencies.reserve( 2 );
		{
			if ( PRINT_DEBUG_MESSAGES ) {

				std::cout << "PASS :'"
				          << "index: " << i << " / "
				          << "FIXME: need pass name / identifier "
				          << "'" << std::endl;
				std::cout << "Subpass Dependency: VK_SUBPASS_EXTERNAL to subpass [0]" << std::endl;
				std::cout << "\t srcStage: " << vk::to_string( srcStageFromExternalFlags ) << std::endl;
				std::cout << "\t dstStage: " << vk::to_string( dstStageFromExternalFlags ) << std::endl;
				std::cout << "\tsrcAccess: " << vk::to_string( srcAccessFromExternalFlags ) << std::endl;
				std::cout << "\tdstAccess: " << vk::to_string( dstAccessFromExternalFlags ) << std::endl
				          << std::endl;

				std::cout << "Subpass Dependency: subpass [0] to VK_SUBPASS_EXTERNAL:" << std::endl;
				std::cout << "\t srcStage: " << vk::to_string( srcStageToExternalFlags ) << std::endl;
				std::cout << "\t dstStage: " << vk::to_string( dstStageToExternalFlags ) << std::endl;
				std::cout << "\tsrcAccess: " << vk::to_string( srcAccessToExternalFlags ) << std::endl;
				std::cout << "\tdstAccess: " << vk::to_string( dstAccessToExternalFlags ) << std::endl
				          << std::endl;
			}

			vk::SubpassDependency externalToSubpassDependency;
			externalToSubpassDependency
			    .setSrcSubpass( VK_SUBPASS_EXTERNAL ) // outside of renderpass
			    .setDstSubpass( 0 )                   // first subpass
			    .setSrcStageMask( srcStageFromExternalFlags )
			    .setDstStageMask( dstStageFromExternalFlags )
			    .setSrcAccessMask( srcAccessFromExternalFlags )
			    .setDstAccessMask( dstAccessFromExternalFlags )
			    .setDependencyFlags( vk::DependencyFlagBits::eByRegion );
			vk::SubpassDependency subpassToExternalDependency;
			subpassToExternalDependency
			    .setSrcSubpass( 0 )                   // last subpass
			    .setDstSubpass( VK_SUBPASS_EXTERNAL ) // outside of renderpass
			    .setSrcStageMask( srcStageToExternalFlags )
			    .setDstStageMask( dstStageToExternalFlags )
			    .setSrcAccessMask( srcAccessToExternalFlags )
			    .setDstAccessMask( dstAccessToExternalFlags )
			    .setDependencyFlags( vk::DependencyFlagBits::eByRegion );

			dependencies.emplace_back( std::move( externalToSubpassDependency ) );
			dependencies.emplace_back( std::move( subpassToExternalDependency ) );
		}

		{
			// -- Build hash for compatible renderpass
			//
			// We need to include all information that defines renderpass compatibility.
			//
			// We are not clear whether subpasses must be identical between two compatible renderpasses,
			// therefore we don't include subpass information in calculating renderpass compatibility.

			// -- 1. hash attachments
			// -- 2. hash subpass descriptions for each subpass
			//       subpass descriptions are structs with vectors of index references to attachments

			{
				uint64_t rp_hash = 0;

				// -- hash attachments
				for ( const auto &a : attachments ) {
					// We use offsetof so that we can get everything from flags to the start of the
					// attachmentdescription to (but not including) loadOp. We assume that struct is tightly packed.

					static_assert( sizeof( vk::AttachmentDescription::flags ) +
					                       sizeof( vk::AttachmentDescription::format ) +
					                       sizeof( vk::AttachmentDescription::samples ) ==
					                   offsetof( vk::AttachmentDescription, loadOp ),
					               "AttachmentDescription struct must be tightly packed for efficient hashing" );

					rp_hash = SpookyHash::Hash64( &a, offsetof( vk::AttachmentDescription, loadOp ), rp_hash );
				}

				// -- Hash subpasses
				for ( const auto &s : subpasses ) {

					// note: attachment references are not that straightforward to hash either, as they contain a layout
					// field, which want to ignore, since it makes no difference for render pass compatibility.

					rp_hash = SpookyHash::Hash64( &s.flags, sizeof( s.flags ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.pipelineBindPoint, sizeof( s.pipelineBindPoint ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.inputAttachmentCount, sizeof( s.inputAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.colorAttachmentCount, sizeof( s.colorAttachmentCount ), rp_hash );
					rp_hash = SpookyHash::Hash64( &s.preserveAttachmentCount, sizeof( s.preserveAttachmentCount ), rp_hash );

					auto calc_hash_for_attachment_references = []( vk::AttachmentReference const *pAttachmentRefs, unsigned int count, uint64_t &seed ) -> uint64_t {
						// We define this as a pure function lambda, and hope for it to be inlined
						if ( pAttachmentRefs == nullptr ) {
							return seed;
						}
						// ----------| invariant: pAttachmentRefs is valid
						for ( auto const *pAr = pAttachmentRefs; pAr != pAttachmentRefs + count; pAr++ ) {
							seed = SpookyHash::Hash64( pAr, sizeof( vk::AttachmentReference::attachment ), seed );
						}
						return seed;
					};

					// -- for each element in attachment reference, add attachment reference index to the hash
					//
					rp_hash = calc_hash_for_attachment_references( s.pColorAttachments, s.colorAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pResolveAttachments, s.colorAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pInputAttachments, s.inputAttachmentCount, rp_hash );
					rp_hash = calc_hash_for_attachment_references( s.pDepthStencilAttachment, 1, rp_hash );

					// -- preserve attachments are special, because they are not stored as attachment references, but as plain indices
					if ( s.pPreserveAttachments ) {
						rp_hash = SpookyHash::Hash64( s.pPreserveAttachments, s.preserveAttachmentCount * sizeof( *s.pPreserveAttachments ), rp_hash );
					}
				}

				// Store *hash for compatible renderpass* with pass so that pipelines can test whether they are compatible.
				//
				// "Compatible renderpass" means the hash is not fully representative of the renderpass,
				// but two renderpasses with same hash should be compatible, as everything that touches
				// renderpass compatibility has been factored into calculating the hash.
				//
				pass.hash = rp_hash;
			}

			vk::RenderPassCreateInfo renderpassCreateInfo;
			renderpassCreateInfo
			    .setAttachmentCount( uint32_t( attachments.size() ) )
			    .setPAttachments( attachments.data() )
			    .setSubpassCount( uint32_t( subpasses.size() ) )
			    .setPSubpasses( subpasses.data() )
			    .setDependencyCount( uint32_t( dependencies.size() ) )
			    .setPDependencies( dependencies.data() );

			// Create vulkan renderpass object
			pass.renderPass = device.createRenderPass( renderpassCreateInfo );

			AbstractPhysicalResource rp;
			rp.type         = AbstractPhysicalResource::eRenderPass;
			rp.asRenderPass = pass.renderPass;

			// Add vulkan renderpass object to list of owned and life-time tracked resources, so that
			// it can be recycled when not needed anymore.
			frame.ownedResources.emplace_front( std::move( rp ) );
		}
	}
}
// ----------------------------------------------------------------------

// create a list of all unique resources referenced by the rendergraph
// and store it with the current backend frame.
static void backend_create_resource_table( BackendFrameData &frame, le_renderpass_o const *passes, size_t numRenderPasses ) {

	frame.syncChainTable.clear();

	for ( le_renderpass_o const *pass = passes; pass != passes + numRenderPasses; pass++ ) {

		// add all write resources to pass
		for ( uint64_t const *resource_id = pass->readResources; resource_id != ( pass->readResources + pass->readResourceCount ); resource_id++ ) {
			frame.syncChainTable.insert( {*resource_id, {BackendFrameData::ResourceState{}}} );
		}

		// add all read resources to pass
		for ( uint64_t const *resource_id = pass->writeResources; resource_id != ( pass->writeResources + pass->writeResourceCount ); resource_id++ ) {
			frame.syncChainTable.insert( {*resource_id, {BackendFrameData::ResourceState{}}} );
		}

		// createResources are a subset of write resources,
		// so by adding write resources these were already added.
	}
}

// ----------------------------------------------------------------------

inline vk::Buffer get_physical_buffer_from_resource_id( const BackendFrameData *frame, uint64_t resourceId ) {
	static_assert( sizeof( AbstractPhysicalResource::asBuffer ) == sizeof( uint64_t ), "size of the union must be 64bit" );
	assert( frame->physicalResources.at( resourceId ).type == AbstractPhysicalResource::eBuffer );
	return frame->physicalResources.at( resourceId ).asBuffer;
}

inline vk::ImageView get_image_view_from_resource_id( const BackendFrameData *frame, uint64_t resourceId ) {
	assert( frame->physicalResources.at( resourceId ).type == AbstractPhysicalResource::eImageView );
	return frame->physicalResources.at( resourceId ).asImageView;
}

inline vk::Image get_image_from_resource_id( const BackendFrameData *frame, uint64_t resourceId ) {
	assert( frame->physicalResources.at( resourceId ).type == AbstractPhysicalResource::eImage );
	return frame->physicalResources.at( resourceId ).asImage;
}

// ----------------------------------------------------------------------
// places physical VkImage handles for all attachmentInfos in all passes
static void backend_patch_attachment_info_images( BackendFrameData &frame ) {
	for ( auto &pass : frame.passes ) {
		for ( AttachmentInfo *attachment = pass.attachments; attachment != pass.attachments + pass.numAttachments; attachment++ ) {
			attachment->physicalImage = get_image_from_resource_id( &frame, attachment->resource_id );
		}
	}
}

// ----------------------------------------------------------------------
// input: Pass
// output: framebuffer, append newly created imageViews to retained resources list.
static void backend_create_frame_buffers( BackendFrameData &frame, vk::Device &device ) {

	for ( auto &pass : frame.passes ) {

		if ( pass.type != LE_RENDER_PASS_TYPE_DRAW ) {
			continue;
		}
		std::vector<vk::ImageView> framebufferAttachments;
		framebufferAttachments.reserve( pass.numAttachments );

		for ( AttachmentInfo const *attachment = pass.attachments; attachment != pass.attachments + pass.numAttachments; attachment++ ) {

			::vk::ImageSubresourceRange subresourceRange;
			subresourceRange
			    .setAspectMask( vk::ImageAspectFlagBits::eColor )
			    .setBaseMipLevel( 0 )
			    .setLevelCount( 1 )
			    .setBaseArrayLayer( 0 )
			    .setLayerCount( 1 );

			::vk::ImageViewCreateInfo imageViewCreateInfo;
			imageViewCreateInfo
			    .setImage( attachment->physicalImage )
			    .setViewType( vk::ImageViewType::e2D )
			    .setFormat( attachment->format ) // FIXME: set correct image format based on swapchain format if need be.
			    .setComponents( vk::ComponentMapping() )
			    .setSubresourceRange( subresourceRange );

			auto imageView = device.createImageView( imageViewCreateInfo );

			framebufferAttachments.push_back( imageView );

			{
				// Retain imageviews in owned resources - they will be released
				// once not needed anymore.

				AbstractPhysicalResource iv;
				iv.type        = AbstractPhysicalResource::eImageView;
				iv.asImageView = imageView;

				frame.ownedResources.emplace_front( std::move( iv ) );
			}
		}

		vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo
		    .setFlags( {} )
		    .setRenderPass( pass.renderPass )
		    .setAttachmentCount( uint32_t( framebufferAttachments.size() ) )
		    .setPAttachments( framebufferAttachments.data() )
		    .setWidth( pass.width )
		    .setHeight( pass.height )
		    .setLayers( 1 );

		pass.framebuffer = device.createFramebuffer( framebufferCreateInfo );

		{
			// Retain framebuffer

			AbstractPhysicalResource fb;
			fb.type          = AbstractPhysicalResource::eFramebuffer;
			fb.asFramebuffer = pass.framebuffer;

			frame.ownedResources.emplace_front( std::move( fb ) );
		}
	}
}

// ----------------------------------------------------------------------
// TODO: this should mark acquired resources as used by this frame -
// so that they can only be destroyed iff this frame has been reset.
static bool backend_acquire_physical_resources( le_backend_o *self, size_t frameIndex, le_renderpass_o *passes, size_t numRenderPasses ) {
	auto &frame = self->mFrames[ frameIndex ];

	if ( !self->swapchain->acquireNextImage( frame.semaphorePresentComplete, frame.swapchainImageIndex ) ) {
		return false;
	}

	// ----------| invariant: swapchain acquisition successful.

	frame.physicalResources[ RESOURCE_IMAGE_ID( "backbuffer" ) ].asImage = self->swapchain->getImage( frame.swapchainImageIndex );
	frame.physicalResources[ RESOURCE_IMAGE_ID( "backbuffer" ) ].type    = AbstractPhysicalResource::eImage;

	frame.physicalResources[ RESOURCE_BUFFER_ID( "scratch" ) ].asBuffer = self->debugTransientVertexBuffer;
	frame.physicalResources[ RESOURCE_BUFFER_ID( "scratch" ) ].type     = AbstractPhysicalResource::eBuffer;

	vk::Device device = self->device->getVkDevice();

	frame.backBufferWidth  = self->swapchain->getImageWidth();
	frame.backBufferHeight = self->swapchain->getImageHeight();

	backend_create_resource_table( frame, passes, numRenderPasses );
	backend_track_resource_state( frame, passes, numRenderPasses );

	backend_create_renderpasses( frame, device );

	// TODO: backend_create_pipelines(frame, device, passes, numRenderPasses);

	// patch and retain physical resources in bulk here, so that
	// each pass may be processed independently
	backend_patch_attachment_info_images( frame );
	backend_create_frame_buffers( frame, device );

	return true;
};

// ----------------------------------------------------------------------

static le_allocator_o *backend_get_transient_allocator( le_backend_o *self, size_t frameIndex ) {

	auto &frame = self->mFrames[ frameIndex ];

	// TODO: (parallelize) make sure to not just return the frame-global allocator, but one allocator per commandbuffer,
	// so that command buffer generation can happen in parallel.
	assert( !frame.allocators.empty() ); // there must be at least one allocator present, and it must have been created in setup()

	return frame.allocators[ 0 ];
}

// ----------------------------------------------------------------------

static void backend_process_frame( le_backend_o *self, size_t frameIndex ) {

	static auto const &le_encoder_api = ( *Registry::getApi<le_renderer_api>() ).le_command_buffer_encoder_i;
	static auto const &vk_device_i    = ( *Registry::getApi<le_backend_vk_api>() ).vk_device_i;

	auto &frame = self->mFrames[ frameIndex ];

	vk::Device device = self->device->getVkDevice();

	static_assert( sizeof( vk::Viewport ) == sizeof( le::Viewport ), "Viewport data size must be same in vk and le" );
	static_assert( sizeof( vk::Rect2D ) == sizeof( le::Rect2D ), "Rect2D data size must be same in vk and le" );

	static auto maxVertexInputBindings = vk_device_i.get_vk_physical_device_properties( *self->device ).limits.maxVertexInputBindings;

	// TODO: (parallelize) when going wide, there needs to be a commandPool for each execution context so that
	// command buffer generation may be free-threaded.
	auto numCommandBuffers = uint32_t( frame.passes.size() );
	auto cmdBufs           = device.allocateCommandBuffers( {frame.commandPool, vk::CommandBufferLevel::ePrimary, numCommandBuffers} );

	// TODO: (parallel for)
	for ( size_t passIndex = 0; passIndex != frame.passes.size(); ++passIndex ) {

		auto &pass = frame.passes[ passIndex ];

		auto &cmd = cmdBufs[ passIndex ];

		// create frame buffer, based on swapchain and renderpass

		cmd.begin( {::vk::CommandBufferUsageFlagBits::eOneTimeSubmit} );

		// non-draw passes don't need renderpasses.
		if ( pass.type == LE_RENDER_PASS_TYPE_DRAW && pass.renderPass ) {

			// TODO: (renderpass): get clear values from renderpass info
			std::array<vk::ClearValue, 1> clearValues{
				{vk::ClearColorValue( std::array<float, 4>{{0.f, 0.3f, 1.0f, 1.f}} )}};

			vk::RenderPassBeginInfo renderPassBeginInfo;
			renderPassBeginInfo
			    .setRenderPass( pass.renderPass )
			    .setFramebuffer( pass.framebuffer )
			    .setRenderArea( vk::Rect2D( {0, 0}, {frame.backBufferWidth, frame.backBufferHeight} ) )
			    .setClearValueCount( uint32_t( clearValues.size() ) )
			    .setPClearValues( clearValues.data() );

			cmd.beginRenderPass( renderPassBeginInfo, vk::SubpassContents::eInline );
		}

		// -- Translate intermediary command stream data to api-native instructions

		void *   commandStream = nullptr;
		size_t   dataSize      = 0;
		size_t   numCommands   = 0;
		size_t   commandIndex  = 0;
		uint32_t subpassIndex  = 0;

		if ( pass.encoder ) {
			le_encoder_api.get_encoded_data( pass.encoder, &commandStream, &dataSize, &numCommands );
		} else {
			// std::cout << "WARNING: pass does not have valid encoder.";
		}

		if ( commandStream != nullptr && numCommands > 0 ) {

			std::vector<vk::Buffer> vertexInputBindings( maxVertexInputBindings, nullptr );
			void *                  dataIt = commandStream;

			while ( commandIndex != numCommands ) {

				auto header = static_cast<le::CommandHeader *>( dataIt );

				switch ( header->info.type ) {

				case le::CommandType::eBindPipeline: {
					auto *le_cmd = static_cast<le::CommandBindPipeline *>( dataIt );
					if ( pass.type == LE_RENDER_PASS_TYPE_DRAW ) {
						// -- todo: potentially compile and create pipeline here, based on current pass and subpass
						// at this point, a valid renderpass must be bound
						//
						// FIXME (pipeline) : this creates a pipeline on every draw loop! - cache pipelines, and get pipeline from cache.
						auto pipeline = backend_create_pipeline( self, le_cmd->info.pipeline, pass.renderPass, subpassIndex );
						cmd.bindPipeline( vk::PipelineBindPoint::eGraphics, pipeline );
					} else if ( pass.type == LE_RENDER_PASS_TYPE_COMPUTE ) {
						// -- TODO: implement compute pass pipeline binding
					}
				} break;

				case le::CommandType::eDraw: {
					auto *le_cmd = static_cast<le::CommandDraw *>( dataIt );
					cmd.draw( le_cmd->info.vertexCount, le_cmd->info.instanceCount, le_cmd->info.firstVertex, le_cmd->info.firstInstance );
				} break;

				case le::CommandType::eDrawIndexed: {
					auto *le_cmd = static_cast<le::CommandDrawIndexed *>( dataIt );
					cmd.drawIndexed( le_cmd->info.indexCount, le_cmd->info.instanceCount, le_cmd->info.firstIndex, le_cmd->info.vertexOffset, le_cmd->info.firstInstance );
				} break;

				case le::CommandType::eSetLineWidth: {
					auto *le_cmd = static_cast<le::CommandSetLineWidth *>( dataIt );
					cmd.setLineWidth( le_cmd->info.width );
				} break;

				case le::CommandType::eSetViewport: {
					auto *le_cmd = static_cast<le::CommandSetViewport *>( dataIt );
					// NOTE: since data for viewports *is stored inline*, we could also increase the typed pointer
					// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
					cmd.setViewport( le_cmd->info.firstViewport, le_cmd->info.viewportCount, reinterpret_cast<vk::Viewport *>( le_cmd + 1 ) );
				} break;

				case le::CommandType::eSetScissor: {
					auto *le_cmd = static_cast<le::CommandSetScissor *>( dataIt );
					// NOTE: since data for scissors *is stored inline*, we could also increase the typed pointer
					// of le_cmd by 1 to reach the next slot in the stream, where the data is stored.
					cmd.setScissor( le_cmd->info.firstScissor, le_cmd->info.scissorCount, reinterpret_cast<vk::Rect2D *>( le_cmd + 1 ) );
				} break;

				case le::CommandType::eBindVertexBuffers: {
					auto *le_cmd = static_cast<le::CommandBindVertexBuffers *>( dataIt );

					uint32_t firstBinding = le_cmd->info.firstBinding;
					uint32_t numBuffers   = le_cmd->info.bindingCount;

					// TODO: here, when we match resource ids to physical resources,
					// we can be much quicker (and safer) if we only search through the resources
					// declared in the setup callback with useResource().

					// fine - but where are these resources actually matched to physical resources?

					// translate le_buffers to vk_buffers
					for ( uint32_t b = 0; b != numBuffers; ++b ) {
						vertexInputBindings[ b + firstBinding ] = get_physical_buffer_from_resource_id( &frame, le_cmd->info.pBuffers[ b ] );
					}

					cmd.bindVertexBuffers( le_cmd->info.firstBinding, le_cmd->info.bindingCount, &vertexInputBindings[ firstBinding ], le_cmd->info.pOffsets );
				} break;
				}

				// Move iterator by size of current le_command so that it points
				// to the next command in the list.
				dataIt = static_cast<char *>( dataIt ) + header->info.size;

				++commandIndex;
			}
		}

		// non-draw passes don't need renderpasses.
		if ( pass.type == LE_RENDER_PASS_TYPE_DRAW && pass.renderPass ) {
			cmd.endRenderPass();
		}

		cmd.end();
	}

	// place command buffer in frame store so that it can be submitted.
	for ( auto &&c : cmdBufs ) {
		frame.commandBuffers.emplace_back( c );
	}
}

// ----------------------------------------------------------------------

static bool backend_dispatch_frame( le_backend_o *self, size_t frameIndex ) {

	auto &frame = self->mFrames[ frameIndex ];

	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = {{::vk::PipelineStageFlagBits::eColorAttachmentOutput}};

	vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount( 1 )
	    .setPWaitSemaphores( &frame.semaphorePresentComplete )
	    .setPWaitDstStageMask( wait_dst_stage_mask.data() )
	    .setCommandBufferCount( uint32_t( frame.commandBuffers.size() ) )
	    .setPCommandBuffers( frame.commandBuffers.data() )
	    .setSignalSemaphoreCount( 1 )
	    .setPSignalSemaphores( &frame.semaphoreRenderComplete );

	auto queue = vk::Queue{self->device->getDefaultGraphicsQueue()};

	queue.submit( {submitInfo}, frame.frameFence );

	bool presentSuccessful = self->swapchain->present( self->device->getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex );

	return presentSuccessful;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_backend_vk_api( void *api_ ) {
	auto  le_backend_vk_api_i = static_cast<le_backend_vk_api *>( api_ );
	auto &vk_backend_i        = le_backend_vk_api_i->vk_backend_i;

	vk_backend_i.create                     = backend_create;
	vk_backend_i.destroy                    = backend_destroy;
	vk_backend_i.setup                      = backend_setup;
	vk_backend_i.create_window_surface      = backend_create_window_surface;
	vk_backend_i.create_swapchain           = backend_create_swapchain;
	vk_backend_i.get_num_swapchain_images   = backend_get_num_swapchain_images;
	vk_backend_i.reset_swapchain            = backend_reset_swapchain;
	vk_backend_i.get_transient_allocator    = backend_get_transient_allocator;
	vk_backend_i.clear_frame                = backend_clear_frame;
	vk_backend_i.acquire_physical_resources = backend_acquire_physical_resources;
	vk_backend_i.process_frame              = backend_process_frame;
	vk_backend_i.dispatch_frame             = backend_dispatch_frame;
	vk_backend_i.create_shader_module       = backend_create_shader_module;

	// register/update submodules inside this plugin

	register_le_device_vk_api( api_ );
	register_le_instance_vk_api( api_ );
	register_le_allocator_linear_api( api_ );

	auto &le_instance_vk_i = le_backend_vk_api_i->vk_instance_i;

	if ( le_backend_vk_api_i->cUniqueInstance != nullptr ) {
		le_instance_vk_i.post_reload_hook( le_backend_vk_api_i->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
