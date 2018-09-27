#include "le_pipeline_builder.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/spooky/SpookyV2.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include "le_renderer/le_renderer.h" // for le_vertex_input_attribute_description le_vertex_input_binding_description

#include <array>
#include <vector>
#include <mutex>
#include <shared_mutex>

constexpr uint8_t MAX_VULKAN_COLOR_ATTACHMENTS = 16; // maximum number of color attachments to a renderpass

/*

  These are the fields that must be set to create a Pipeline:

		.setStageCount( uint32_t( pipelineStages.size() ) ) // held outside
		.setPStages( pipelineStages.data() )				// held outside
		.setPVertexInputState( &vertexInputStageInfo )		// held outside

		.setPInputAssemblyState( &inputAssemblyState )

		.setPTessellationState( nullptr )

		.setPViewportState( &viewportState )

		.setPRasterizationState( &pso->rasterizationInfo )

		.setPMultisampleState( &multisampleState )

		.setPDepthStencilState( &depthStencilState )

		.setPColorBlendState( &colorBlendState ) // count and number ot attachments, plus blend constants

		.setPDynamicState( &dynamicState ) // count and pointer to dynamic states

		.setLayout( pipelineLayout )
		.setRenderPass( pass.renderPass ) // must be a valid renderpass.
		.setSubpass( subpass )
		.setBasePipelineHandle( nullptr )
		.setBasePipelineIndex( 0 ) // -1 signals not to use a base pipeline index


*/

struct le_pipeline_builder_data {

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{};
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	vk::PipelineTessellationStateCreateInfo  tessellationState{};
	vk::PipelineMultisampleStateCreateInfo   multisampleState{};
	vk::PipelineDepthStencilStateCreateInfo  depthStencilState{};

	std::array<vk::PipelineColorBlendAttachmentState, MAX_VULKAN_COLOR_ATTACHMENTS> blendAttachmentStates{};
};

struct le_pipeline_builder_o {

	le_pipeline_builder_data data{};

	struct le_shader_module_o *vertexShader   = nullptr; // refers opaquely to a shader module (or not)
	struct le_shader_module_o *fragmentShader = nullptr; // refers opaquely to a shader module (or not)

	std::vector<le_vertex_input_attribute_description> explicitVertexAttributeDescriptions; // only used if explicitly told to, otherwise use from vertex shader reflection
	std::vector<le_vertex_input_binding_description>   explicitVertexBindingDescriptions;   // only used if explicitly told to, otherwise use from vertex shader reflection

	bool useExplicitVertexInputDescriptions = false;
};

// we need to store pipelineInfo objects in here- protected by a mutex.
struct pipeline_data_cache_o {
	std::mutex                            mtx;       // mutex protecting read/write access to this struct (we must protect read unfortunately)
	std::vector<uint64_t>                 data_hash; // hash for each info, indices match info
	std::vector<le_pipeline_builder_data> data;      // data where info may point into
};

// ----------------------------------------------------------------------

