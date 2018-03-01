#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h"
#include "le_backend_vk/private/le_device_vk.h"
#include "le_backend_vk/private/le_instance_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"

#include "pal_window/pal_window.h"

#include <vector>
#include <vulkan/vulkan.hpp>


// herein goes all data which is associated with the current frame
// backend keeps track of multiple frames.
struct BackendFrameData {
	vk::Fence                      frameFence               = nullptr;
	vk::Semaphore                  semaphoreRenderComplete  = nullptr;
	vk::Semaphore                  semaphorePresentComplete = nullptr;
	vk::CommandPool                commandPool              = nullptr;
	uint32_t                       swapchainImageIndex      = uint32_t( ~0 );
	uint32_t                       padding = 0; // NOTICE: remove if needed.
	std::vector<vk::CommandBuffer> commandBuffers;
	std::vector<vk::Framebuffer>   debugFramebuffers;

};

// backend data object
struct le_backend_o {

	le_backend_vk_settings_t settings;

	std::unique_ptr<le::Instance> instance;
	std::unique_ptr<le::Device>	  device;

	std::unique_ptr<pal::Window> window; // non-owning
	std::unique_ptr<le::Swapchain> swapchain;

	std::vector<BackendFrameData> mFrames;

	vk::RenderPass    debugRenderPass    = nullptr;
	vk::Pipeline      debugPipeline      = nullptr;
	vk::PipelineCache debugPipelineCache = nullptr;
	vk::ShaderModule  debugVertexShaderModule = nullptr;
	vk::ShaderModule  debugFragmentShaderModule = nullptr;
	vk::PipelineLayout debugPipelineLayout = nullptr;
	vk::DescriptorSetLayout debugDescriptorSetLayout = nullptr;

};

static le_backend_o* backend_create(le_backend_vk_settings_t* settings){
	auto self = new le_backend_o; // todo: leDevice must have been introduced here...

	self->settings = *settings;

	self->instance = std::make_unique<le::Instance>(self->settings.requestedExtensions, self->settings.numRequestedExtensions);
	self->device = std::make_unique<le::Device>(*self->instance);

	return self;
}

