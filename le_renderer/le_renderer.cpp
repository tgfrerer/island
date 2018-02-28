#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_rendergraph.h"
#include "le_renderer/private/le_renderpass.h"
#include "le_renderer/private/le_command_buffer_encoder.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <future>

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;


// ----------------------------------------------------------------------

struct FrameData {

	enum class State : int32_t {
		eFailedClear    = -4,
		eFailedDispatch = -3,
		eFailedAcquire  = -2,
		eInitial        = -1,
		eCleared        = 0,
		eAcquired,
		eRecorded,
		eProcessed,
		eDispatched,
	};

	struct Meta {
		 NanoTime time_acquire_frame_start;
		 NanoTime time_acquire_frame_end;

		 NanoTime time_process_frame_start;
		 NanoTime time_process_frame_end;

		 NanoTime time_record_frame_start;
		 NanoTime time_record_frame_end;

		 NanoTime time_dispatch_frame_start;
		 NanoTime time_dispatch_frame_end;
	};

	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	State                          state                    = State::eInitial;
	std::vector<vk::CommandBuffer> commandBuffers;
	std::vector<vk::Framebuffer>   debugFramebuffers;

	std::unique_ptr<le::GraphBuilder> graphBuilder;

	Meta meta;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	le::Device     leDevice;
	le::Swapchain  swapchain;
	uint64_t       swapchainDirty  = false;
	vk::Device     vkDevice        = nullptr;

	vk::RenderPass    debugRenderPass    = nullptr;
	vk::Pipeline      debugPipeline      = nullptr;
	vk::PipelineCache debugPipelineCache = nullptr;
	vk::ShaderModule  debugVertexShaderModule = nullptr;
	vk::ShaderModule  debugFragmentShaderModule = nullptr;
	vk::PipelineLayout debugPipelineLayout = nullptr;
	vk::DescriptorSetLayout debugDescriptorSetLayout = nullptr;

	std::vector<FrameData> frames;
	size_t                 numSwapchainImages = 0;
	size_t                 currentFrameNumber = size_t( ~0 ); // ever increasing number of current frame

	le_renderer_o( const le::Device &device_, const le::Swapchain &swapchain_ )
	    : leDevice( device_ )
	    , swapchain( swapchain_ )
	    , vkDevice( leDevice.getVkDevice() ) {
	}
};

// ----------------------------------------------------------------------

static le_renderer_o *renderer_create( le_backend_vk_device_o *device, le_backend_swapchain_o *swapchain ) {
	auto obj = new le_renderer_o( device, swapchain );
	return obj;
}

// ----------------------------------------------------------------------

