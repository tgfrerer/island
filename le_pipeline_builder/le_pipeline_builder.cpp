#include "le_pipeline_builder.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/spooky/SpookyV2.h"

#include "le_renderer/le_renderer.h" // for le_vertex_input_attribute_description le_vertex_input_binding_description

#include "le_backend_vk/le_backend_vk.h" // for access to pipeline state object cache
#include "le_backend_vk/le_backend_types_internal.h"

#include <array>
#include <vector>
#include <mutex>

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

static constexpr inline vk::PrimitiveTopology le_to_vk( const le::PrimitiveTopology &lhs ) noexcept {
	return vk::PrimitiveTopology( lhs );
}

// contains everything (except renderpass/subpass) needed to create a pipeline in the backend
struct le_graphics_pipeline_builder_o {
	graphics_pipeline_state_o *obj           = nullptr;
	le_pipeline_manager_o *    pipelineCache = nullptr;
};

// ----------------------------------------------------------------------

static le_graphics_pipeline_builder_o *le_graphics_pipeline_builder_create( le_pipeline_manager_o *pipelineCache ) {
	auto self = new le_graphics_pipeline_builder_o();

	self->pipelineCache = pipelineCache;
	self->obj           = new graphics_pipeline_state_o();
	// set default values

	self->obj->data.inputAssemblyState
	    .setTopology( ::vk::PrimitiveTopology::eTriangleList )
	    .setPrimitiveRestartEnable( VK_FALSE );

	self->obj->data.tessellationState
	    .setPatchControlPoints( 3 );

	// Viewport and scissor are tracked as dynamic states,
	// so this object will not be used,
	// but we need to give it some default values to match requirements.
	//

	self->obj->data.rasterizationInfo
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

	self->obj->data.multisampleState
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

	self->obj->data.depthStencilState
	    .setDepthTestEnable( VK_TRUE )
	    .setDepthWriteEnable( VK_TRUE )
	    .setDepthCompareOp( ::vk::CompareOp::eLessOrEqual )
	    .setDepthBoundsTestEnable( VK_FALSE )
	    .setStencilTestEnable( VK_FALSE )
	    .setFront( stencilOpState )
	    .setBack( stencilOpState )
	    .setMinDepthBounds( 0.f )
	    .setMaxDepthBounds( 0.f );

	// Default values for color blend state: premultiplied alpha
	for ( auto &blendAttachmentState : self->obj->data.blendAttachmentStates ) {
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

static void le_graphics_pipeline_builder_set_vertex_input_attribute_descriptions( le_graphics_pipeline_builder_o *self, le_vertex_input_attribute_description *p_input_attribute_descriptions, size_t count ) {
	self->obj->explicitVertexAttributeDescriptions =
	    {p_input_attribute_descriptions,
	     p_input_attribute_descriptions + count};
}

// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_set_vertex_input_binding_descriptions( le_graphics_pipeline_builder_o *self, le_vertex_input_binding_description *p_input_binding_descriptions, size_t count ) {
	self->obj->explicitVertexInputBindingDescriptions =
	    {p_input_binding_descriptions,
	     p_input_binding_descriptions + count};
}

// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_set_rasterization_info( le_graphics_pipeline_builder_o *self, const VkPipelineRasterizationStateCreateInfo &rasterizationInfo ) {
	self->obj->data.rasterizationInfo = rasterizationInfo;
};

static void le_graphics_pipeline_builder_set_input_assembly_info( le_graphics_pipeline_builder_o *self, const VkPipelineInputAssemblyStateCreateInfo &inputAssemblyInfo ) {
	self->obj->data.inputAssemblyState = inputAssemblyInfo;
}

static void le_graphics_pipeline_builder_set_tessellation_info( le_graphics_pipeline_builder_o *self, const VkPipelineTessellationStateCreateInfo &tessellationInfo ) {
	self->obj->data.tessellationState = tessellationInfo;
}

static void le_graphics_pipeline_builder_set_multisample_info( le_graphics_pipeline_builder_o *self, const VkPipelineMultisampleStateCreateInfo &multisampleInfo ) {
	self->obj->data.multisampleState = multisampleInfo;
}

static void le_graphics_pipeline_builder_set_depth_stencil_info( le_graphics_pipeline_builder_o *self, const VkPipelineDepthStencilStateCreateInfo &depthStencilInfo ) {
	self->obj->data.depthStencilState = depthStencilInfo;
}
// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_destroy( le_graphics_pipeline_builder_o *self ) {
	if ( self->obj ) {
		delete self->obj;
	}
	delete self;
}

// ----------------------------------------------------------------------

