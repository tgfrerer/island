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

  where do we store pipeline state objects? the best place is probably the backend.
  the backend then is also responsible for synchronising access.

  When a pipeline state object is built, the hash for the pipeline state object is calculated.
  - if this hash already exists in the cache, we return the hash
  - if this hash does not exist in the cache, we must store the pipeline object in the cache,
	then return the hash.

  Where does the cache live? It must be accessible to the backend, since the backend compiles pipelines
  based on the pipeline state objects.

  A pipeline builder therefore must be created from a backend, so that it can access the backend, and update
  the pipeline state object cache if necessary.

  Thread safety:

  - multiple renderpasses may write to or read from pso cache (read mostly happens to hash_ids)
	- access here is looking whether pso with hash is already in cache
	- if not, write to the cache
  - multiple frames may access pso cache: when processing commandbuffers
	- lookup pso hashes for index
	- read from pso state based on found hash index
  - Write access is therefore only if there is a new pso and it must be added to the cache.

  - we need to protect access to pso cache so that its thread safe
	- consider using a shared_mutex - either: multiple readers - or one single writer

	a pipeline builder *must* be associated with a backend, so that we can
	write pso data back to the backend's cache.

	does this mean that the pipeline builder is an object inside the backend api?
	it is strongly suggested.

*/

struct le_graphics_pipeline_builder_data {

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{};
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	vk::PipelineTessellationStateCreateInfo  tessellationState{};
	vk::PipelineMultisampleStateCreateInfo   multisampleState{};
	vk::PipelineDepthStencilStateCreateInfo  depthStencilState{};

	std::array<vk::PipelineColorBlendAttachmentState, MAX_VULKAN_COLOR_ATTACHMENTS> blendAttachmentStates{};
};

// contains everything (except renderpass/subpass) needed to create a pipeline in the backend
struct le_graphics_pipeline_builder_o {

	le_graphics_pipeline_builder_data data{};

	struct le_shader_module_o *vertexShader   = nullptr; // refers opaquely to a shader module (or not)
	struct le_shader_module_o *fragmentShader = nullptr; // refers opaquely to a shader module (or not)

	std::vector<VkVertexInputAttributeDescription> explicitVertexAttributeDescriptions;    // only used if contains values, otherwise use from vertex shader reflection
	std::vector<VkVertexInputBindingDescription>   explicitVertexInputBindingDescriptions; // only used if contains values, otherwise use from vertex shader reflection
};

// ----------------------------------------------------------------------

static le_graphics_pipeline_builder_o *le_graphics_pipeline_builder_create() {
	auto self = new le_graphics_pipeline_builder_o();

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

static void le_graphics_pipeline_builder_set_vertex_input_attribute_descriptions( le_graphics_pipeline_builder_o *self, VkVertexInputAttributeDescription *p_input_attribute_descriptions, size_t count ) {
	self->explicitVertexAttributeDescriptions =
	    {p_input_attribute_descriptions,
	     p_input_attribute_descriptions + count};
}

// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_set_vertex_input_binding_descriptions( le_graphics_pipeline_builder_o *self, VkVertexInputBindingDescription *p_input_binding_descriptions, size_t count ) {
	self->explicitVertexInputBindingDescriptions =
	    {p_input_binding_descriptions,
	     p_input_binding_descriptions + count};
}

// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_destroy( le_graphics_pipeline_builder_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

// Calculate pipeline info hash, and add pipeline info to shared store if not yet seen.
// Return pipeline hash
static uint64_t le_graphics_pipeline_builder_build( le_graphics_pipeline_builder_o *self ) {
	uint64_t hash_value{}; // hash will get updated based on values from info_objects

	constexpr size_t
	    hash_msg_size = sizeof( le_graphics_pipeline_builder_data );

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

static void le_graphics_pipeline_builder_set_vertex_shader( le_graphics_pipeline_builder_o *self, struct le_shader_module_o *vertexShader ) {
	self->vertexShader = vertexShader;
}

static void le_graphics_pipeline_builder_set_fragment_shader( le_graphics_pipeline_builder_o *self, struct le_shader_module_o *fragmentShader ) {
	self->fragmentShader = fragmentShader;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_graphics_pipeline_builder_api( void *api ) {
	auto &i = static_cast<le_graphics_pipeline_builder_api *>( api )->le_graphics_pipeline_builder_i;

	i.create                                  = le_graphics_pipeline_builder_create;
	i.destroy                                 = le_graphics_pipeline_builder_destroy;
	i.build                                   = le_graphics_pipeline_builder_build;
	i.set_fragment_shader                     = le_graphics_pipeline_builder_set_fragment_shader;
	i.set_vertex_shader                       = le_graphics_pipeline_builder_set_vertex_shader;
	i.set_vertex_input_attribute_descriptions = le_graphics_pipeline_builder_set_vertex_input_attribute_descriptions;
	i.set_vertex_input_binding_descriptions   = le_graphics_pipeline_builder_set_vertex_input_binding_descriptions;
}