static void renderer_setup( le_renderer_o *self ) {

	vk::CommandPoolCreateInfo commandPoolCreateInfo;

	self->numSwapchainImages = self->swapchain.getImagesCount();

	self->frames.reserve( self->numSwapchainImages );

	for ( size_t i = 0; i != self->numSwapchainImages; ++i ) {
		auto frameData = FrameData();

		frameData.frameFence               = self->vkDevice.createFence( {} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = self->vkDevice.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = self->vkDevice.createSemaphore( {} );
		frameData.commandPool              = self->vkDevice.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->leDevice.getDefaultGraphicsQueueFamilyIndex()} );

		frameData.graphBuilder = std::make_unique<le::GraphBuilder>();
		self->frames.push_back( std::move( frameData ) );

	}

	self->currentFrameNumber = 0;

	// stand-in: create default renderpass.

	{
		std::array<vk::AttachmentDescription, 1> attachments;

		attachments[0]		// color attachment
		    .setFormat          ( vk::Format(self->swapchain.getSurfaceFormat()->format) )
		    .setSamples         ( vk::SampleCountFlagBits::e1 )
		    .setLoadOp          ( vk::AttachmentLoadOp::eClear )
		    .setStoreOp         ( vk::AttachmentStoreOp::eStore )
		    .setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
		    .setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
		    .setInitialLayout   ( vk::ImageLayout::eUndefined )
		    .setFinalLayout     ( vk::ImageLayout::ePresentSrcKHR )
		    ;
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

		vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
		//vk::AttachmentReference depthReference{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

		vk::SubpassDescription subpassDescription;
		subpassDescription
		    .setPipelineBindPoint       ( vk::PipelineBindPoint::eGraphics )
		    .setInputAttachmentCount    ( 0 )
		    .setPInputAttachments       ( nullptr )
		    .setColorAttachmentCount    ( 1 )
		    .setPColorAttachments       ( &colorReference )
		    .setPResolveAttachments     ( nullptr )
		    .setPDepthStencilAttachment ( nullptr ) /* &depthReference */
		    .setPPreserveAttachments    ( nullptr )
		    .setPreserveAttachmentCount (0)
		    ;

		// Define a external dependency for subpass 0

		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0]
		    .setSrcSubpass      ( VK_SUBPASS_EXTERNAL ) // producer
		    .setDstSubpass      ( 0 )                   // consumer
		    .setSrcStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )  // we need this because the semaphore waits on colorAttachmentOutput
		    .setDstStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		    .setSrcAccessMask   ( vk::AccessFlagBits(0) ) // don't flush anything - nothing needs flushing.
		    .setDstAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		    .setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		    ;

		dependencies[1]
		    .setSrcSubpass      ( 0 )                                     // producer (last possible subpass == subpass 1)
		    .setDstSubpass      ( VK_SUBPASS_EXTERNAL )                   // consumer
		    .setSrcStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		    .setDstStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		    .setSrcAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite ) // this needs to be complete,
		    .setDstAccessMask   ( vk::AccessFlagBits::eMemoryRead )			  // before this can begin
		    .setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		    ;

		// Define 1 renderpass with 1 subpass

		vk::RenderPassCreateInfo renderPassCreateInfo;
		renderPassCreateInfo
		    .setAttachmentCount ( attachments.size() )
		    .setPAttachments    ( attachments.data() )
		    .setSubpassCount    ( 1 )
		    .setPSubpasses      ( &subpassDescription )
		    .setDependencyCount ( dependencies.size() )
		    .setPDependencies   ( dependencies.data() );

		self->debugRenderPass = self->vkDevice.createRenderPass(renderPassCreateInfo);
	}


	{
		vk::PipelineCacheCreateInfo pipelineCacheInfo;
		pipelineCacheInfo
		        .setFlags( vk::PipelineCacheCreateFlags() ) // "reserved for future use"
		        .setInitialDataSize( 0 )
		        .setPInitialData( nullptr )
		        ;

		self->debugPipelineCache = self->vkDevice.createPipelineCache(pipelineCacheInfo);

		static const std::vector<uint32_t> shaderCodeVert {
			// converted using: `glslc vertex_shader.vert fragment_shader.frag -c -mfmt=num`
#include "vertex_shader.vert.spv"
		};

		static const std::vector<uint32_t> shaderCodeFrag {
#include "fragment_shader.frag.spv"
		};

		self->debugVertexShaderModule   = self->vkDevice.createShaderModule({vk::ShaderModuleCreateFlags(),shaderCodeVert.size() * sizeof(uint32_t),shaderCodeVert.data()});
		self->debugFragmentShaderModule = self->vkDevice.createShaderModule({vk::ShaderModuleCreateFlags(),shaderCodeFrag.size() * sizeof(uint32_t),shaderCodeFrag.data()});

		std::array<vk::PipelineShaderStageCreateInfo,2> pipelineStages;
		pipelineStages[0]
		        .setFlags( vk::PipelineShaderStageCreateFlags() ) // must be 0 - "reserved for future use"
		        .setStage( vk::ShaderStageFlagBits::eVertex )
		        .setModule( self->debugVertexShaderModule )
		        .setPName( "main" )
		        .setPSpecializationInfo( nullptr )
		        ;
		pipelineStages[1]
		        .setFlags( vk::PipelineShaderStageCreateFlags() ) // must be 0 - "reserved for future use"
		        .setStage( vk::ShaderStageFlagBits::eFragment )
		        .setModule( self->debugFragmentShaderModule )
		        .setPName( "main" )
		        .setPSpecializationInfo( nullptr )
		        ;

		vk::PipelineVertexInputStateCreateInfo vertexInputStageInfo;
		vertexInputStageInfo
		    .setFlags( vk::PipelineVertexInputStateCreateFlags() )
		    .setVertexBindingDescriptionCount( 0 )
		    .setPVertexBindingDescriptions( nullptr )
		    .setVertexAttributeDescriptionCount( 0 )
		    .setPVertexAttributeDescriptions( nullptr )
		    ;

		// todo: get layout from shader

		vk::DescriptorSetLayoutCreateInfo setLayoutInfo;
		setLayoutInfo
		    .setFlags( vk::DescriptorSetLayoutCreateFlags())
		    .setBindingCount( 0 )
		    .setPBindings( nullptr )
		;

		self->debugDescriptorSetLayout = self->vkDevice.createDescriptorSetLayout(setLayoutInfo);

		vk::PipelineLayoutCreateInfo layoutCreateInfo;
		layoutCreateInfo
		        .setFlags( vk::PipelineLayoutCreateFlags() ) // "reserved for future use"
		        .setSetLayoutCount( 1 )
		        .setPSetLayouts( &self->debugDescriptorSetLayout )
		        .setPushConstantRangeCount( 0 )
		        .setPPushConstantRanges( nullptr )
		        ;


		self->debugPipelineLayout = self->vkDevice.createPipelineLayout(layoutCreateInfo);

		vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
		    inputAssemblyState
			    .setTopology( ::vk::PrimitiveTopology::eTriangleList )
			    .setPrimitiveRestartEnable( VK_FALSE )
			    ;

			vk::PipelineTessellationStateCreateInfo tessellationState;
			tessellationState
			    .setPatchControlPoints( 3 )
			    ;

			// viewport and scissor are tracked as dynamic states, so this object
			// will not get used.
			vk::PipelineViewportStateCreateInfo viewportState;
			viewportState
			    .setViewportCount( 1 )
			    .setPViewports( nullptr )
			    .setScissorCount( 1 )
			    .setPScissors( nullptr )
			    ;

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
			    .setLineWidth( 1.f )
			    ;

			vk::PipelineMultisampleStateCreateInfo multisampleState;
			multisampleState
			    .setRasterizationSamples( ::vk::SampleCountFlagBits::e1 )
			    .setSampleShadingEnable( VK_FALSE )
			    .setMinSampleShading( 0.f )
			    .setPSampleMask( nullptr )
			    .setAlphaToCoverageEnable( VK_FALSE )
			    .setAlphaToOneEnable( VK_FALSE )
			    ;

			vk::StencilOpState stencilOpState;
			stencilOpState
			    .setFailOp( ::vk::StencilOp::eKeep )
			    .setPassOp( ::vk::StencilOp::eKeep )
			    .setDepthFailOp( ::vk::StencilOp::eKeep )
			    .setCompareOp( ::vk::CompareOp::eNever )
			    .setCompareMask( 0 )
			    .setWriteMask( 0 )
			    .setReference( 0 )
			    ;

			vk::PipelineDepthStencilStateCreateInfo depthStencilState;
			depthStencilState
			    .setDepthTestEnable( VK_FALSE )
			    .setDepthWriteEnable( VK_FALSE)
			    .setDepthCompareOp( ::vk::CompareOp::eLessOrEqual )
			    .setDepthBoundsTestEnable( VK_FALSE )
			    .setStencilTestEnable( VK_FALSE )
			    .setFront( stencilOpState )
			    .setBack( stencilOpState )
			    .setMinDepthBounds( 0.f )
			    .setMaxDepthBounds( 0.f )
			    ;

			std::array<vk::PipelineColorBlendAttachmentState,1> blendAttachmentStates;
			blendAttachmentStates.fill( vk::PipelineColorBlendAttachmentState() );

			blendAttachmentStates[0]
			    .setBlendEnable( VK_TRUE )
			    .setColorBlendOp( ::vk::BlendOp::eAdd)
			    .setAlphaBlendOp( ::vk::BlendOp::eAdd)
			    .setSrcColorBlendFactor( ::vk::BlendFactor::eOne)              // eOne, because we require premultiplied alpha!
			    .setDstColorBlendFactor( ::vk::BlendFactor::eOneMinusSrcAlpha )
			    .setSrcAlphaBlendFactor( ::vk::BlendFactor::eOne)
			    .setDstAlphaBlendFactor( ::vk::BlendFactor::eZero )
			    .setColorWriteMask(
			        ::vk::ColorComponentFlagBits::eR |
			        ::vk::ColorComponentFlagBits::eG |
			        ::vk::ColorComponentFlagBits::eB |
			        ::vk::ColorComponentFlagBits::eA
			    )
			    ;

			vk::PipelineColorBlendStateCreateInfo colorBlendState;
			colorBlendState
			    .setLogicOpEnable( VK_FALSE )
			    .setLogicOp( ::vk::LogicOp::eClear )
			    .setAttachmentCount( blendAttachmentStates.size() )
			    .setPAttachments   ( blendAttachmentStates.data() )
			    .setBlendConstants( {{0.f, 0.f, 0.f, 0.f}} )
			    ;

			std::array<vk::DynamicState,2> dynamicStates = {{
			    ::vk::DynamicState::eScissor,
			    ::vk::DynamicState::eViewport,
			}};

			vk::PipelineDynamicStateCreateInfo dynamicState;
			dynamicState
			    .setDynamicStateCount( dynamicStates.size() )
			    .setPDynamicStates( dynamicStates.data() )
			    ;


		// setup debug pipeline
		vk::GraphicsPipelineCreateInfo gpi;
		gpi
		        .setFlags( vk::PipelineCreateFlagBits::eAllowDerivatives )
		        .setStageCount( uint32_t(pipelineStages.size()) )
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

	self->debugPipeline = self->vkDevice.createGraphicsPipeline(self->debugPipelineCache, gpi);


	}

}