// Calculate pipeline info hash, and add pipeline info to shared store if not yet seen.
// Return pipeline hash
static uint64_t le_graphics_pipeline_builder_build( le_graphics_pipeline_builder_o *self ) {

	uint64_t hash_value{}; // We declare this as the first element, because it will get returned
	                       // at the end of the method and we're hoping for copy-elision.

	constexpr size_t hash_msg_size = sizeof( le_graphics_pipeline_builder_data );

	hash_value = SpookyHash::Hash64( &self->obj->data, hash_msg_size, 0 );

	{
		// Calculate a meta-hash over shader stage hash entries so that we can
		// detect if a shader component has changed
		//
		// Rather than a std::vector, we use a plain-c array to collect hash entries
		// for all stages, because we don't want to allocate anything on the heap,
		// and local fixed-size c-arrays are cheap.

		constexpr size_t maxShaderStages = 8;                 // we assume a maximum number of shader entries
		uint64_t         stageHashEntries[ maxShaderStages ]; // array of stage hashes for further hashing
		uint64_t         stageHashEntriesUsed = 0;            // number of used shader stage hash entries

		for ( auto const &module : self->obj->shaderStages ) {
			using namespace le_backend_vk;
			stageHashEntries[ stageHashEntriesUsed++ ] = le_shader_module_i.get_hash( module );
			assert( stageHashEntriesUsed <= maxShaderStages ); // We're gonna need a bigger boat.
		}

		// Mix in the meta-hash over shader stages with the previous hash over pipeline state
		// which gives the complete hash representing a pipeline state object.

		hash_value = SpookyHash::Hash64( stageHashEntries, stageHashEntriesUsed * sizeof( uint64_t ), hash_value );
	}

	// Add pipeline state object to the shared store

	using namespace le_backend_vk;
	le_pipeline_manager_i.introduce_graphics_pipeline_state( self->pipelineCache, self->obj, hash_value );

	// Note that the pipeline_manager makes a copy of the pso object before returning
	// from `introduce_graphics_pipeline_state` if it wants to keep it, which means
	// we don't have to worry about keeping self->obj alife.

	return hash_value;
}

// ----------------------------------------------------------------------
// Adds a shader module to a given pipeline builder object
//
// If shader module with the given shader stage already exists in pso,
// overwrite old entry, otherwise add new shader module.
static void le_graphics_pipeline_builder_add_shader_stage( le_graphics_pipeline_builder_o *self, struct le_shader_module_o *shaderModule ) {

	using namespace le_backend_vk;

	auto givenShaderStage = le_shader_module_i.get_stage( shaderModule );

	bool wasInserted = false;
	for ( auto &s : self->obj->shaderStages ) {
		if ( givenShaderStage == le_shader_module_i.get_stage( s ) ) {
			// PSO has a previous module which refers to the same shader stage as our given shaderModule.
			// We need to overwrite the shader module pointer with the given pointer.
			s           = shaderModule;
			wasInserted = true;
		}
	}

	// No entry for such shader stage yet, we add a new shader module
	if ( false == wasInserted ) {
		self->obj->shaderStages.push_back( shaderModule );
	}
}

// ----------------------------------------------------------------------

static void input_assembly_state_set_primitive_restart_enable( le_graphics_pipeline_builder_o *self, uint32_t const &primitiveRestartEnable ) {
	self->obj->data.inputAssemblyState.setPrimitiveRestartEnable( primitiveRestartEnable );
}

// ----------------------------------------------------------------------

static void input_assembly_state_set_toplogy( le_graphics_pipeline_builder_o *self, le::PrimitiveTopology const &topology ) {
	self->obj->data.inputAssemblyState.setTopology( le_to_vk( topology ) );
}

// ----------------------------------------------------------------------

static void blend_attachment_state_set_blend_enable( le_graphics_pipeline_builder_o *self, size_t which_attachment, const bool &enable ) {
	self->obj->data.blendAttachmentStates[ which_attachment ].setBlendEnable( enable );
}

// ----------------------------------------------------------------------

vk::BlendOp le_blend_op_to_vk( const le::BlendOp &rhs ) {
	return vk::BlendOp( rhs );
}

vk::BlendFactor le_blend_factor_to_vk( const le::BlendFactor &rhs ) {
	return vk::BlendFactor( rhs );
}

vk::ColorComponentFlags le_color_component_flags_to_vk( LeColorComponentFlags rhs ) {
	return vk::ColorComponentFlags( rhs );
}

static void blend_attachment_state_set_color_blend_op( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendOp &blendOp ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .setColorBlendOp( le_blend_op_to_vk( blendOp ) );
}

static void blend_attachment_state_set_alpha_blend_op( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendOp &blendOp ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .setAlphaBlendOp( le_blend_op_to_vk( blendOp ) );
}

