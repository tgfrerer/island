#include "le_pipeline_builder.h"
#include "le_core.h"
#include "le_log.h"

#include "3rdparty/src/spooky/SpookyV2.h"

#include "private/le_renderer/le_renderer_types.h" // for le_vertex_input_attribute_description le_vertex_input_binding_description

#include "le_backend_vk.h" // for access to pipeline state object cache

#include <cassert>
#include <string>
#include <cstring> // for memcpy
#include <vector>
#include <map>

#include <vulkan/vulkan.h>

#include "private/le_backend_vk/le_backend_types_pipeline.inl"

static constexpr auto LOGGER_LABEL = "le_pipeline_builder";

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

// contains everything (except renderpass/subpass) needed to create a pipeline in the backend
struct le_graphics_pipeline_builder_o {
	graphics_pipeline_state_o* obj           = nullptr;
	le_pipeline_manager_o*     pipelineCache = nullptr;
};

struct le_compute_pipeline_builder_o {
	compute_pipeline_state_o* obj           = nullptr;
	le_pipeline_manager_o*    pipelineCache = nullptr;
};

struct le_rtx_pipeline_builder_o {
	rtx_pipeline_state_o*  obj           = nullptr;
	le_pipeline_manager_o* pipelineCache = nullptr;
};

// ----------------------------------------------------------------------

struct le_shader_module_builder_o {
	le_pipeline_manager_o* pipeline_manager = nullptr;
	le::ShaderStage        shader_stage     = le::ShaderStage{};

	enum shader_module_builder_type_t {
		eUndefined  = 0,
		eFromSource = 1,
		eFromSpirV  = 2,
	} type = eUndefined;

	// Only used when builder type is eFromSource
	std::string              source_file_path       = {};
	std::string              source_defines_string  = {};
	le::ShaderSourceLanguage shader_source_language = le::ShaderSourceLanguage::eDefault;

	// Only used in case builder type is eFromSpirV
	uint32_t* spirv_code;        // non-owning
	uint32_t  spirv_code_length; // number of uint32_t elements in spirv_code array

	le_shader_module_handle               previous_handle = nullptr;
	std::map<uint32_t, std::vector<char>> specialisation_map;
};

inline bool set_type( le_shader_module_builder_o* self, le_shader_module_builder_o::shader_module_builder_type_t type ) {
	if ( self->type == le_shader_module_builder_o::eUndefined ) {
		self->type = type;
	}
	return self->type == type;
}

static le_shader_module_builder_o* le_shader_module_builder_create( le_pipeline_manager_o* pipeline_cache ) {
	auto self              = new le_shader_module_builder_o{};
	self->pipeline_manager = pipeline_cache;
	self->type             = le_shader_module_builder_o::eFromSource;
	return self;
}
static void le_shader_module_builder_destroy( le_shader_module_builder_o* self ) {
	delete self;
}
static void le_shader_module_builder_set_source_file_path( le_shader_module_builder_o* self, char const* source_file_path ) {
	if ( set_type( self, le_shader_module_builder_o::eFromSource ) ) {
		self->source_file_path = source_file_path;
	} else {
		static auto logger = LeLog( LOGGER_LABEL );
		logger.error( "Cannot set shader module to compile from source as it was set to use spir-v previously." );
	}
}
static void le_shader_module_builder_set_source_defines_string( le_shader_module_builder_o* self, char const* source_defines_string ) {
	if ( set_type( self, le_shader_module_builder_o::eFromSource ) ) {
		self->source_defines_string = source_defines_string;
	} else {
		static auto logger = LeLog( LOGGER_LABEL );
		logger.error( "Cannot set source defines for a shader module that is not compiled from source. \n(Consider using specialization constants if you want precompiled shader code, yet still to be able to set shader constants at runtime.)" );
	}
}
static void le_shader_module_builder_set_shader_stage( le_shader_module_builder_o* self, le::ShaderStage const& shader_stage ) {
	self->shader_stage = shader_stage;
}
static void le_shader_module_builder_set_source_language( le_shader_module_builder_o* self, le::ShaderSourceLanguage const& shader_source_language ) {
	self->shader_source_language = shader_source_language;
}
static void le_shader_module_builder_set_handle( le_shader_module_builder_o* self, le_shader_module_handle previous_handle ) {
	self->previous_handle = previous_handle;
}
static void le_shader_module_builder_set_specialization_constant( le_shader_module_builder_o* self, uint32_t id, void const* value, uint32_t size ) {
	auto& entry = self->specialisation_map[ id ];
	entry       = std::vector<char>( size );
	memcpy( entry.data(), value, size );
}
static le_shader_module_handle le_shader_module_builder_build( le_shader_module_builder_o* self ) {
	using namespace le_backend_vk;
	static auto logger = LeLog( LOGGER_LABEL );

	// We must flatten specialization constant data and entries, if any.

	std::vector<char>                     sp_data;
	std::vector<VkSpecializationMapEntry> sp_info;

	for ( auto& e : self->specialisation_map ) {
		uint32_t                 offset = sp_data.size();
		VkSpecializationMapEntry info;
		info.constantID = e.first;
		info.size       = e.second.size();
		info.offset     = offset;

		sp_info.emplace_back( info );
		sp_data.insert( sp_data.end(), e.second.begin(), e.second.end() );
	}

	// call the correct builder function based on type

	switch ( self->type ) {
	case le_shader_module_builder_o::eFromSource:
		return le_pipeline_manager_i.create_shader_module(
		    self->pipeline_manager,
		    self->source_file_path.c_str(),
		    { self->shader_source_language },
		    { self->shader_stage },
		    self->source_defines_string.c_str(),
		    self->previous_handle,
		    sp_info.data(),
		    sp_info.size(),
		    sp_data.data(),
		    sp_data.size() );
	case le_shader_module_builder_o::eFromSpirV:
		return le_pipeline_manager_i.create_shader_module_from_spirv(
		    self->pipeline_manager,
		    self->spirv_code,
		    self->spirv_code_length,
		    { self->shader_stage },
		    self->previous_handle,
		    sp_info.data(),
		    sp_info.size(),
		    sp_data.data(),
		    sp_data.size() );
	default:
		logger.error( "Could not generate shader module - shader module type not set." );
		return nullptr;
	}
}

