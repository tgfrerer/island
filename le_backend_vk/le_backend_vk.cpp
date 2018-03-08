#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h"
#include "le_backend_vk/private/le_device_vk.h"
#include "le_backend_vk/private/le_instance_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"

#include "pal_window/pal_window.h"

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/hash_util.h"  // todo: not cool to include privates!
#include "le_renderer/private/le_renderpass.h"

#include "le_renderer/private/le_renderer_types.h"

#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

#include <iostream>
#include <iomanip>


// herein goes all data which is associated with the current frame
// backend keeps track of multiple frames.
struct BackendFrameData {
	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	uint32_t                       padding                  = 0; // NOTICE: remove if needed.
	std::vector<vk::CommandBuffer> commandBuffers;
	std::vector<vk::Framebuffer>   debugFramebuffers;

	struct ResourceInfo {
		uint64_t   id = 0;
		vk::Format format;
		uint32_t   reserved;
	};

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

	std::unordered_map<uint64_t, ResourceInfo, IdentityHash> resourceTable;

};

// ----------------------------------------------------------------------

// backend data object
struct le_backend_o {

	le_backend_vk_settings_t settings;

	std::unique_ptr<le::Instance> instance;
	std::unique_ptr<le::Device>   device;

	std::unique_ptr<pal::Window>   window; // non-owning
	std::unique_ptr<le::Swapchain> swapchain;

	std::vector<BackendFrameData> mFrames;

	vk::RenderPass          debugRenderPass           = nullptr;
	vk::Pipeline            debugPipeline             = nullptr;
	vk::PipelineCache       debugPipelineCache        = nullptr;
	vk::ShaderModule        debugVertexShaderModule   = nullptr;
	vk::ShaderModule        debugFragmentShaderModule = nullptr;
	vk::PipelineLayout      debugPipelineLayout       = nullptr;
	vk::DescriptorSetLayout debugDescriptorSetLayout  = nullptr;
};

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

	device.destroyRenderPass( self->debugRenderPass );

	if ( self->debugDescriptorSetLayout ) {
		device.destroyDescriptorSetLayout( self->debugDescriptorSetLayout );
	}

	if ( self->debugPipelineLayout ) {
		device.destroyPipelineLayout( self->debugPipelineLayout );
	}

	if ( self->debugPipeline ) {
		device.destroyPipeline( self->debugPipeline );
	}

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
	}

	self->mFrames.clear();

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