static void blend_attachment_state_set_src_color_blend_factor( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .setSrcColorBlendFactor( le_blend_factor_to_vk( blendFactor ) );
}
static void blend_attachment_state_set_dst_color_blend_factor( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .setDstColorBlendFactor( le_blend_factor_to_vk( blendFactor ) );
}

static void blend_attachment_state_set_src_alpha_blend_factor( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .setSrcAlphaBlendFactor( le_blend_factor_to_vk( blendFactor ) );
}

static void blend_attachment_state_set_dst_alpha_blend_factor( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .setDstAlphaBlendFactor( le_blend_factor_to_vk( blendFactor ) );
}

static void blend_attachment_state_set_color_write_mask( le_graphics_pipeline_builder_o *self, size_t which_attachment, const LeColorComponentFlags &write_mask ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .setColorWriteMask( le_color_component_flags_to_vk( write_mask ) );
}

static void blend_attachment_state_use_preset( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::AttachmentBlendPreset &preset ) {

	switch ( preset ) {

	case le::AttachmentBlendPreset::ePremultipliedAlpha: {

		self->obj->data.blendAttachmentStates[ which_attachment ]
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
		        ::vk::ColorComponentFlagBits::eA ) //
		    ;

	} break;

	case le::AttachmentBlendPreset::eAdd: {

		self->obj->data.blendAttachmentStates[ which_attachment ]
		    .setBlendEnable( VK_TRUE )
		    .setColorBlendOp( vk::BlendOp::eAdd )
		    .setAlphaBlendOp( vk::BlendOp::eAdd )
		    .setSrcColorBlendFactor( vk::BlendFactor::eOne )  //  fragment shader output assumed to be premultiplied alpha!
		    .setDstColorBlendFactor( vk::BlendFactor::eOne )  //
		    .setSrcAlphaBlendFactor( vk::BlendFactor::eZero ) //
		    .setDstAlphaBlendFactor( vk::BlendFactor::eZero ) //
		    .setColorWriteMask(
		        vk::ColorComponentFlagBits::eR |
		        vk::ColorComponentFlagBits::eG |
		        vk::ColorComponentFlagBits::eB |
		        vk::ColorComponentFlagBits::eA ) //
		    ;

	} break;
	}
}

// ----------------------------------------------------------------------

static void tessellation_state_set_patch_control_points( le_graphics_pipeline_builder_o *self, uint32_t count ) {
	self->obj->data.tessellationState.setPatchControlPoints( count );
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_pipeline_builder_api( void *api ) {
	auto &i = static_cast<le_graphics_pipeline_builder_api *>( api )->le_graphics_pipeline_builder_i;

	i.create                                  = le_graphics_pipeline_builder_create;
	i.destroy                                 = le_graphics_pipeline_builder_destroy;
	i.build                                   = le_graphics_pipeline_builder_build;
	i.add_shader_stage                        = le_graphics_pipeline_builder_add_shader_stage;
	i.set_vertex_input_attribute_descriptions = le_graphics_pipeline_builder_set_vertex_input_attribute_descriptions;
	i.set_vertex_input_binding_descriptions   = le_graphics_pipeline_builder_set_vertex_input_binding_descriptions;
	i.set_rasterization_info                  = le_graphics_pipeline_builder_set_rasterization_info;
	i.set_input_assembly_info                 = le_graphics_pipeline_builder_set_input_assembly_info;
	i.set_tessellation_info                   = le_graphics_pipeline_builder_set_tessellation_info;
	i.set_multisample_info                    = le_graphics_pipeline_builder_set_multisample_info;
	i.set_depth_stencil_info                  = le_graphics_pipeline_builder_set_depth_stencil_info;

	i.input_assembly_state_i.set_primitive_restart_enable = input_assembly_state_set_primitive_restart_enable;
	i.input_assembly_state_i.set_topology                 = input_assembly_state_set_toplogy;

	i.blend_attachment_state_i.set_alpha_blend_op         = blend_attachment_state_set_alpha_blend_op;
	i.blend_attachment_state_i.set_color_blend_op         = blend_attachment_state_set_color_blend_op;
	i.blend_attachment_state_i.set_color_write_mask       = blend_attachment_state_set_color_write_mask;
	i.blend_attachment_state_i.set_dst_alpha_blend_factor = blend_attachment_state_set_dst_alpha_blend_factor;
	i.blend_attachment_state_i.set_src_alpha_blend_factor = blend_attachment_state_set_src_alpha_blend_factor;
	i.blend_attachment_state_i.set_dst_color_blend_factor = blend_attachment_state_set_dst_color_blend_factor;
	i.blend_attachment_state_i.set_src_color_blend_factor = blend_attachment_state_set_src_color_blend_factor;
	i.blend_attachment_state_i.use_preset                 = blend_attachment_state_use_preset;

	i.tessellation_state_i.set_patch_control_points = tessellation_state_set_patch_control_points;
}