// ----------------------------------------------------------------------

static void renderer_clear_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame  = self->frames[ frameIndex ];

	if (frame.state == FrameData::State::eCleared || frame.state==FrameData::State::eInitial){
		return;
	}

	// ----------| invariant: frame was not yet cleared

	auto &device = self->vkDevice;
	// + ensure frame fence has been reached
	if ( frame.state == FrameData::State::eDispatched ||
	     frame.state == FrameData::State::eFailedDispatch ||
	     frame.state == FrameData::State::eFailedClear ) {
		auto result = device.waitForFences( {frame.frameFence}, true, 100'000'000 );

		if (result != vk::Result::eSuccess){
			frame.state = FrameData::State::eFailedClear;
			return ;
		}
	}

	device.resetFences( {frame.frameFence} );

	for ( auto &fb : frame.debugFramebuffers ) {
		device.destroyFramebuffer( fb );
	}
	frame.debugFramebuffers.clear();

	device.freeCommandBuffers( frame.commandPool, frame.commandBuffers );
	frame.commandBuffers.clear();

	device.resetCommandPool( frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources );

	// NOTE: free transient gpu resources
	//       + transient memory
	frame.graphBuilder->reset();

	frame.state = FrameData::State::eCleared;
}

// ----------------------------------------------------------------------