// ----------------------------------------------------------------------

static le_compute_pipeline_builder_o* le_compute_pipeline_builder_create( le_pipeline_manager_o* pipelineCache ) {
	auto self           = new le_compute_pipeline_builder_o();
	self->pipelineCache = pipelineCache;
	self->obj           = new compute_pipeline_state_o();

	// Now initialise obj with default values.
	self->obj->shaderStage = nullptr;

	return self;
}

// ----------------------------------------------------------------------

static void le_compute_pipeline_builder_destroy( le_compute_pipeline_builder_o* self ) {
	if ( self ) {
		delete self->obj;
	}
	delete self;
}

// ----------------------------------------------------------------------
// Builds a hash value from the pipeline state object, that is:
//	+ pipeline shader stages,
//  + and associated settings,
// so that we have a unique fingerprint for this pipeline.
// The handle contains the hash value and is unique for pipeline
// state objects with given settings.
static le_cpso_handle le_compute_pipeline_builder_build( le_compute_pipeline_builder_o* self ) {
	using namespace le_backend_vk;
	le_cpso_handle pipeline_handle;
	le_pipeline_manager_i.introduce_compute_pipeline_state( self->pipelineCache, self->obj, &pipeline_handle );
	return pipeline_handle;
}

// ----------------------------------------------------------------------

static void le_compute_pipeline_builder_set_shader_stage( le_compute_pipeline_builder_o* self, le_shader_module_handle shaderModule ) {
	assert( self->obj );
	if ( self->obj ) {
		self->obj->shaderStage = shaderModule;
	}
}

// ----------------------------------------------------------------------

