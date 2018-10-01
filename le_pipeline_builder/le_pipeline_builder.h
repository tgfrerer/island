#ifndef GUARD_le_graphics_pipeline_builder_H
#define GUARD_le_graphics_pipeline_builder_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_graphics_pipeline_builder_o;
struct le_shader_module_o;
struct le_backend_o;

struct VkVertexInputAttributeDescription;
struct VkVertexInputBindingDescription;

void register_le_pipeline_builder_api( void *api );

// clang-format off
struct le_graphics_pipeline_builder_api {
	static constexpr auto id      = "le_pipeline_builder";
	static constexpr auto pRegFun = register_le_pipeline_builder_api;

	struct le_graphics_pipeline_builder_interface_t {

		le_graphics_pipeline_builder_o * ( * create                   ) ( le_backend_o *backend ); // TODO: needs to be created for a backend.
		void                             ( * destroy                  ) ( le_graphics_pipeline_builder_o* self );

		void                             ( * set_vertex_shader   ) ( le_graphics_pipeline_builder_o* self,  le_shader_module_o* vertex_shader);
		void                             ( * set_fragment_shader ) ( le_graphics_pipeline_builder_o* self,  le_shader_module_o* fragment_shader);

		void                             ( * set_vertex_input_attribute_descriptions )(le_graphics_pipeline_builder_o* self, VkVertexInputAttributeDescription* p_input_attribute_descriptions, size_t count);
		void                             ( * set_vertex_input_binding_descriptions   )(le_graphics_pipeline_builder_o* self, VkVertexInputBindingDescription* p_input_binding_descriptions, size_t count);

		uint64_t                         ( * build )(le_graphics_pipeline_builder_o* self );
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
	LeGraphicsPipelineBuilder( le_backend_o *backend )
	    : self( le_pipeline_builder::le_graphics_pipeline_builder_i.create( backend ) ) {
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

	LeGraphicsPipelineBuilder &setVertexInputAttributeDescriptions( VkVertexInputAttributeDescription *pDescr, size_t count ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_vertex_input_attribute_descriptions( self, pDescr, count );
		return *this;
	}

	LeGraphicsPipelineBuilder &setVertexInputBindingDescriptions( VkVertexInputBindingDescription *pDescr, size_t count ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_vertex_input_binding_descriptions( self, pDescr, count );
		return *this;
	}
};

#endif // __cplusplus

#endif