static const FrameData::State& renderer_acquire_swapchain_image(le_renderer_o* self, size_t frameIndex){

	// ---------| invariant: There are frames to process.

	auto &frame = self->frames[ frameIndex ];

	frame.meta.time_acquire_frame_start = std::chrono::high_resolution_clock::now();

	if ( frame.state != FrameData::State::eRecorded ) {
		return frame.state;
	}

	// ----------| invariant: frame is either initial, or cleared.

	// TODO: update descriptor pool for this frame

	auto acquireSuccess = self->swapchain.acquireNextImage(frame.semaphorePresentComplete,frame.swapchainImageIndex);

	frame.meta.time_acquire_frame_end = std::chrono::high_resolution_clock::now();

	if ( acquireSuccess ) {
		frame.state = FrameData::State::eAcquired;

	} else {
		frame.state = FrameData::State::eFailedAcquire;
		// Failure most likely means that the swapchain was reset,
		// perhaps because of window resize.
		std::cout << "WARNING: Could not acquire frame." << std::endl;
		self->swapchainDirty = true;
	}

	return frame.state;
}

// ----------------------------------------------------------------------

static void renderer_record_frame(le_renderer_o* self, size_t frameIndex, le_render_module_o * module_){

	auto &frame = self->frames[ frameIndex ];

	if (frame.state != FrameData::State::eCleared && frame.state != FrameData::State::eInitial){
		return;
	}
	// ---------| invariant: Frame was previously acquired successfully.

	// record api-agnostic intermediate draw lists
	frame.meta.time_record_frame_start = std::chrono::high_resolution_clock::now();

	// TODO: implement record_frame
	// - resolve rendergraph: which passes do contribute?
	// - consolidate resources, synchronisation for resources
	//
	// For each render pass, call renderpass' render method, build intermediary command lists
	//

	le::RenderModule renderModule{module_};

	renderModule.buildGraph(*frame.graphBuilder);   // - build up dependencies for graph, create table of unique resources for graph
	renderModule.executeGraph(*frame.graphBuilder); // - this is where we execute the rendergraph, which should create an intermediate representation of the frame

	frame.meta.time_record_frame_end   = std::chrono::high_resolution_clock::now();
	std::cout << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_record_frame_end-frame.meta.time_record_frame_start).count() << "ms" << std::endl;

	frame.state = FrameData::State::eRecorded;
}