static le_rtx_pipeline_builder_o* le_rtx_pipeline_builder_create( le_pipeline_manager_o* pipelineCache ) {
	auto self           = new le_rtx_pipeline_builder_o();
	self->pipelineCache = pipelineCache;
	self->obj           = new rtx_pipeline_state_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_rtx_pipeline_builder_destroy( le_rtx_pipeline_builder_o* self ) {
	if ( self ) {
		delete self->obj;
	}
	delete self;
}

// ----------------------------------------------------------------------
// Adds shader module to pso if not yet encountered
// returns index into shader modules for this module
static uint32_t rtx_pipeline_builder_add_shader_module( le_rtx_pipeline_builder_o* self, le_shader_module_handle shaderModule ) {
	assert( self->obj );

	if ( nullptr == shaderModule ) {
		return LE_SHADER_UNUSED_NV;
	}

	size_t module_idx = 0;

	for ( auto& m : self->obj->shaderStages ) {
		if ( shaderModule == m ) {
			break;
		}
		module_idx++;
	}

	if ( module_idx == self->obj->shaderStages.size() ) {
		self->obj->shaderStages.push_back( shaderModule );
	}

	return uint32_t( module_idx );
}
// ----------------------------------------------------------------------

void le_rtx_pipeline_builder_set_shader_group_ray_gen( le_rtx_pipeline_builder_o* self, le_shader_module_handle raygen_shader ) {
	assert( raygen_shader && "must specify ray gen shader" );
	le_rtx_shader_group_info info{};
	info.type             = le::RayTracingShaderGroupType::eRayGen;
	info.generalShaderIdx = rtx_pipeline_builder_add_shader_module( self, raygen_shader );
	self->obj->shaderGroups.emplace_back( info );
}

void le_rtx_pipeline_builder_add_shader_group_miss( le_rtx_pipeline_builder_o* self, le_shader_module_handle miss_shader ) {
	assert( miss_shader && "must specify miss shader" );
	le_rtx_shader_group_info info{};
	info.type             = le::RayTracingShaderGroupType::eMiss;
	info.generalShaderIdx = rtx_pipeline_builder_add_shader_module( self, miss_shader );
	self->obj->shaderGroups.emplace_back( info );
}

void le_rtx_pipeline_builder_add_shader_group_callable( le_rtx_pipeline_builder_o* self, le_shader_module_handle callable_shader ) {
	assert( callable_shader && "must specify callable shader" );
	le_rtx_shader_group_info info{};
	info.type             = le::RayTracingShaderGroupType::eCallable;
	info.generalShaderIdx = rtx_pipeline_builder_add_shader_module( self, callable_shader );
	self->obj->shaderGroups.emplace_back( info );
}

void le_rtx_pipeline_builder_add_shader_group_triangle_hit( le_rtx_pipeline_builder_o* self, le_shader_module_handle maybe_closest_hit_shader, le_shader_module_handle maybe_any_hit_shader ) {
	assert( ( maybe_any_hit_shader || maybe_closest_hit_shader ) && "must specify at least one of closet hit or any hit shader" );
	le_rtx_shader_group_info info{};
	info.type                = le::RayTracingShaderGroupType::eTrianglesHitGroup;
	info.closestHitShaderIdx = rtx_pipeline_builder_add_shader_module( self, maybe_closest_hit_shader );
	info.anyHitShaderIdx     = rtx_pipeline_builder_add_shader_module( self, maybe_any_hit_shader );
	self->obj->shaderGroups.emplace_back( info );
}

void le_rtx_pipeline_builder_add_shader_group_procedural_hit( le_rtx_pipeline_builder_o* self, le_shader_module_handle intersection_shader, le_shader_module_handle maybe_closest_hit_shader, le_shader_module_handle maybe_any_hit_shader ) {
	assert( intersection_shader && "must specify intersection shader" );
	le_rtx_shader_group_info info{};
	info.type                  = le::RayTracingShaderGroupType::eProceduralHitGroup;
	info.intersectionShaderIdx = rtx_pipeline_builder_add_shader_module( self, intersection_shader );
	info.closestHitShaderIdx   = rtx_pipeline_builder_add_shader_module( self, maybe_closest_hit_shader );
	info.anyHitShaderIdx       = rtx_pipeline_builder_add_shader_module( self, maybe_any_hit_shader );
	self->obj->shaderGroups.emplace_back( info );
}
// ----------------------------------------------------------------------
// Builds a hash value from the pipeline state object, that is:
//	+ pipeline shader stages,
//  + and associated settings,
// so that we have a unique fingerprint for this pipeline.
// The handle contains the hash value and is unique for pipeline
// state objects with given settings.
static le_rtxpso_handle le_rtx_pipeline_builder_build( le_rtx_pipeline_builder_o* self ) {

	le_rtxpso_handle pipeline_handle = {};

	using namespace le_backend_vk;

	// Introduce pipeline state object to manager so that it may be cached.

	le_pipeline_manager_i.introduce_rtx_pipeline_state( self->pipelineCache, self->obj, &pipeline_handle );

	return pipeline_handle;
}

// ----------------------------------------------------------------------

static le_graphics_pipeline_builder_o*
le_graphics_pipeline_builder_create( le_pipeline_manager_o* pipelineCache ) {
	auto self = new le_graphics_pipeline_builder_o();

	self->pipelineCache = pipelineCache;
	self->obj           = new graphics_pipeline_state_o();
	// set default values

	self->obj->data.inputAssemblyState = {
	    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	    .pNext                  = nullptr,
	    .flags                  = 0,
	    .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	    .primitiveRestartEnable = VK_FALSE,
	};

	self->obj->data.tessellationState = {
	    .sType              = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
	    .pNext              = nullptr, // optional
	    .flags              = 0,       // optional
	    .patchControlPoints = 3,
	};

	// Viewport and scissor are tracked as dynamic states,
	// so this object will not be used,
	// but we need to give it some default values to match requirements.
	//
	self->obj->data.rasterizationInfo = {
	    .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	    .pNext                   = nullptr,
	    .flags                   = 0,
	    .depthClampEnable        = VK_FALSE,
	    .rasterizerDiscardEnable = VK_FALSE,
	    .polygonMode             = VK_POLYGON_MODE_FILL,
	    .cullMode                = VK_CULL_MODE_NONE,
	    .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	    .depthBiasEnable         = VK_FALSE,
	    .depthBiasConstantFactor = 0.f,
	    .depthBiasClamp          = 0.f,
	    .depthBiasSlopeFactor    = 1.f,
	    .lineWidth               = 1.f,
	};

	self->obj->data.multisampleState = {
	    .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
	    .pNext                 = nullptr, // optional
	    .flags                 = 0,       // optional
	    .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
	    .sampleShadingEnable   = VK_FALSE,
	    .minSampleShading      = 0.f,
	    .pSampleMask           = nullptr, // optional
	    .alphaToCoverageEnable = VK_FALSE,
	    .alphaToOneEnable      = VK_FALSE,
	};

	VkStencilOpState stencilOpState = {
	    .failOp      = VK_STENCIL_OP_KEEP,
	    .passOp      = VK_STENCIL_OP_KEEP,
	    .depthFailOp = VK_STENCIL_OP_KEEP,
	    .compareOp   = VK_COMPARE_OP_NEVER,
	    .compareMask = 0,
	    .writeMask   = 0,
	    .reference   = 0,
	};

	self->obj->data.depthStencilState = {
	    .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
	    .pNext                 = nullptr, // optional
	    .flags                 = 0,       // optional
	    .depthTestEnable       = VK_TRUE,
	    .depthWriteEnable      = VK_TRUE,
	    .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
	    .depthBoundsTestEnable = VK_FALSE,
	    .stencilTestEnable     = VK_FALSE,
	    .front                 = stencilOpState,
	    .back                  = stencilOpState,
	    .minDepthBounds        = 0.f,
	    .maxDepthBounds        = 0.f,
	};

	// Default values for color blend state: premultiplied alpha
	for ( auto& blendAttachmentState : self->obj->data.blendAttachmentStates ) {

		blendAttachmentState = {
		    .blendEnable         = VK_TRUE,
		    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		    .colorBlendOp        = VK_BLEND_OP_ADD,
		    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		    .alphaBlendOp        = VK_BLEND_OP_ADD,
		    .colorWriteMask =
		        VK_COLOR_COMPONENT_R_BIT |
		        VK_COLOR_COMPONENT_G_BIT |
		        VK_COLOR_COMPONENT_B_BIT |
		        VK_COLOR_COMPONENT_A_BIT, // optional
		};
	}

	return self;
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_add_binding( le_graphics_pipeline_builder_o* self, uint8_t binding_number ) {
	le_vertex_input_binding_description binding;
	binding.stride     = 0;
	binding.binding    = binding_number;
	binding.input_rate = le_vertex_input_rate::ePerVertex;
	assert( binding_number == self->obj->explicitVertexInputBindingDescriptions.size() && "binding numbers must be in sequence" );
	self->obj->explicitVertexInputBindingDescriptions.emplace_back( binding );
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_set_binding_input_rate( le_graphics_pipeline_builder_o* self, uint8_t binding_number, const le_vertex_input_rate& input_rate ) {
	self->obj->explicitVertexInputBindingDescriptions[ binding_number ].input_rate = input_rate;
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_set_binding_stride( le_graphics_pipeline_builder_o* self, uint8_t binding_number, uint16_t stride ) {
	self->obj->explicitVertexInputBindingDescriptions[ binding_number ].stride = stride;
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_binding_add_attribute( le_graphics_pipeline_builder_o* self, uint8_t binding_number, uint8_t attribute_number ) {
	le_vertex_input_attribute_description attribute;

	attribute.binding        = binding_number;
	attribute.location       = attribute_number;
	attribute.type           = le_num_type::eFloat; // Float is the most likely type, so we're setting this as default
	attribute.vecsize        = 1;                   // 1 means a single float, for vec3 use: 3, for vec2 use: 2, ...
	attribute.isNormalised   = false;               // Mostly used for uint8_t which want to be treated as float values.
	attribute.binding_offset = 0;                   // if not part of a struct, no binding offset must be sset

	assert( attribute_number == self->obj->explicitVertexAttributeDescriptions.size() && "attribute locations must be in sequence" );

	self->obj->explicitVertexAttributeDescriptions.emplace_back( attribute );
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_attribute_set_offset( le_graphics_pipeline_builder_o* self, uint8_t attribute_location, uint16_t offset ) {
	self->obj->explicitVertexAttributeDescriptions[ attribute_location ].binding_offset = offset;
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_attribute_set_type( le_graphics_pipeline_builder_o* self, uint8_t attribute_location, const le_num_type& type ) {
	self->obj->explicitVertexAttributeDescriptions[ attribute_location ].type = type;
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_attribute_set_vec_size( le_graphics_pipeline_builder_o* self, uint8_t attribute_location, uint8_t vec_size ) {
	self->obj->explicitVertexAttributeDescriptions[ attribute_location ].vecsize = vec_size;
}

// ----------------------------------------------------------------------

void le_graphics_pipeline_builder_attribute_set_is_normalized( le_graphics_pipeline_builder_o* self, uint8_t attribute_location, bool is_normalized ) {
	self->obj->explicitVertexAttributeDescriptions[ attribute_location ].isNormalised = is_normalized;
}

// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_set_vertex_input_attribute_descriptions( le_graphics_pipeline_builder_o* self, le_vertex_input_attribute_description* p_input_attribute_descriptions, size_t count ) {
	self->obj->explicitVertexAttributeDescriptions =
	    { p_input_attribute_descriptions,
	      p_input_attribute_descriptions + count };
}

// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_set_vertex_input_binding_descriptions( le_graphics_pipeline_builder_o* self, le_vertex_input_binding_description* p_input_binding_descriptions, size_t count ) {
	self->obj->explicitVertexInputBindingDescriptions =
	    { p_input_binding_descriptions,
	      p_input_binding_descriptions + count };
}

// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_set_multisample_info( le_graphics_pipeline_builder_o* self, const VkPipelineMultisampleStateCreateInfo& multisampleInfo ) {
	self->obj->data.multisampleState = multisampleInfo;
}

static void le_graphics_pipeline_builder_set_depth_stencil_info( le_graphics_pipeline_builder_o* self, const VkPipelineDepthStencilStateCreateInfo& depthStencilInfo ) {
	self->obj->data.depthStencilState = depthStencilInfo;
}
// ----------------------------------------------------------------------

static void le_graphics_pipeline_builder_destroy( le_graphics_pipeline_builder_o* self ) {
	if ( self ) {
		delete self->obj;
	}
	delete self;
}

// ----------------------------------------------------------------------

// Calculate pipeline info hash, and add pipeline info to shared store if not yet seen.
// Return pipeline hash
static le_gpso_handle le_graphics_pipeline_builder_build( le_graphics_pipeline_builder_o* self ) {

	le_gpso_handle pipeline_handle;

	// Note that the pipeline_manager makes a copy of the pso object before returning
	// from `introduce_graphics_pipeline_state` if it wants to keep it, which means
	// we don't have to worry about keeping self->obj alife.

	using namespace le_backend_vk;
	le_pipeline_manager_i.introduce_graphics_pipeline_state( self->pipelineCache, self->obj, &pipeline_handle );

	return pipeline_handle;
}

// ----------------------------------------------------------------------
// Adds a shader module to a given pipeline builder object
//
// If shader module with the given shader stage already exists in pso,
// overwrite old entry, otherwise add new shader module.
static void le_graphics_pipeline_builder_add_shader_stage( le_graphics_pipeline_builder_o* self, le_shader_module_handle shaderModule ) {

	static auto logger = LeLog( LOGGER_LABEL );

	using namespace le_backend_vk;

	auto givenShaderStage = le_shader_module_i.get_stage( self->pipelineCache, shaderModule );

	size_t i = 0;
	for ( ; i != self->obj->shaderStagePerModule.size(); i++ ) {
		if ( givenShaderStage == self->obj->shaderStagePerModule[ i ] ) {
			// This pipeline builder has already had a shader for the given stage.
			// we must warn about this.
			self->obj->shaderModules[ i ] = shaderModule;
			logger.warn( "Overwriting shader stage for shader module %x", shaderModule );
			break;
		}
	}

	// No entry for such shader stage yet, we add a new shader module
	if ( i == self->obj->shaderStagePerModule.size() ) {
		self->obj->shaderModules.push_back( shaderModule );
		self->obj->shaderStagePerModule.push_back( givenShaderStage );
	}
}

// ----------------------------------------------------------------------

static void input_assembly_state_set_primitive_restart_enable( le_graphics_pipeline_builder_o* self, uint32_t const& primitiveRestartEnable ) {
	self->obj->data.inputAssemblyState.primitiveRestartEnable = primitiveRestartEnable;
}

// ----------------------------------------------------------------------

static void input_assembly_state_set_toplogy( le_graphics_pipeline_builder_o* self, le::PrimitiveTopology const& topology ) {
	self->obj->data.inputAssemblyState.topology = static_cast<VkPrimitiveTopology>( topology );
}

static void blend_attachment_state_set_blend_enable( le_graphics_pipeline_builder_o* self, size_t which_attachment, bool blendEnable ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .blendEnable = blendEnable;
}

static void blend_attachment_state_set_color_blend_op( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::BlendOp& blendOp ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .colorBlendOp = static_cast<VkBlendOp>( blendOp );
}

static void blend_attachment_state_set_alpha_blend_op( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::BlendOp& blendOp ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .alphaBlendOp = static_cast<VkBlendOp>( blendOp );
}

static void blend_attachment_state_set_src_color_blend_factor( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::BlendFactor& blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .srcColorBlendFactor = static_cast<VkBlendFactor>( blendFactor );
}
static void blend_attachment_state_set_dst_color_blend_factor( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::BlendFactor& blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .dstColorBlendFactor = static_cast<VkBlendFactor>( blendFactor );
}

static void blend_attachment_state_set_src_alpha_blend_factor( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::BlendFactor& blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .srcAlphaBlendFactor = static_cast<VkBlendFactor>( blendFactor );
}

static void blend_attachment_state_set_dst_alpha_blend_factor( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::BlendFactor& blendFactor ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .dstAlphaBlendFactor = static_cast<VkBlendFactor>( blendFactor );
}

static void blend_attachment_state_set_color_write_mask( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::ColorComponentFlags& write_mask ) {
	self->obj->data.blendAttachmentStates[ which_attachment ]
	    .colorWriteMask = static_cast<VkColorComponentFlags>( write_mask );
}

static void blend_attachment_state_use_preset( le_graphics_pipeline_builder_o* self, size_t which_attachment, const le::AttachmentBlendPreset& preset ) {

	switch ( preset ) {
	case le::AttachmentBlendPreset::ePremultipliedAlpha: {
		self->obj->data.blendAttachmentStates[ which_attachment ] = {
		    .blendEnable         = VK_TRUE,
		    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		    .colorBlendOp        = VK_BLEND_OP_ADD,
		    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		    .alphaBlendOp        = VK_BLEND_OP_ADD,
		    .colorWriteMask =
		        VK_COLOR_COMPONENT_R_BIT |
		        VK_COLOR_COMPONENT_G_BIT |
		        VK_COLOR_COMPONENT_B_BIT |
		        VK_COLOR_COMPONENT_A_BIT, // optional
		};
	} break;
	case le::AttachmentBlendPreset::eAdd: {
		self->obj->data.blendAttachmentStates[ which_attachment ] = {
		    .blendEnable         = VK_TRUE,
		    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, //  fragment shader output assumed to be premultiplied alpha!
		    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
		    .colorBlendOp        = VK_BLEND_OP_ADD,
		    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		    .alphaBlendOp        = VK_BLEND_OP_ADD,
		    .colorWriteMask =
		        VK_COLOR_COMPONENT_R_BIT |
		        VK_COLOR_COMPONENT_G_BIT |
		        VK_COLOR_COMPONENT_B_BIT |
		        VK_COLOR_COMPONENT_A_BIT, // optional
		};
	} break;
	case le::AttachmentBlendPreset::eMultiply: {
		self->obj->data.blendAttachmentStates[ which_attachment ] = {
		    .blendEnable         = VK_TRUE,
		    .srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR,
		    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		    .colorBlendOp        = VK_BLEND_OP_ADD,
		    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, // has no effect because only rgb
		    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, // has no effect because only rgb
		    .alphaBlendOp        = VK_BLEND_OP_ADD,      // has no effect because only rgb
		    .colorWriteMask =
		        VK_COLOR_COMPONENT_R_BIT |
		        VK_COLOR_COMPONENT_G_BIT |
		        VK_COLOR_COMPONENT_B_BIT, // note that we're not using alpha here
		};
	} break;
	case le::AttachmentBlendPreset::eCopy: {
		self->obj->data.blendAttachmentStates[ which_attachment ]
		    .blendEnable = VK_FALSE;

	} break;
	}
}

static void tessellation_state_set_patch_control_points( le_graphics_pipeline_builder_o* self, uint32_t count ) {
	self->obj->data.tessellationState.patchControlPoints = count;
}

static void rasterization_state_set_depth_clamp_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.rasterizationInfo.depthClampEnable = enable;
}
static void rasterization_state_set_rasterizer_discard_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.rasterizationInfo.rasterizerDiscardEnable = enable;
}
static void rasterization_state_set_polygon_mode( le_graphics_pipeline_builder_o* self, le::PolygonMode const& polygon_mode ) {
	self->obj->data.rasterizationInfo.polygonMode = VkPolygonMode( polygon_mode );
}
static void rasterization_state_set_cull_mode( le_graphics_pipeline_builder_o* self, le::CullModeFlags const& cull_mode_flag_bits ) {
	self->obj->data.rasterizationInfo.cullMode = VkCullModeFlags( cull_mode_flag_bits );
}
static void rasterization_state_set_front_face( le_graphics_pipeline_builder_o* self, le::FrontFace const& front_face ) {
	self->obj->data.rasterizationInfo.frontFace = VkFrontFace( front_face );
}
static void rasterization_state_set_depth_bias_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.rasterizationInfo.depthBiasEnable = enable;
}
static void rasterization_state_set_depth_bias_constant_factor( le_graphics_pipeline_builder_o* self, float const& factor ) {
	self->obj->data.rasterizationInfo.depthBiasConstantFactor = factor;
}
static void rasterization_state_set_depth_bias_clamp( le_graphics_pipeline_builder_o* self, float const& clamp ) {
	self->obj->data.rasterizationInfo.depthBiasClamp = clamp;
}
static void rasterization_state_set_depth_bias_slope_factor( le_graphics_pipeline_builder_o* self, float const& factor ) {
	self->obj->data.rasterizationInfo.depthBiasSlopeFactor = factor;
}
static void rasterization_state_set_line_width( le_graphics_pipeline_builder_o* self, float const& line_width ) {
	self->obj->data.rasterizationInfo.lineWidth = line_width;
}

static void multisample_state_set_rasterization_samples( le_graphics_pipeline_builder_o* self, le::SampleCountFlagBits const& num_samples ) {
	self->obj->data.multisampleState.rasterizationSamples = VkSampleCountFlagBits( num_samples );
}

static void multisample_state_set_sample_shading_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.multisampleState.sampleShadingEnable = enable;
}
static void multisample_state_set_min_sample_shading( le_graphics_pipeline_builder_o* self, float const& min_sample_shading ) {
	self->obj->data.multisampleState.minSampleShading = min_sample_shading;
}
static void multisample_state_set_alpha_to_coverage_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.multisampleState.alphaToCoverageEnable = enable;
}
static void multisample_state_set_alpha_to_one_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.multisampleState.alphaToOneEnable = enable;
}