static void backend_setup( le_backend_o *self ) {

	auto frameCount = backend_get_num_swapchain_images( self );

	self->mFrames.reserve( frameCount );

	vk::Device device = self->device->getVkDevice();

	assert( device ); // device must come from somewhere! it must have been introduced to backend before, or backend must create device used by everyone else...

	for ( size_t i = 0; i != frameCount; ++i ) {
		auto frameData = BackendFrameData();

		frameData.frameFence               = device.createFence( {} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = device.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = device.createSemaphore( {} );
		frameData.commandPool              = device.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->device->getDefaultGraphicsQueueFamilyIndex()} );

		self->mFrames.emplace_back( std::move( frameData ) );
	}

	// stand-in: create default renderpass.

	{
		std::array<vk::AttachmentDescription, 1> attachments;

		attachments[ 0 ] // color attachment
		    .setFormat( vk::Format( self->swapchain->getSurfaceFormat()->format ) )
		    .setSamples( vk::SampleCountFlagBits::e1 )
		    .setLoadOp( vk::AttachmentLoadOp::eClear )
		    .setStoreOp( vk::AttachmentStoreOp::eStore )
		    .setStencilLoadOp( vk::AttachmentLoadOp::eDontCare )
		    .setStencilStoreOp( vk::AttachmentStoreOp::eDontCare )
		    .setInitialLayout( vk::ImageLayout::eUndefined )
		    .setFinalLayout( vk::ImageLayout::ePresentSrcKHR );
		//		attachments[1]		//depth stencil attachment
		//			.setFormat          ( depthFormat_ )
		//			.setSamples         ( vk::SampleCountFlagBits::e1 )
		//			.setLoadOp          ( vk::AttachmentLoadOp::eClear )
		//			.setStoreOp         ( vk::AttachmentStoreOp::eStore)
		//			.setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
		//			.setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
		//			.setInitialLayout   ( vk::ImageLayout::eUndefined )
		//			.setFinalLayout     ( vk::ImageLayout::eDepthStencilAttachmentOptimal )
		//			;

		// Define 2 attachments, and tell us what layout to use during the subpass
		// Index references attachments from above.

		vk::AttachmentReference colorReference{0, vk::ImageLayout::eColorAttachmentOptimal};
		//vk::AttachmentReference depthReference{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

		vk::SubpassDescription subpassDescription;
		subpassDescription
		    .setPipelineBindPoint( vk::PipelineBindPoint::eGraphics )
		    .setInputAttachmentCount( 0 )
		    .setPInputAttachments( nullptr )
		    .setColorAttachmentCount( 1 )
		    .setPColorAttachments( &colorReference )
		    .setPResolveAttachments( nullptr )
		    .setPDepthStencilAttachment( nullptr ) /* &depthReference */
		    .setPPreserveAttachments( nullptr )
		    .setPreserveAttachmentCount( 0 );

		// Define a external dependency for subpass 0

		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[ 0 ]
		    .setSrcSubpass( VK_SUBPASS_EXTERNAL )                                 // producer
		    .setDstSubpass( 0 )                                                   // consumer
		    .setSrcStageMask( vk::PipelineStageFlagBits::eColorAttachmentOutput ) // we need this because the semaphore waits on colorAttachmentOutput
		    .setDstStageMask( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		    .setSrcAccessMask( vk::AccessFlagBits( 0 ) ) // don't flush anything - nothing needs flushing.
		    .setDstAccessMask( vk::AccessFlagBits::eColorAttachmentWrite )
		    .setDependencyFlags( vk::DependencyFlagBits::eByRegion );

		dependencies[ 1 ]
		    .setSrcSubpass( 0 )                   // producer (last possible subpass == subpass 1)
		    .setDstSubpass( VK_SUBPASS_EXTERNAL ) // consumer
		    .setSrcStageMask( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		    .setDstStageMask( vk::PipelineStageFlagBits::eBottomOfPipe )
		    .setSrcAccessMask( vk::AccessFlagBits::eColorAttachmentWrite ) // this needs to be complete,
		    .setDstAccessMask( vk::AccessFlagBits::eMemoryRead )           // before this can begin
		    .setDependencyFlags( vk::DependencyFlagBits::eByRegion );

		// Define 1 renderpass with 1 subpass

		vk::RenderPassCreateInfo renderPassCreateInfo;
		renderPassCreateInfo
		    .setAttachmentCount( attachments.size() )
		    .setPAttachments( attachments.data() )
		    .setSubpassCount( 1 )
		    .setPSubpasses( &subpassDescription )
		    .setDependencyCount( dependencies.size() )
		    .setPDependencies( dependencies.data() );

		self->debugRenderPass = device.createRenderPass( renderPassCreateInfo );
	}

	// stand-in: create default pipeline

	{
		vk::PipelineCacheCreateInfo pipelineCacheInfo;
		pipelineCacheInfo
		    .setFlags( vk::PipelineCacheCreateFlags() ) // "reserved for future use"
		    .setInitialDataSize( 0 )
		    .setPInitialData( nullptr );

		self->debugPipelineCache = device.createPipelineCache( pipelineCacheInfo );

		static const std::vector<uint32_t> shaderCodeVert{
		// converted using: `glslc vertex_shader.vert fragment_shader.frag -c -mfmt=num`
#include "vertex_shader.vert.spv"
		};

		static const std::vector<uint32_t> shaderCodeFrag{
#include "fragment_shader.frag.spv"
		};

		self->debugVertexShaderModule   = device.createShaderModule( {vk::ShaderModuleCreateFlags(), shaderCodeVert.size() * sizeof( uint32_t ), shaderCodeVert.data()} );
		self->debugFragmentShaderModule = device.createShaderModule( {vk::ShaderModuleCreateFlags(), shaderCodeFrag.size() * sizeof( uint32_t ), shaderCodeFrag.data()} );

		std::array<vk::PipelineShaderStageCreateInfo, 2> pipelineStages;
		pipelineStages[ 0 ]
		    .setFlags( vk::PipelineShaderStageCreateFlags() ) // must be 0 - "reserved for future use"
		    .setStage( vk::ShaderStageFlagBits::eVertex )
		    .setModule( self->debugVertexShaderModule )
		    .setPName( "main" )
		    .setPSpecializationInfo( nullptr );
		pipelineStages[ 1 ]
		    .setFlags( vk::PipelineShaderStageCreateFlags() ) // must be 0 - "reserved for future use"
		    .setStage( vk::ShaderStageFlagBits::eFragment )
		    .setModule( self->debugFragmentShaderModule )
		    .setPName( "main" )
		    .setPSpecializationInfo( nullptr );

		vk::PipelineVertexInputStateCreateInfo vertexInputStageInfo;
		vertexInputStageInfo
		    .setFlags( vk::PipelineVertexInputStateCreateFlags() )
		    .setVertexBindingDescriptionCount( 0 )
		    .setPVertexBindingDescriptions( nullptr )
		    .setVertexAttributeDescriptionCount( 0 )
		    .setPVertexAttributeDescriptions( nullptr );

		// todo: get layout from shader

		vk::DescriptorSetLayoutCreateInfo setLayoutInfo;
		setLayoutInfo
		    .setFlags( vk::DescriptorSetLayoutCreateFlags() )
		    .setBindingCount( 0 )
		    .setPBindings( nullptr );

		self->debugDescriptorSetLayout = device.createDescriptorSetLayout( setLayoutInfo );

		vk::PipelineLayoutCreateInfo layoutCreateInfo;
		layoutCreateInfo
		    .setFlags( vk::PipelineLayoutCreateFlags() ) // "reserved for future use"
		    .setSetLayoutCount( 1 )
		    .setPSetLayouts( &self->debugDescriptorSetLayout )
		    .setPushConstantRangeCount( 0 )
		    .setPPushConstantRanges( nullptr );

		self->debugPipelineLayout = device.createPipelineLayout( layoutCreateInfo );

		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
		inputAssemblyState
		    .setTopology(::vk::PrimitiveTopology::eTriangleList )
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
		    .setPolygonMode(::vk::PolygonMode::eFill )
		    .setCullMode(::vk::CullModeFlagBits::eFront )
		    .setFrontFace(::vk::FrontFace::eCounterClockwise )
		    .setDepthBiasEnable( VK_FALSE )
		    .setDepthBiasConstantFactor( 0.f )
		    .setDepthBiasClamp( 0.f )
		    .setDepthBiasSlopeFactor( 1.f )
		    .setLineWidth( 1.f );

		vk::PipelineMultisampleStateCreateInfo multisampleState;
		multisampleState
		    .setRasterizationSamples(::vk::SampleCountFlagBits::e1 )
		    .setSampleShadingEnable( VK_FALSE )
		    .setMinSampleShading( 0.f )
		    .setPSampleMask( nullptr )
		    .setAlphaToCoverageEnable( VK_FALSE )
		    .setAlphaToOneEnable( VK_FALSE );

		vk::StencilOpState stencilOpState;
		stencilOpState
		    .setFailOp(::vk::StencilOp::eKeep )
		    .setPassOp(::vk::StencilOp::eKeep )
		    .setDepthFailOp(::vk::StencilOp::eKeep )
		    .setCompareOp(::vk::CompareOp::eNever )
		    .setCompareMask( 0 )
		    .setWriteMask( 0 )
		    .setReference( 0 );

		vk::PipelineDepthStencilStateCreateInfo depthStencilState;
		depthStencilState
		    .setDepthTestEnable( VK_FALSE )
		    .setDepthWriteEnable( VK_FALSE )
		    .setDepthCompareOp(::vk::CompareOp::eLessOrEqual )
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
		    .setColorBlendOp(::vk::BlendOp::eAdd )
		    .setAlphaBlendOp(::vk::BlendOp::eAdd )
		    .setSrcColorBlendFactor(::vk::BlendFactor::eOne ) // eOne, because we require premultiplied alpha!
		    .setDstColorBlendFactor(::vk::BlendFactor::eOneMinusSrcAlpha )
		    .setSrcAlphaBlendFactor(::vk::BlendFactor::eOne )
		    .setDstAlphaBlendFactor(::vk::BlendFactor::eZero )
		    .setColorWriteMask(
		        ::vk::ColorComponentFlagBits::eR |
		        ::vk::ColorComponentFlagBits::eG |
		        ::vk::ColorComponentFlagBits::eB |
		        ::vk::ColorComponentFlagBits::eA );

		vk::PipelineColorBlendStateCreateInfo colorBlendState;
		colorBlendState
		    .setLogicOpEnable( VK_FALSE )
		    .setLogicOp(::vk::LogicOp::eClear )
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

		// setup debug pipeline
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
		    .setRenderPass( self->debugRenderPass ) // must be a valid renderpass.
		    .setSubpass( 0 )
		    .setBasePipelineHandle( nullptr )
		    .setBasePipelineIndex( 0 ) // -1 signals not to use a base pipeline index
		    ;

		self->debugPipeline = device.createGraphicsPipeline( self->debugPipelineCache, gpi );
	}
}

// ----------------------------------------------------------------------

static bool backend_clear_frame( le_backend_o *self, size_t frameIndex ) {

	auto &     frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	auto result = device.waitForFences( {frame.frameFence}, true, 100'000'000 );
	if ( result != vk::Result::eSuccess ) {
		return false;
	}

	device.resetFences( {frame.frameFence} );

	for ( auto &fb : frame.debugFramebuffers ) {
		device.destroyFramebuffer( fb );
	}
	frame.debugFramebuffers.clear();

	device.freeCommandBuffers( frame.commandPool, frame.commandBuffers );
	frame.commandBuffers.clear();

	device.resetCommandPool( frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources );

	return true;
};

// ----------------------------------------------------------------------

static bool backend_acquire_swapchain_image( le_backend_o *self, size_t frameIndex ) {
	auto &frame = self->mFrames[ frameIndex ];
	return self->swapchain->acquireNextImage( frame.semaphorePresentComplete, frame.swapchainImageIndex );
};

// ----------------------------------------------------------------------

static inline bool is_depth_stencil_format(vk::Format format_){
	return (format_ >= vk::Format::eD16Unorm && format_ <= vk::Format::eD32SfloatS8Uint);
}

// ----------------------------------------------------------------------

static void backend_create_resource_table(le_backend_o* self, size_t frameIndex, le_graph_builder_o* graph_){
	// we want a list of unique resources referenced in the renderpass,
	// and for each resource we must know its first reference.
	// we also need to know if there are any external resources
	// then, we go through all passes, and we track the resource state for each resource

	auto &frame = self->mFrames[ frameIndex ];

	frame.resourceTable.clear();

	size_t numRenderPasses = 0;
	le_renderpass_o* pPasses = nullptr;
	{
		le::GraphBuilder graph{graph_};
		graph.getPasses(&pPasses,&numRenderPasses);
	}


	for ( size_t i = 0; i!=numRenderPasses; i++) {

		auto &pass = pPasses[i];

		for (auto &resource : pass.imageAttachments){
			BackendFrameData::ResourceInfo info;
			info.id = resource.id;
			info.format = resource.format;
			auto result = frame.resourceTable.insert({info.id,info});
			if (!result.second){
				if (result.first->second.format != info.format){
					std::cerr << "WARNING: Resource '" << resource.debugName << "' re-defined with incompatible format." << std::endl;
				}
			}
		}
	}
}

// ----------------------------------------------------------------------

static void backend_track_resource_state(le_backend_o* self, size_t frameIndex, le_graph_builder_o* graph_ ){
	// track resource state

	// we should mark persistent resources which are not frame-local with special flags, so that they
	// come with an initial element in their sync chain, this element signals their last (frame-crossing) state
	// this naturally applies to "backbuffer", for example.


	// a pipeline barrier is defined as a combination of execution dependency and
	// memory dependency.
	// An EXECUTION DEPENDENCY tells us which stage needs to be complete (srcStage) before another named stage (dstStage) may execute.
	// A MEMORY DEPENDECY tells us which memory needs to be made available/flushed (srcAccess) after srcStage
	// before another memory can be made visible/invalidated (dstAccess) before dstStage

	auto &frame = self->mFrames[ frameIndex ];

	auto & syncChainTable = frame.syncChainTable;

	{
		// TODO: frame-external ("persistent") resources such as backbuffer
		// need to be correctly initialised:
		//

		for ( auto &resource : frame.resourceTable ) {
			syncChainTable[ resource.first ].push_back( BackendFrameData::ResourceState{} );
		}

		auto &backbufferState         = syncChainTable[ const_char_hash64( "backbuffer" ) ].front();
		backbufferState.write_stage   = vk::PipelineStageFlagBits::eColorAttachmentOutput; // we need this, since semaphore waits on this stage
		backbufferState.visible_access = vk::AccessFlagBits( 0 );                           // semaphore took care of availability - we can assume memory is already available
	}

	// * sync state: ready to enter renderpass: colorattachmentOutput=visible *

	size_t numRenderPasses = 0;
	le_renderpass_o* pPasses = nullptr;
	{
		le::GraphBuilder graph{graph_};
		graph.getPasses(&pPasses,&numRenderPasses);
	}

	for ( size_t i = 0; i!=numRenderPasses; i++) {

		auto &pass = pPasses[i];

		for ( auto &resource : pass.imageAttachments ) {

			auto &syncChain = syncChainTable[ resource.id ];

			bool isDepthStencil = is_depth_stencil_format( resource.format );

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

			{
				auto & previousSyncState = syncChain.back();
				auto beforeFirstUse{previousSyncState};

				switch ( resource.access_flags ) {
				case le::AccessFlagBits::eReadWrite:
					// resource.loadOp must be LOAD

					// we must now specify which stages need to be visible for which coming memory access
					if ( isDepthStencil ) {
						beforeFirstUse.visible_access = vk::AccessFlagBits::eDepthStencilAttachmentRead;
						beforeFirstUse.write_stage = vk::PipelineStageFlagBits::eEarlyFragmentTests;

					} else {
						// we need to make visible the information from color attachment output stage
						// to anyone using read or write on the color attachment.
						beforeFirstUse.visible_access = vk::AccessFlagBits::eColorAttachmentRead;
						beforeFirstUse.write_stage       = vk::PipelineStageFlagBits::eColorAttachmentOutput;
					}
				    break;

				case le::AccessFlagBits::eWrite:
					// resource.loadOp must be either CLEAR / or DONT_CARE
					beforeFirstUse.write_stage       = isDepthStencil ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eColorAttachmentOutput;
					beforeFirstUse.visible_access    = vk::AccessFlagBits(0);
					beforeFirstUse.layout            = vk::ImageLayout::eUndefined; // override to undefined to invalidate attachment which will be cleared.
				    break;

				case le::AccessFlagBits::eRead:
				    break;
				}

				resource.syncState.idxInitial = syncChain.size();
				syncChain.emplace_back( std::move( beforeFirstUse ) ); // attachment initial state for a renderpass - may be loaded/cleared on first use
				    // * sync state: ready for load/store *
			}


			{
				auto &        previousSyncState = syncChain.back();
				auto beforeSubpass{previousSyncState};

				if ( resource.access_flags == le::AccessFlagBits::eReadWrite ) {
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

				} else if ( resource.access_flags & le::AccessFlagBits::eRead ) {
				} else if ( resource.access_flags & le::AccessFlagBits::eWrite ) {

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
				resource.syncState.idxFinal = syncChain.size();
			}

			// print out info for this resource at this pass.
		}
	}

	// TODO: add final states for resources which are permanent - or are used on another queue
	// this includes backbuffer, and makes sure the backbuffer transitions to the correct state in its last
	// subpass dependency.

	for (auto & syncChainPair : syncChainTable){
		const auto & id = syncChainPair.first;
		auto & syncChain = syncChainPair.second;

		auto finalState{syncChain.back()};

		if (id == const_char_hash64( "backbuffer" )){
			finalState.write_stage  = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.visible_access = vk::AccessFlagBits::eMemoryRead;
			finalState.layout    = vk::ImageLayout::ePresentSrcKHR;
		} else {
			// we mimick implicit dependency here, which exists for a final subpass
			// see p.210 vk spec (chapter 7, render pass)
			finalState.write_stage = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.visible_access = vk::AccessFlagBits(0);
		}

		syncChain.emplace_back(std::move(finalState));
	}

	static_assert(sizeof(le_renderer_api::image_attachment_info_o::SyncState) == sizeof(uint64_t), "must be tightly packed.");

}

// ----------------------------------------------------------------------

static void backend_create_renderpasses(le_backend_o* self, size_t frameIndex, le_graph_builder_o* graph_ ){

	auto &frame = self->mFrames[ frameIndex ];

	// create renderpasses
	auto & syncChainTable = frame.syncChainTable;

	// we use this to mask out any reads in srcAccess, as it never makes sense to flush reads
	const auto ANY_WRITE_ACCESS_FLAGS = ( vk::AccessFlagBits::eColorAttachmentWrite |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eDepthStencilAttachmentWrite |
	                                      vk::AccessFlagBits::eHostWrite |
	                                      vk::AccessFlagBits::eMemoryWrite |
	                                      vk::AccessFlagBits::eShaderWrite |
	                                      vk::AccessFlagBits::eTransferWrite );


	size_t numRenderPasses = 0;
	le_renderpass_o* pPasses = nullptr;
	{
		le::GraphBuilder graph{graph_};
		graph.getPasses(&pPasses,&numRenderPasses);
	}

	for ( size_t i = 0; i!=numRenderPasses; i++) {

		auto &pass = pPasses[i];

		std::vector<vk::AttachmentDescription> attachments;
		attachments.reserve(pass.imageAttachments.size());

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

		for ( auto &attachment : pass.imageAttachments ) {
			auto &syncChain   = syncChainTable.at( attachment.id );
			auto &syncIndices = attachment.syncState;
			auto &syncInitial = syncChain.at( syncIndices.idxInitial );
			auto &syncSubpass = syncChain.at( syncIndices.idxInitial + 1 );
			auto &syncFinal   = syncChain.at( syncIndices.idxFinal );

			bool isDepthStencil = is_depth_stencil_format( attachment.format );

			vk::AttachmentDescription attachmentDescription;
			attachmentDescription
			    .setFlags          ( vk::AttachmentDescriptionFlags() )
			    .setFormat         ( attachment.format )
			    .setSamples        ( vk::SampleCountFlagBits::e1 )
			    .setLoadOp         ( isDepthStencil ? vk::AttachmentLoadOp::eDontCare  : attachment.loadOp )
			    .setStoreOp        ( isDepthStencil ? vk::AttachmentStoreOp::eDontCare : attachment.storeOp )
			    .setStencilLoadOp  ( isDepthStencil ? attachment.loadOp                : vk::AttachmentLoadOp::eDontCare)
			    .setStencilStoreOp ( isDepthStencil ? attachment.storeOp               : vk::AttachmentStoreOp::eDontCare)
			    .setInitialLayout  ( syncInitial.layout )
			    .setFinalLayout    ( syncFinal.layout )
			    ;

			if (false){
				std::cout << "attachment: " << attachment.debugName << std::endl;
				std::cout << "layout initial: " << vk::to_string( syncInitial.layout ) << std::endl;
				std::cout << "layout subpass: " << vk::to_string( syncSubpass.layout ) << std::endl;
				std::cout << "layout   final: " << vk::to_string( syncFinal.layout ) << std::endl;
			}

			attachments.emplace_back( attachmentDescription );
			colorAttachmentReferences.emplace_back( attachments.size() - 1, syncSubpass.layout );

			srcStageFromExternalFlags |= syncInitial.write_stage;
			dstStageFromExternalFlags |= syncSubpass.write_stage;
			srcAccessFromExternalFlags |= ( syncInitial.visible_access & ANY_WRITE_ACCESS_FLAGS);
			dstAccessFromExternalFlags |= syncSubpass.visible_access ; // & ~(syncInitial.visible_access ); // this would make only changes in availability operations happen. it should only happen if there are no src write_access_flags. we leave this out so as to give the driver more info

			// TODO: deal with other subpasses ...

			srcStageToExternalFlags |= syncChain.at( syncIndices.idxFinal - 1 ).write_stage ;
			dstStageToExternalFlags |= syncFinal.write_stage;
			srcAccessToExternalFlags |= ( syncChain.at( syncIndices.idxFinal - 1 ).visible_access  & ANY_WRITE_ACCESS_FLAGS);
			dstAccessToExternalFlags |= syncFinal.visible_access;
		}

		std::vector<vk::SubpassDescription>    subpasses;
		subpasses.reserve(1);

		vk::SubpassDescription subpassDescription;
		subpassDescription
		        .setFlags                   ( vk::SubpassDescriptionFlags() )
		        .setPipelineBindPoint       ( vk::PipelineBindPoint::eGraphics )
		        .setInputAttachmentCount    ( 0 )
		        .setPInputAttachments       ( nullptr )
		        .setColorAttachmentCount    ( uint32_t(colorAttachmentReferences.size()) )
		        .setPColorAttachments       ( colorAttachmentReferences.data() )
		        .setPResolveAttachments     ( nullptr )
		        .setPDepthStencilAttachment ( nullptr )
		        .setPreserveAttachmentCount ( 0 )
		        .setPPreserveAttachments    ( nullptr )
		        ;

		subpasses.emplace_back(subpassDescription);

		std::vector<vk::SubpassDependency> dependencies;
		dependencies.reserve( 2 );
		{
			if (false){

			std::cout << "PASS :'" << pass.debugName << "'" << std::endl;
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
			    .setSrcSubpass      ( 0 )                                  // last subpass
			    .setDstSubpass      ( VK_SUBPASS_EXTERNAL )                // outside of renderpass
			    .setSrcStageMask    ( srcStageToExternalFlags )
			    .setDstStageMask    ( dstStageToExternalFlags )
			    .setSrcAccessMask   ( srcAccessToExternalFlags )
			    .setDstAccessMask   ( dstAccessToExternalFlags )
			    .setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
			    ;

			dependencies.emplace_back(std::move(externalToSubpassDependency));
			dependencies.emplace_back(std::move(subpassToExternalDependency));
		}

		vk::RenderPassCreateInfo renderpassCreateInfo;
		renderpassCreateInfo
		    .setAttachmentCount ( uint32_t(attachments.size())  )
		    .setPAttachments    ( attachments.data()  )
		    .setSubpassCount    ( uint32_t(subpasses.size())    )
		    .setPSubpasses      ( subpasses.data()    )
		    .setDependencyCount ( uint32_t(dependencies.size()) )
		    .setPDependencies   ( dependencies.data() )
		    ;
	}
}

// ----------------------------------------------------------------------

static void backend_process_frame( le_backend_o *self, size_t frameIndex, le_graph_builder_o* graph_) {

	backend_create_resource_table( self, frameIndex, graph_ );
	backend_track_resource_state( self, frameIndex, graph_ );

	// TODO: Go through command stream and substitute any references to (virtual) le resource ids with (actual) api resources.
	// backend_patch_resources(self, frameIndex, graph_);

	backend_create_renderpasses( self, frameIndex, graph_ );

	auto &     frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	static_assert(sizeof(vk::Viewport) == sizeof(le::Viewport), "Viewport data size must be same in vk and le");
	static_assert(sizeof(vk::Rect2D) == sizeof(le::Rect2D), "Rect2D data size must be same in vk and le");

	static const le_renderer_api &rendererApiI       = *Registry::getApi<le_renderer_api>();
	static const auto &           le_graph_builder_i = rendererApiI.le_graph_builder_i;

	le_renderpass_o *passes          = nullptr;
	size_t           numRenderPasses = 0;
	le_graph_builder_i.get_passes( graph_, &passes, &numRenderPasses );


	// TODO: when going wide, there needs to be a commandPool for each execution context so that
	// command buffer generation may be free-threaded.
	uint32_t numCommandBuffers = uint32_t(numRenderPasses);
	auto cmdBufs = device.allocateCommandBuffers( {frame.commandPool, vk::CommandBufferLevel::ePrimary, numCommandBuffers} );

	assert( cmdBufs.size() == 1 ); // for debug purposes

	// TODO: we can go wide here - each renderpass can be processed independently of
	// other renderpasses.
	for ( size_t passIndex = 0; passIndex != numRenderPasses; ++passIndex ) {

		// auto &pass = passes[ i ];
		auto &cmd = cmdBufs[passIndex];

		// create frame buffer, based on swapchain and renderpass

		{
			std::array<vk::ImageView, 1> framebufferAttachments{
				{self->swapchain->getImageView( frame.swapchainImageIndex )}};

			vk::FramebufferCreateInfo framebufferCreateInfo;
			framebufferCreateInfo
			    .setFlags( {} )
			    .setRenderPass( self->debugRenderPass )
			    .setAttachmentCount( uint32_t( framebufferAttachments.size() ) )
			    .setPAttachments( framebufferAttachments.data() )
			    .setWidth( self->swapchain->getImageWidth() )
			    .setHeight( self->swapchain->getImageHeight() )
			    .setLayers( 1 );

			vk::Framebuffer fb = device.createFramebuffer( framebufferCreateInfo );
			frame.debugFramebuffers.emplace_back( std::move( fb ) );
		}

		cmd.begin( {::vk::CommandBufferUsageFlagBits::eOneTimeSubmit} );

		vk::RenderPassBeginInfo renderPassBeginInfo;

		auto renderAreaWidth  = self->swapchain->getImageWidth();
		auto renderAreaHeight = self->swapchain->getImageHeight();

		std::array<vk::ClearValue, 1> clearValues{
			{vk::ClearColorValue( std::array<float, 4>{{0.f, 0.3f, 1.0f, 1.f}} )}};

		renderPassBeginInfo
		    .setRenderPass( self->debugRenderPass )
		    .setFramebuffer( frame.debugFramebuffers.back() )
		    .setRenderArea( vk::Rect2D( {0, 0}, {renderAreaWidth, renderAreaHeight} ) )
		    .setClearValueCount( uint32_t( clearValues.size() ) )
		    .setPClearValues( clearValues.data() );

		cmd.beginRenderPass( renderPassBeginInfo, vk::SubpassContents::eInline );
		cmd.bindPipeline( vk::PipelineBindPoint::eGraphics, self->debugPipeline );

		// Translate intermediary command stream data to api-native instructions

		void * commandStream = nullptr;
		size_t dataSize      = 0;
		size_t numCommands   = 0;
		size_t commandIndex  = 0;

		le_graph_builder_i.get_encoded_data_for_pass( graph_, passIndex, &commandStream, &dataSize, &numCommands );

		if (commandStream != nullptr && numCommands > 0){

			void * dataIt = commandStream;

			while ( commandIndex != numCommands ) {

				le::CommandHeader *header = static_cast<le::CommandHeader *>( dataIt );

				switch ( header->info.type ) {

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
					cmd.setViewport( le_cmd->info.firstViewport, le_cmd->info.viewportCount, reinterpret_cast<vk::Viewport *>( le_cmd->info.pViewports ) );
				} break;

				case le::CommandType::eSetScissor: {
					auto *le_cmd = static_cast<le::CommandSetScissor *>( dataIt );
					cmd.setScissor( le_cmd->info.firstScissor, le_cmd->info.scissorCount, reinterpret_cast<vk::Rect2D *>( le_cmd->info.pScissors ) );
				} break;

				}

				// Move iterator by size of current le_command so that it points
				// to the next command in the list.
				dataIt = static_cast<char *>( dataIt ) + header->info.size;

				++commandIndex;
			}
		}

		cmd.endRenderPass();

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

	vk_backend_i.create                   = backend_create;
	vk_backend_i.destroy                  = backend_destroy;
	vk_backend_i.setup                    = backend_setup;
	vk_backend_i.clear_frame              = backend_clear_frame;
	vk_backend_i.acquire_swapchain_image  = backend_acquire_swapchain_image;
	vk_backend_i.create_window_surface    = backend_create_window_surface;
	vk_backend_i.create_swapchain         = backend_create_swapchain;
	vk_backend_i.dispatch_frame           = backend_dispatch_frame;
	vk_backend_i.process_frame            = backend_process_frame;
	vk_backend_i.get_num_swapchain_images = backend_get_num_swapchain_images;
	vk_backend_i.reset_swapchain          = backend_reset_swapchain;

	vk_backend_i.track_resource_state = backend_track_resource_state;

	// register/update submodules inside this plugin

	register_le_device_vk_api( api_ );
	register_le_instance_vk_api( api_ );

	auto &le_instance_vk_i = le_backend_vk_api_i->vk_instance_i;

	if ( le_backend_vk_api_i->cUniqueInstance != nullptr ) {
		le_instance_vk_i.post_reload_hook( le_backend_vk_api_i->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );

}