// ----------------------------------------------------------------------

static const FrameData::State& renderer_process_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame = self->frames[ frameIndex ];

	if (frame.state != FrameData::State::eAcquired){
		return frame.state;
	}
	// ---------| invariant: frame was previously recorded successfully

	frame.meta.time_process_frame_start = std::chrono::high_resolution_clock::now();

	// translate intermediate draw lists into vk command buffers, and sync primitives

	auto cmdBufs = self->vkDevice.allocateCommandBuffers( {frame.commandPool, vk::CommandBufferLevel::ePrimary, 1} );
	assert( cmdBufs.size() == 1 );

	// create frame buffer, based on swapchain and renderpass

	{
		std::array<vk::ImageView, 1> framebufferAttachments{
			{self->swapchain.getImageView( frame.swapchainImageIndex )}};

		vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo
		    .setFlags           ( {} )
		    .setRenderPass      ( self->debugRenderPass )
		    .setAttachmentCount ( uint32_t( framebufferAttachments.size() ) )
		    .setPAttachments    ( framebufferAttachments.data() )
		    .setWidth           ( self->swapchain.getImageWidth() )
		    .setHeight          ( self->swapchain.getImageHeight() )
		    .setLayers          ( 1 )
		    ;

		vk::Framebuffer fb = self->vkDevice.createFramebuffer( framebufferCreateInfo );
		frame.debugFramebuffers.emplace_back( std::move( fb ) );
	}

	std::array<vk::ClearValue, 1> clearValues{
		{vk::ClearColorValue( std::array<float, 4>{{0.f, 1.f, 0.0f, 1.f}} )}};

	auto &cmd = cmdBufs.front();

	cmd.begin( {::vk::CommandBufferUsageFlagBits::eOneTimeSubmit} );
	{
		vk::RenderPassBeginInfo renderPassBeginInfo;

		auto renderAreaWidth  = self->swapchain.getImageWidth();
		auto renderAreaHeight = self->swapchain.getImageHeight();

		renderPassBeginInfo
		    .setRenderPass      ( self->debugRenderPass )
		    .setFramebuffer     ( frame.debugFramebuffers.back() )
		    .setRenderArea      ( vk::Rect2D( {0, 0}, {renderAreaWidth, renderAreaHeight} ) )
		    .setClearValueCount ( uint32_t( clearValues.size() ) )
		    .setPClearValues    ( clearValues.data() )
		    ;

		cmd.beginRenderPass( renderPassBeginInfo, vk::SubpassContents::eInline );

		// TODO: bind pipeline
		// TODO: draw something
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,self->debugPipeline);
		cmd.setViewport(0,{vk::Viewport(0,0,renderAreaWidth,renderAreaHeight,0,1)});
		cmd.setScissor(0,{vk::Rect2D({0,0},{renderAreaWidth,renderAreaHeight})});
		cmd.draw(3,1,0,0);

		cmd.endRenderPass();
	}
	cmd.end();

	// place command buffer in frame store so that it can be submitted.
	for ( auto &&c : cmdBufs ) {
		frame.commandBuffers.emplace_back( c );
	}

	frame.meta.time_process_frame_end = std::chrono::high_resolution_clock::now();
	//std::cout << "process: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_process_frame_end-frame.meta.time_process_frame_start).count() << "ms" << std::endl;

	frame.state = FrameData::State::eProcessed;
	return frame.state;
}


// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o *self, size_t frameIndex) {


	auto &frame = self->frames[ frameIndex ];

	if (frame.state != FrameData::State::eProcessed){
		return;
	}

	// ---------| invariant: frame was successfully processed previously

	frame.meta.time_dispatch_frame_start = std::chrono::high_resolution_clock::now();

	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = {{::vk::PipelineStageFlagBits::eColorAttachmentOutput}};

	vk::SubmitInfo submitInfo;
	submitInfo
	    .setWaitSemaphoreCount   ( 1 )
	    .setPWaitSemaphores      ( &frame.semaphorePresentComplete )
	    .setPWaitDstStageMask    ( wait_dst_stage_mask.data() )
	    .setCommandBufferCount   ( uint32_t( frame.commandBuffers.size() ) )
	    .setPCommandBuffers      ( frame.commandBuffers.data() )
	    .setSignalSemaphoreCount ( 1 )
	    .setPSignalSemaphores    ( &frame.semaphoreRenderComplete )
	    ;

	auto queue = vk::Queue{self->leDevice.getDefaultGraphicsQueue()};

	queue.submit( {submitInfo}, frame.frameFence );

	bool presentSuccessful = self->swapchain.present( self->leDevice.getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex );

	frame.meta.time_dispatch_frame_end = std::chrono::high_resolution_clock::now();

	if (presentSuccessful){
		frame.state = FrameData::State::eDispatched;
	} else {

		std::cout << "WARNING: Could not present frame." << std::endl;

		// Present was not successful -
		//
		// This most likely happened because the window surface has been resized.
		// We therefore attempt to reset the swapchain.

		frame.state = FrameData::State::eFailedDispatch;

		self->swapchainDirty = true;
	}

}