// ----------------------------------------------------------------------

static void stencil_op_state_front_set_fail_op( le_graphics_pipeline_builder_o* self, le::StencilOp const& op ) {
	self->obj->data.depthStencilState.front.failOp = VkStencilOp( op );
}
static void stencil_op_state_front_set_pass_op( le_graphics_pipeline_builder_o* self, le::StencilOp const& op ) {
	self->obj->data.depthStencilState.front.passOp = VkStencilOp( op );
}
static void stencil_op_state_front_set_depth_fail_op( le_graphics_pipeline_builder_o* self, le::StencilOp const& op ) {
	self->obj->data.depthStencilState.front.depthFailOp = VkStencilOp( op );
}
static void stencil_op_state_front_set_compare_op( le_graphics_pipeline_builder_o* self, le::CompareOp const& op ) {
	self->obj->data.depthStencilState.front.compareOp = VkCompareOp( op );
}
static void stencil_op_state_front_set_compare_mask( le_graphics_pipeline_builder_o* self, uint32_t const& mask ) {
	self->obj->data.depthStencilState.front.compareMask = mask;
}
static void stencil_op_state_front_set_write_mask( le_graphics_pipeline_builder_o* self, uint32_t const& mask ) {
	self->obj->data.depthStencilState.front.writeMask = mask;
}
static void stencil_op_state_front_set_reference( le_graphics_pipeline_builder_o* self, uint32_t const& reference ) {
	self->obj->data.depthStencilState.front.reference = reference;
}

