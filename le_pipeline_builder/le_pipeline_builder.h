#ifndef GUARD_le_graphics_pipeline_builder_H
#define GUARD_le_graphics_pipeline_builder_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_graphics_pipeline_builder_o;
struct le_shader_module_o;
struct le_pipeline_manager_o;

struct le_vertex_input_binding_description;
struct le_vertex_input_attribute_description;
struct VkPipelineRasterizationStateCreateInfo;
struct VkPipelineInputAssemblyStateCreateInfo;
struct VkPipelineTessellationStateCreateInfo;
struct VkPipelineMultisampleStateCreateInfo;
struct VkPipelineDepthStencilStateCreateInfo;

void register_le_pipeline_builder_api( void *api );

// clang-format off
struct le_graphics_pipeline_builder_api {
	static constexpr auto id      = "le_pipeline_builder";
	static constexpr auto pRegFun = register_le_pipeline_builder_api;

	struct le_graphics_pipeline_builder_interface_t {

		le_graphics_pipeline_builder_o * ( * create          ) ( le_pipeline_manager_o *pipeline_cache ); // TODO: needs to be created for a backend.
		void                             ( * destroy         ) ( le_graphics_pipeline_builder_o* self );

		void     ( * set_vertex_shader                       ) ( le_graphics_pipeline_builder_o* self,  le_shader_module_o* vertex_shader);
		void     ( * set_fragment_shader                     ) ( le_graphics_pipeline_builder_o* self,  le_shader_module_o* fragment_shader);

		void     ( * set_vertex_input_attribute_descriptions ) ( le_graphics_pipeline_builder_o* self, le_vertex_input_attribute_description* p_input_attribute_descriptions, size_t count);
		void     ( * set_vertex_input_binding_descriptions   ) ( le_graphics_pipeline_builder_o* self, le_vertex_input_binding_description* p_input_binding_descriptions, size_t count);

		void     ( * set_rasterization_info                  ) ( le_graphics_pipeline_builder_o* self, const VkPipelineRasterizationStateCreateInfo& rasterizationState);
		void     ( * set_input_assembly_info                 ) ( le_graphics_pipeline_builder_o *self, const VkPipelineInputAssemblyStateCreateInfo &inputAssemblyInfo ) ;
		void     ( * set_tessellation_info                   ) ( le_graphics_pipeline_builder_o *self, const VkPipelineTessellationStateCreateInfo &tessellationInfo );
		void     ( * set_multisample_info                    ) ( le_graphics_pipeline_builder_o *self, const VkPipelineMultisampleStateCreateInfo &multisampleInfo );
		void     ( * set_depth_stencil_info                  ) ( le_graphics_pipeline_builder_o *self, const VkPipelineDepthStencilStateCreateInfo &depthStencilInfo );

		uint64_t ( * build                                   )(le_graphics_pipeline_builder_o* self );
	};

	le_graphics_pipeline_builder_interface_t le_graphics_pipeline_builder_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_pipeline_builder {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_graphics_pipeline_builder_api>( true );
#	else
const auto api = Registry::addApiStatic<le_graphics_pipeline_builder_api>();
#	endif

static const auto &le_graphics_pipeline_builder_i = api -> le_graphics_pipeline_builder_i;

} // namespace le_pipeline_builder

class LeGraphicsPipelineBuilder : NoCopy, NoMove {

	le_graphics_pipeline_builder_o *self;

  public:
	LeGraphicsPipelineBuilder( le_pipeline_manager_o *pipelineCache )
	    : self( le_pipeline_builder::le_graphics_pipeline_builder_i.create( pipelineCache ) ) {
	}

	~LeGraphicsPipelineBuilder() {
		le_pipeline_builder::le_graphics_pipeline_builder_i.destroy( self );
	}

	uint64_t build() {
		return le_pipeline_builder::le_graphics_pipeline_builder_i.build( self );
	}

	LeGraphicsPipelineBuilder &setVertexShader( le_shader_module_o *shaderModule ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_vertex_shader( self, shaderModule );
		return *this;
	}

	LeGraphicsPipelineBuilder &setFragmentShader( le_shader_module_o *shaderModule ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_fragment_shader( self, shaderModule );
		return *this;
	}

	LeGraphicsPipelineBuilder &setVertexInputAttributeDescriptions( le_vertex_input_attribute_description *pDescr, size_t count ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_vertex_input_attribute_descriptions( self, pDescr, count );
		return *this;
	}

	LeGraphicsPipelineBuilder &setVertexInputBindingDescriptions( le_vertex_input_binding_description *pDescr, size_t count ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_vertex_input_binding_descriptions( self, pDescr, count );
		return *this;
	}

	LeGraphicsPipelineBuilder &setRasterizationInfo( const VkPipelineRasterizationStateCreateInfo &info ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_rasterization_info( self, info );
		return *this;
	}

	LeGraphicsPipelineBuilder &setInputAssemblyInfo( const VkPipelineInputAssemblyStateCreateInfo &info ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_input_assembly_info( self, info );
		return *this;
	}

	LeGraphicsPipelineBuilder &setTessellationInfo( const VkPipelineTessellationStateCreateInfo &info ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_tessellation_info( self, info );
		return *this;
	}

	LeGraphicsPipelineBuilder &setMultisampleInfo( const VkPipelineMultisampleStateCreateInfo &info ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_multisample_info( self, info );
		return *this;
	}

	LeGraphicsPipelineBuilder &setDepthStencilInfo( const VkPipelineDepthStencilStateCreateInfo &info ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_depth_stencil_info( self, info );
		return *this;
	}
};

#endif // __cplusplus

#endif