// ----------------------------------------------------------------------

static void renderer_update( le_renderer_o *self, le_render_module_o * module_ ) {

	const auto &index     = self->currentFrameNumber;
	const auto &numFrames = self->numSwapchainImages;

	// TODO: think more about interleaving - ideally, each one of these stages
	// should be able to be executed in its own thread.
	//
	// At the moment, this is not possible, as acquisition might acquire more images
	// than available if there are more threads than swapchain images.

	renderer_clear_frame            ( self, ( index + 0 ) % numFrames );

	// generate an intermediary, api-agnostic, representation of the frame
	renderer_record_frame           ( self, ( index + 0 ) % numFrames, module_ );

	renderer_acquire_swapchain_image( self, ( index + 1 ) % numFrames );

	// generate api commands for the frame
	renderer_process_frame          ( self, ( index + 1 ) % numFrames );

	renderer_dispatch_frame         ( self, ( index + 1 ) % numFrames );


	if (self->swapchainDirty){
		// we must dispatch, then clear all previous dispatchable frames,
		// before recreating swapchain. This is because this frame
		// was processed against the vkImage object from the previous
		// swapchain.

		// TODO: check if you could just signal these fences so that the
		// leftover frames must not be dispatched.

		for ( size_t i = 0; i != self->frames.size(); ++i ) {
			if ( self->frames[ i ].state == FrameData::State::eProcessed ) {
				renderer_dispatch_frame( self, i );
				renderer_clear_frame( self, i );
			}
		}

		self->swapchain.reset();
		self->swapchainDirty = false;
	}

	++ self->currentFrameNumber;
}

// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o *self ) {

	const auto &lastIndex = self->currentFrameNumber;
	for ( size_t i = 0; i != self->frames.size(); ++i ) {
		renderer_clear_frame( self, ( lastIndex + i) % self->frames.size() );
	}

	self->vkDevice.destroyRenderPass(self->debugRenderPass);

	if (self->debugDescriptorSetLayout){
		self->vkDevice.destroyDescriptorSetLayout(self->debugDescriptorSetLayout);
	}

	if (self->debugPipelineLayout){
		self->vkDevice.destroyPipelineLayout(self->debugPipelineLayout);
	}

	if (self->debugPipeline){
		self->vkDevice.destroyPipeline(self->debugPipeline);
	}

	if (self->debugPipelineCache){
		self->vkDevice.destroyPipelineCache(self->debugPipelineCache);
	}

	if (self->debugVertexShaderModule){
		self->vkDevice.destroyShaderModule(self->debugVertexShaderModule);
	}

	if (self->debugFragmentShaderModule){
		self->vkDevice.destroyShaderModule(self->debugFragmentShaderModule);
	}

	for ( auto &frameData : self->frames ) {
		self->vkDevice.destroyFence( frameData.frameFence );
		self->vkDevice.destroySemaphore( frameData.semaphorePresentComplete );
		self->vkDevice.destroySemaphore( frameData.semaphoreRenderComplete );
		self->vkDevice.destroyCommandPool( frameData.commandPool );
	}

	self->frames.clear();

	delete self;
}


// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_renderer_api( void *api_ ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create  = renderer_create;
	le_renderer_i.destroy = renderer_destroy;
	le_renderer_i.setup   = renderer_setup;
	le_renderer_i.update  = renderer_update;

	Registry::loadLibraryPersistently( "libvulkan.so" );

	// register sub-components of this api
	register_le_rendergraph_api(api_);
	register_le_renderpass_api(api_);
	register_le_command_buffer_encoder_api(api_);
}