static void stencil_op_state_back_set_fail_op( le_graphics_pipeline_builder_o* self, le::StencilOp const& op ) {
	self->obj->data.depthStencilState.back.failOp = VkStencilOp( op );
}
static void stencil_op_state_back_set_pass_op( le_graphics_pipeline_builder_o* self, le::StencilOp const& op ) {
	self->obj->data.depthStencilState.back.passOp = VkStencilOp( op );
}
static void stencil_op_state_back_set_depth_fail_op( le_graphics_pipeline_builder_o* self, le::StencilOp const& op ) {
	self->obj->data.depthStencilState.back.depthFailOp = VkStencilOp( op );
}
static void stencil_op_state_back_set_compare_op( le_graphics_pipeline_builder_o* self, le::CompareOp const& op ) {
	self->obj->data.depthStencilState.back.compareOp = VkCompareOp( op );
}
static void stencil_op_state_back_set_compare_mask( le_graphics_pipeline_builder_o* self, uint32_t const& mask ) {
	self->obj->data.depthStencilState.back.compareMask = mask;
}
static void stencil_op_state_back_set_write_mask( le_graphics_pipeline_builder_o* self, uint32_t const& mask ) {
	self->obj->data.depthStencilState.back.writeMask = mask;
}
static void stencil_op_state_back_set_reference( le_graphics_pipeline_builder_o* self, uint32_t const& reference ) {
	self->obj->data.depthStencilState.back.reference = reference;
}