static le_pipeline_builder_o *le_pipeline_builder_create() {
	auto self = new le_pipeline_builder_o();

	// set default values

	self->data.inputAssemblyState
	    .setTopology( ::vk::PrimitiveTopology::eTriangleList )
	    .setPrimitiveRestartEnable( VK_FALSE );

	self->data.tessellationState
	    .setPatchControlPoints( 3 );

	// Viewport and scissor are tracked as dynamic states,
	// so this object will not be used,
	// but we need to give it some default values to match requirements.
	//

	self->data.rasterizationInfo
	    .setDepthClampEnable( VK_FALSE )
	    .setRasterizerDiscardEnable( VK_FALSE )
	    .setPolygonMode( ::vk::PolygonMode::eFill )
	    .setCullMode( ::vk::CullModeFlagBits::eNone )
	    .setFrontFace( ::vk::FrontFace::eCounterClockwise )
	    .setDepthBiasEnable( VK_FALSE )
	    .setDepthBiasConstantFactor( 0.f )
	    .setDepthBiasClamp( 0.f )
	    .setDepthBiasSlopeFactor( 1.f )
	    .setLineWidth( 1.f );

	self->data.multisampleState
	    .setRasterizationSamples( ::vk::SampleCountFlagBits::e1 )
	    .setSampleShadingEnable( VK_FALSE )
	    .setMinSampleShading( 0.f )
	    .setPSampleMask( nullptr )
	    .setAlphaToCoverageEnable( VK_FALSE )
	    .setAlphaToOneEnable( VK_FALSE );

	vk::StencilOpState stencilOpState{};
	stencilOpState
	    .setFailOp( ::vk::StencilOp::eKeep )
	    .setPassOp( ::vk::StencilOp::eKeep )
	    .setDepthFailOp( ::vk::StencilOp::eKeep )
	    .setCompareOp( ::vk::CompareOp::eNever )
	    .setCompareMask( 0 )
	    .setWriteMask( 0 )
	    .setReference( 0 );

	self->data.depthStencilState
	    .setDepthTestEnable( VK_TRUE )
	    .setDepthWriteEnable( VK_TRUE )
	    .setDepthCompareOp( ::vk::CompareOp::eLessOrEqual )
	    .setDepthBoundsTestEnable( VK_FALSE )
	    .setStencilTestEnable( VK_FALSE )
	    .setFront( stencilOpState )
	    .setBack( stencilOpState )
	    .setMinDepthBounds( 0.f )
	    .setMaxDepthBounds( 0.f );

	// Default values for color blend state
	for ( auto &blendAttachmentState : self->data.blendAttachmentStates ) {
		blendAttachmentState
		    .setBlendEnable( VK_TRUE )
		    .setColorBlendOp( ::vk::BlendOp::eAdd )
		    .setAlphaBlendOp( ::vk::BlendOp::eAdd )
		    .setSrcColorBlendFactor( ::vk::BlendFactor::eSrcAlpha )
		    .setDstColorBlendFactor( ::vk::BlendFactor::eOneMinusSrcAlpha )
		    .setSrcAlphaBlendFactor( ::vk::BlendFactor::eOne )
		    .setDstAlphaBlendFactor( ::vk::BlendFactor::eZero )
		    .setColorWriteMask(
		        ::vk::ColorComponentFlagBits::eR |
		        ::vk::ColorComponentFlagBits::eG |
		        ::vk::ColorComponentFlagBits::eB |
		        ::vk::ColorComponentFlagBits::eA );
	}

	return self;
}

// ----------------------------------------------------------------------

static void le_pipeline_builder_set_vertex_input_attribute_descriptions( le_pipeline_builder_o *self, le_vertex_input_attribute_description *p_input_attribute_descriptions, size_t count ) {
	self->explicitVertexAttributeDescriptions =
	    {p_input_attribute_descriptions,
	     p_input_attribute_descriptions + count};
}

// ----------------------------------------------------------------------

static void le_pipeline_builder_set_vertex_input_binding_descriptions( le_pipeline_builder_o *self, le_vertex_input_binding_description *p_input_binding_descriptions, size_t count ) {
	self->explicitVertexBindingDescriptions =
	    {p_input_binding_descriptions,
	     p_input_binding_descriptions + count};
}

// ----------------------------------------------------------------------

static void le_pipeline_builder_destroy( le_pipeline_builder_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

// Calculate pipeline info hash, and add pipeline info to shared store if not yet seen.
// Return pipeline hash
static uint64_t le_pipeline_builder_build( le_pipeline_builder_o *self ) {
	uint64_t hash_value{}; // hash will get updated based on values from info_objects

	constexpr size_t
	    hash_msg_size = sizeof( le_pipeline_builder_data );

	hash_value = SpookyHash::Hash64( &self->data, hash_msg_size, 0 );

	// Check if this hash_value is already in the cold store.
	// - if not, add info to cold store, and index it with hash_value
	// - if yes, just return the hash value.

	// The hash value is what we use to refer to this pso
	// from within the encoders, for example.
	//
	// it is not used to refer to a pipeline object directly, since
	// the pipeline object needs to take into account the renderpass
	// and subpass.

	return hash_value;
}

// ----------------------------------------------------------------------

static void le_pipeline_builder_set_vertex_shader( le_pipeline_builder_o *self, struct le_shader_module_o *vertexShader ) {
	self->vertexShader = vertexShader;
}

static void le_pipeline_builder_set_fragment_shader( le_pipeline_builder_o *self, struct le_shader_module_o *fragmentShader ) {
	self->fragmentShader = fragmentShader;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_pipeline_builder_api( void *api ) {
	auto &i = static_cast<le_pipeline_builder_api *>( api )->le_pipeline_builder_i;

	i.create                                  = le_pipeline_builder_create;
	i.destroy                                 = le_pipeline_builder_destroy;
	i.build                                   = le_pipeline_builder_build;
	i.set_fragment_shader                     = le_pipeline_builder_set_fragment_shader;
	i.set_vertex_shader                       = le_pipeline_builder_set_vertex_shader;
	i.set_vertex_input_attribute_descriptions = le_pipeline_builder_set_vertex_input_attribute_descriptions;
	i.set_vertex_input_binding_descriptions   = le_pipeline_builder_set_vertex_input_binding_descriptions;
}