static void backend_destroy(le_backend_o* self){

	vk::Device device = self->device->getVkDevice();

	device.destroyRenderPass(self->debugRenderPass);

	if (self->debugDescriptorSetLayout){
		device.destroyDescriptorSetLayout(self->debugDescriptorSetLayout);
	}

	if (self->debugPipelineLayout){
		device.destroyPipelineLayout(self->debugPipelineLayout);
	}

	if (self->debugPipeline){
		device.destroyPipeline(self->debugPipeline);
	}

	if (self->debugPipelineCache){
		device.destroyPipelineCache(self->debugPipelineCache);
	}

	if (self->debugVertexShaderModule){
		device.destroyShaderModule(self->debugVertexShaderModule);
	}

	if (self->debugFragmentShaderModule){
		device.destroyShaderModule(self->debugFragmentShaderModule);
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

static bool backend_create_window_surface(le_backend_o* self, pal_window_o* window_){
	self->window = std::make_unique<pal::Window>(window_);
	assert (self->instance);
	assert (self->instance->getVkInstance());
	bool success = self->window->createSurface(self->instance->getVkInstance());
	return success;
}

static void backend_create_swapchain(le_backend_o* self, le_swapchain_vk_settings_o* swapchainSettings_){

	assert (self->window);

	le_swapchain_vk_settings_o tmpSwapchainSettings;

	tmpSwapchainSettings.imagecount_hint = 3;
	tmpSwapchainSettings.presentmode_hint = le::Swapchain::Presentmode::eFifo;
	tmpSwapchainSettings.width_hint = self->window->getSurfaceWidth();
	tmpSwapchainSettings.height_hint = self->window->getSurfaceHeight();
	tmpSwapchainSettings.vk_device = self->device->getVkDevice();
	tmpSwapchainSettings.vk_physical_device = self->device->getVkPhysicalDevice();
	tmpSwapchainSettings.vk_surface = self->window->getVkSurfaceKHR();
	tmpSwapchainSettings.vk_graphics_queue_family_index = self->device->getDefaultGraphicsQueueFamilyIndex();

	self->swapchain = std::make_unique<le::Swapchain>(&tmpSwapchainSettings);
}

static size_t backend_get_num_swapchain_images(le_backend_o* self){
	assert (self->swapchain);
	return self->swapchain->getImagesCount();
}

void backend_reset_swapchain(le_backend_o* self){
	self->swapchain->reset();
}

static void backend_setup(le_backend_o* self){

	auto frameCount = backend_get_num_swapchain_images(self);

	self->mFrames.reserve(frameCount);

	vk::Device device = self->device->getVkDevice();

	assert(device); // device must come from somewhere! it must have been introduced to backend before, or backend must create device used by everyone else...

	for (size_t i=0; i!= frameCount; ++i){
		auto frameData = BackendFrameData();

		frameData.frameFence               = device.createFence( {} ); // fence starts out as "signalled"
		frameData.semaphorePresentComplete = device.createSemaphore( {} );
		frameData.semaphoreRenderComplete  = device.createSemaphore( {} );
		frameData.commandPool              = device.createCommandPool( {vk::CommandPoolCreateFlagBits::eTransient, self->device->getDefaultGraphicsQueueFamilyIndex()} );

		self->mFrames.emplace_back(std::move(frameData));
	}


	// stand-in: create default renderpass.

	{
		std::array<vk::AttachmentDescription, 1> attachments;

		attachments[0]		// color attachment
		    .setFormat          ( vk::Format(self->swapchain->getSurfaceFormat()->format) )
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

		self->debugRenderPass = device.createRenderPass(renderPassCreateInfo);
	}

	// stand-in: create default pipeline

	{
		    vk::PipelineCacheCreateInfo pipelineCacheInfo;
			pipelineCacheInfo
			        .setFlags( vk::PipelineCacheCreateFlags() ) // "reserved for future use"
			        .setInitialDataSize( 0 )
			        .setPInitialData( nullptr )
			        ;

			self->debugPipelineCache = device.createPipelineCache(pipelineCacheInfo);

			static const std::vector<uint32_t> shaderCodeVert {
				// converted using: `glslc vertex_shader.vert fragment_shader.frag -c -mfmt=num`
    #include "vertex_shader.vert.spv"
			};

			static const std::vector<uint32_t> shaderCodeFrag {
    #include "fragment_shader.frag.spv"
			};

			self->debugVertexShaderModule   = device.createShaderModule({vk::ShaderModuleCreateFlags(),shaderCodeVert.size() * sizeof(uint32_t),shaderCodeVert.data()});
			self->debugFragmentShaderModule = device.createShaderModule({vk::ShaderModuleCreateFlags(),shaderCodeFrag.size() * sizeof(uint32_t),shaderCodeFrag.data()});

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

			self->debugDescriptorSetLayout = device.createDescriptorSetLayout(setLayoutInfo);

			vk::PipelineLayoutCreateInfo layoutCreateInfo;
			layoutCreateInfo
			        .setFlags( vk::PipelineLayoutCreateFlags() ) // "reserved for future use"
			        .setSetLayoutCount( 1 )
			        .setPSetLayouts( &self->debugDescriptorSetLayout )
			        .setPushConstantRangeCount( 0 )
			        .setPPushConstantRanges( nullptr )
			        ;


			self->debugPipelineLayout = device.createPipelineLayout(layoutCreateInfo);

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

		self->debugPipeline = device.createGraphicsPipeline(self->debugPipelineCache, gpi);


	    }
}

// ----------------------------------------------------------------------

static bool backend_clear_frame(le_backend_o* self, size_t frameIndex){

	auto &frame  = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	auto result = device.waitForFences( {frame.frameFence}, true, 100'000'000 );
	if (result != vk::Result::eSuccess){
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

static bool backend_acquire_swapchain_image(le_backend_o* self, size_t frameIndex){
	auto &frame = self->mFrames[ frameIndex ];
	return self->swapchain->acquireNextImage(frame.semaphorePresentComplete,frame.swapchainImageIndex);
};

// ----------------------------------------------------------------------

static void backend_process_frame(le_backend_o*self, size_t frameIndex /* renderGraph */){

	auto &frame = self->mFrames[ frameIndex ];
	vk::Device device = self->device->getVkDevice();

	auto cmdBufs = device.allocateCommandBuffers( {frame.commandPool, vk::CommandBufferLevel::ePrimary, 1} );
	assert( cmdBufs.size() == 1 );

	// create frame buffer, based on swapchain and renderpass

	{
		std::array<vk::ImageView, 1> framebufferAttachments{
			{self->swapchain->getImageView( frame.swapchainImageIndex )}};

		vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo
		    .setFlags           ( {} )
		    .setRenderPass      ( self->debugRenderPass )
		    .setAttachmentCount ( uint32_t( framebufferAttachments.size() ) )
		    .setPAttachments    ( framebufferAttachments.data() )
		    .setWidth           ( self->swapchain->getImageWidth() )
		    .setHeight          ( self->swapchain->getImageHeight() )
		    .setLayers          ( 1 )
		    ;

		vk::Framebuffer fb = device.createFramebuffer( framebufferCreateInfo );
		frame.debugFramebuffers.emplace_back( std::move( fb ) );
	}

	std::array<vk::ClearValue, 1> clearValues{
		{vk::ClearColorValue( std::array<float, 4>{{0.f, 0.f, 0.0f, 1.f}} )}};

	auto &cmd = cmdBufs.front();

	cmd.begin( {::vk::CommandBufferUsageFlagBits::eOneTimeSubmit} );
	{
		vk::RenderPassBeginInfo renderPassBeginInfo;

		auto renderAreaWidth  = self->swapchain->getImageWidth();
		auto renderAreaHeight = self->swapchain->getImageHeight();

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
		cmd.setViewport(0,{vk::Viewport(200,0,renderAreaWidth-200,renderAreaHeight,0,1)});
		cmd.setScissor(0,{vk::Rect2D({0,0},{renderAreaWidth,renderAreaHeight})});
		cmd.draw(3,1,0,0);


		cmd.endRenderPass();
	}
	cmd.end();

	// place command buffer in frame store so that it can be submitted.
	for ( auto &&c : cmdBufs ) {
		frame.commandBuffers.emplace_back( c );
	}
}

// ----------------------------------------------------------------------

static bool backend_dispatch_frame(le_backend_o* self, size_t frameIndex){


	auto &frame = self->mFrames[ frameIndex ];

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

	auto queue = vk::Queue{self->device->getDefaultGraphicsQueue()};

	queue.submit( {submitInfo}, frame.frameFence );

	bool presentSuccessful = self->swapchain->present( self->device->getDefaultGraphicsQueue(), frame.semaphoreRenderComplete, &frame.swapchainImageIndex );

	return presentSuccessful;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_backend_vk_api( void *api_ ) {
	auto  le_backend_vk_api_i  = static_cast<le_backend_vk_api *>( api_ );
	auto &vk_backend_i = le_backend_vk_api_i->vk_backend_i;

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

	// register/update submodules inside this plugin

	register_le_device_vk_api( api_ );
	register_le_instance_vk_api( api_ );

	auto &le_instance_vk_i = le_backend_vk_api_i->vk_instance_i;

	if ( le_backend_vk_api_i->cUniqueInstance != nullptr ) {
		le_instance_vk_i.post_reload_hook( le_backend_vk_api_i->cUniqueInstance );
	}

	Registry::loadLibraryPersistently( "libvulkan.so" );
}