static void depth_stencil_state_set_depth_test_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.depthStencilState.depthTestEnable = enable;
}
static void depth_stencil_state_set_depth_write_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.depthStencilState.depthWriteEnable = enable;
}
static void depth_stencil_state_set_depth_compare_op( le_graphics_pipeline_builder_o* self, le::CompareOp const& compare_op ) {
	self->obj->data.depthStencilState.depthCompareOp = VkCompareOp( compare_op );
}
static void depth_stencil_state_set_depth_bounds_test_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.depthStencilState.depthBoundsTestEnable = enable;
}
static void depth_stencil_state_set_stencil_test_enable( le_graphics_pipeline_builder_o* self, bool const& enable ) {
	self->obj->data.depthStencilState.stencilTestEnable = enable;
}
static void depth_stencil_state_set_min_depth_bounds( le_graphics_pipeline_builder_o* self, float const& min_bounds ) {
	self->obj->data.depthStencilState.minDepthBounds = min_bounds;
}
static void depth_stencil_state_set_max_depth_bounds( le_graphics_pipeline_builder_o* self, float const& max_bounds ) {
	self->obj->data.depthStencilState.maxDepthBounds = max_bounds;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_pipeline_builder, api ) {

	{
		// setup graphics pipeline builder api
		auto& i = static_cast<le_pipeline_builder_api*>( api )->le_graphics_pipeline_builder_i;

		i.create                                  = le_graphics_pipeline_builder_create;
		i.destroy                                 = le_graphics_pipeline_builder_destroy;
		i.build                                   = le_graphics_pipeline_builder_build;
		i.add_shader_stage                        = le_graphics_pipeline_builder_add_shader_stage;
		i.set_vertex_input_attribute_descriptions = le_graphics_pipeline_builder_set_vertex_input_attribute_descriptions;
		i.set_vertex_input_binding_descriptions   = le_graphics_pipeline_builder_set_vertex_input_binding_descriptions;
		i.set_multisample_info                    = le_graphics_pipeline_builder_set_multisample_info;
		i.set_depth_stencil_info                  = le_graphics_pipeline_builder_set_depth_stencil_info;

		i.attribute_binding_state_i.add_binding                 = le_graphics_pipeline_builder_add_binding;
		i.attribute_binding_state_i.set_binding_input_rate      = le_graphics_pipeline_builder_set_binding_input_rate;
		i.attribute_binding_state_i.set_binding_stride          = le_graphics_pipeline_builder_set_binding_stride;
		i.attribute_binding_state_i.binding_add_attribute       = le_graphics_pipeline_builder_binding_add_attribute;
		i.attribute_binding_state_i.attribute_set_offset        = le_graphics_pipeline_builder_attribute_set_offset;
		i.attribute_binding_state_i.attribute_set_type          = le_graphics_pipeline_builder_attribute_set_type;
		i.attribute_binding_state_i.attribute_set_vec_size      = le_graphics_pipeline_builder_attribute_set_vec_size;
		i.attribute_binding_state_i.attribute_set_is_normalized = le_graphics_pipeline_builder_attribute_set_is_normalized;

		i.input_assembly_state_i.set_primitive_restart_enable = input_assembly_state_set_primitive_restart_enable;
		i.input_assembly_state_i.set_topology                 = input_assembly_state_set_toplogy;

		i.blend_attachment_state_i.set_blend_enable           = blend_attachment_state_set_blend_enable;
		i.blend_attachment_state_i.set_alpha_blend_op         = blend_attachment_state_set_alpha_blend_op;
		i.blend_attachment_state_i.set_color_blend_op         = blend_attachment_state_set_color_blend_op;
		i.blend_attachment_state_i.set_color_write_mask       = blend_attachment_state_set_color_write_mask;
		i.blend_attachment_state_i.set_dst_alpha_blend_factor = blend_attachment_state_set_dst_alpha_blend_factor;
		i.blend_attachment_state_i.set_src_alpha_blend_factor = blend_attachment_state_set_src_alpha_blend_factor;
		i.blend_attachment_state_i.set_dst_color_blend_factor = blend_attachment_state_set_dst_color_blend_factor;
		i.blend_attachment_state_i.set_src_color_blend_factor = blend_attachment_state_set_src_color_blend_factor;
		i.blend_attachment_state_i.use_preset                 = blend_attachment_state_use_preset;

		i.tessellation_state_i.set_patch_control_points = tessellation_state_set_patch_control_points;

		i.rasterization_state_i.set_depth_clamp_enable         = rasterization_state_set_depth_clamp_enable;
		i.rasterization_state_i.set_rasterizer_discard_enable  = rasterization_state_set_rasterizer_discard_enable;
		i.rasterization_state_i.set_polygon_mode               = rasterization_state_set_polygon_mode;
		i.rasterization_state_i.set_cull_mode                  = rasterization_state_set_cull_mode;
		i.rasterization_state_i.set_front_face                 = rasterization_state_set_front_face;
		i.rasterization_state_i.set_depth_bias_enable          = rasterization_state_set_depth_bias_enable;
		i.rasterization_state_i.set_depth_bias_constant_factor = rasterization_state_set_depth_bias_constant_factor;
		i.rasterization_state_i.set_depth_bias_clamp           = rasterization_state_set_depth_bias_clamp;
		i.rasterization_state_i.set_depth_bias_slope_factor    = rasterization_state_set_depth_bias_slope_factor;
		i.rasterization_state_i.set_line_width                 = rasterization_state_set_line_width;

		i.multisample_state_i.set_rasterization_samples    = multisample_state_set_rasterization_samples;
		i.multisample_state_i.set_sample_shading_enable    = multisample_state_set_sample_shading_enable;
		i.multisample_state_i.set_min_sample_shading       = multisample_state_set_min_sample_shading;
		i.multisample_state_i.set_alpha_to_coverage_enable = multisample_state_set_alpha_to_coverage_enable;
		i.multisample_state_i.set_alpha_to_one_enable      = multisample_state_set_alpha_to_one_enable;

		i.stencil_op_state_front_i.set_fail_op       = stencil_op_state_front_set_fail_op;
		i.stencil_op_state_front_i.set_pass_op       = stencil_op_state_front_set_pass_op;
		i.stencil_op_state_front_i.set_depth_fail_op = stencil_op_state_front_set_depth_fail_op;
		i.stencil_op_state_front_i.set_compare_op    = stencil_op_state_front_set_compare_op;
		i.stencil_op_state_front_i.set_compare_mask  = stencil_op_state_front_set_compare_mask;
		i.stencil_op_state_front_i.set_write_mask    = stencil_op_state_front_set_write_mask;
		i.stencil_op_state_front_i.set_reference     = stencil_op_state_front_set_reference;

		i.stencil_op_state_back_i.set_fail_op       = stencil_op_state_back_set_fail_op;
		i.stencil_op_state_back_i.set_pass_op       = stencil_op_state_back_set_pass_op;
		i.stencil_op_state_back_i.set_depth_fail_op = stencil_op_state_back_set_depth_fail_op;
		i.stencil_op_state_back_i.set_compare_op    = stencil_op_state_back_set_compare_op;
		i.stencil_op_state_back_i.set_compare_mask  = stencil_op_state_back_set_compare_mask;
		i.stencil_op_state_back_i.set_write_mask    = stencil_op_state_back_set_write_mask;
		i.stencil_op_state_back_i.set_reference     = stencil_op_state_back_set_reference;

		i.depth_stencil_state_i.set_depth_test_enable        = depth_stencil_state_set_depth_test_enable;
		i.depth_stencil_state_i.set_depth_write_enable       = depth_stencil_state_set_depth_write_enable;
		i.depth_stencil_state_i.set_depth_compare_op         = depth_stencil_state_set_depth_compare_op;
		i.depth_stencil_state_i.set_depth_bounds_test_enable = depth_stencil_state_set_depth_bounds_test_enable;
		i.depth_stencil_state_i.set_stencil_test_enable      = depth_stencil_state_set_stencil_test_enable;
		i.depth_stencil_state_i.set_min_depth_bounds         = depth_stencil_state_set_min_depth_bounds;
		i.depth_stencil_state_i.set_max_depth_bounds         = depth_stencil_state_set_max_depth_bounds;
	}

	{
		// setup compute pipeline builder api
		auto& i            = static_cast<le_pipeline_builder_api*>( api )->le_compute_pipeline_builder_i;
		i.create           = le_compute_pipeline_builder_create;
		i.destroy          = le_compute_pipeline_builder_destroy;
		i.build            = le_compute_pipeline_builder_build;
		i.set_shader_stage = le_compute_pipeline_builder_set_shader_stage;
	}

	{
		// setup rtx pipeline builder api
		auto& i                           = static_cast<le_pipeline_builder_api*>( api )->le_rtx_pipeline_builder_i;
		i.create                          = le_rtx_pipeline_builder_create;
		i.destroy                         = le_rtx_pipeline_builder_destroy;
		i.build                           = le_rtx_pipeline_builder_build;
		i.set_shader_group_ray_gen        = le_rtx_pipeline_builder_set_shader_group_ray_gen;
		i.add_shader_group_miss           = le_rtx_pipeline_builder_add_shader_group_miss;
		i.add_shader_group_callable       = le_rtx_pipeline_builder_add_shader_group_callable;
		i.add_shader_group_triangle_hit   = le_rtx_pipeline_builder_add_shader_group_triangle_hit;
		i.add_shader_group_procedural_hit = le_rtx_pipeline_builder_add_shader_group_procedural_hit;
	}

	{
		// setup shader module builder api
		auto& i                       = static_cast<le_pipeline_builder_api*>( api )->le_shader_module_builder_i;
		i.create                      = le_shader_module_builder_create;
		i.destroy                     = le_shader_module_builder_destroy;
		i.set_source_file_path        = le_shader_module_builder_set_source_file_path;
		i.set_source_defines_string   = le_shader_module_builder_set_source_defines_string;
		i.set_shader_stage            = le_shader_module_builder_set_shader_stage;
		i.set_source_language         = le_shader_module_builder_set_source_language;
		i.set_specialization_constant = le_shader_module_builder_set_specialization_constant;
		i.set_handle                  = le_shader_module_builder_set_handle;
		i.build                       = le_shader_module_builder_build;
	}
}
