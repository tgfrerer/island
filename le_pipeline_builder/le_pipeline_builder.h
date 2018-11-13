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

struct LeColorComponentFlags;

namespace le {
enum class PrimitiveTopology : uint32_t;
enum class BlendOp : uint32_t;
enum class BlendFactor : uint32_t;
enum class AttachmentBlendPreset : uint32_t;
} // namespace le

void register_le_pipeline_builder_api( void *api );

// clang-format off
struct le_graphics_pipeline_builder_api {
	static constexpr auto id      = "le_pipeline_builder";
	static constexpr auto pRegFun = register_le_pipeline_builder_api;

	struct le_graphics_pipeline_builder_interface_t {

		le_graphics_pipeline_builder_o * ( * create          ) ( le_pipeline_manager_o *pipeline_cache ); // TODO: needs to be created for a backend.
		void                             ( * destroy         ) ( le_graphics_pipeline_builder_o* self );

		void     ( * add_shader_stage                        ) ( le_graphics_pipeline_builder_o* self,  le_shader_module_o* shaderStage);

		void     ( * set_vertex_input_attribute_descriptions ) ( le_graphics_pipeline_builder_o* self, le_vertex_input_attribute_description* p_input_attribute_descriptions, size_t count);
		void     ( * set_vertex_input_binding_descriptions   ) ( le_graphics_pipeline_builder_o* self, le_vertex_input_binding_description* p_input_binding_descriptions, size_t count);

		void     ( * set_rasterization_info                  ) ( le_graphics_pipeline_builder_o* self, const VkPipelineRasterizationStateCreateInfo& rasterizationState);
		void     ( * set_input_assembly_info                 ) ( le_graphics_pipeline_builder_o *self, const VkPipelineInputAssemblyStateCreateInfo &inputAssemblyInfo ) ;
		void     ( * set_tessellation_info                   ) ( le_graphics_pipeline_builder_o *self, const VkPipelineTessellationStateCreateInfo &tessellationInfo );
		void     ( * set_multisample_info                    ) ( le_graphics_pipeline_builder_o *self, const VkPipelineMultisampleStateCreateInfo &multisampleInfo );
		void     ( * set_depth_stencil_info                  ) ( le_graphics_pipeline_builder_o *self, const VkPipelineDepthStencilStateCreateInfo &depthStencilInfo );

		uint64_t ( * build                                   ) ( le_graphics_pipeline_builder_o* self );

		struct input_assembly_state_t {
			void ( *set_primitive_restart_enable ) ( le_graphics_pipeline_builder_o* self, uint32_t const& primitiveRestartEnable );
			void ( *set_topology                 ) ( le_graphics_pipeline_builder_o* self, le::PrimitiveTopology const & topology);
		};

		struct blend_attachment_state_t{
			void (*set_color_blend_op         )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendOp &blendOp );
			void (*set_alpha_blend_op         )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendOp &blendOp );
			void (*set_src_color_blend_factor )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor );
			void (*set_dst_color_blend_factor )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor );
			void (*set_src_alpha_blend_factor )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor );
			void (*set_dst_alpha_blend_factor )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::BlendFactor &blendFactor );
			void (*set_color_write_mask       )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const LeColorComponentFlags &write_mask );
			void (*use_preset                 )( le_graphics_pipeline_builder_o *self, size_t which_attachment, const le::AttachmentBlendPreset &preset );
		};

		struct tessellation_state_t{
			void (*set_patch_control_points)(le_graphics_pipeline_builder_o *self, uint32_t count);
		};

		input_assembly_state_t   input_assembly_state_i;
		blend_attachment_state_t blend_attachment_state_i;
		tessellation_state_t     tessellation_state_i;
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

class LeGraphicsPipelineBuilder;

class LeGraphicsPipelineBuilder : NoCopy, NoMove {

	le_graphics_pipeline_builder_o *self;

	class InputAssembly {
		LeGraphicsPipelineBuilder &parent;

	  public:
		InputAssembly( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		InputAssembly &setPrimitiveRestartEnable( uint32_t const &primitiveRestartEnable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.input_assembly_state_i.set_primitive_restart_enable( parent.self, primitiveRestartEnable );
			return *this;
		}

		InputAssembly &setToplogy( le::PrimitiveTopology const &topology ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.input_assembly_state_i.set_topology( parent.self, topology );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	InputAssembly mInputAssembly{*this};

	class TessellationState {
		LeGraphicsPipelineBuilder &parent;

	  public:
		TessellationState( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		TessellationState &setPatchControlPoints( uint32_t const &count ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.tessellation_state_i.set_patch_control_points( parent.self, count );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	TessellationState mTessellationState{*this};

	class AttachmentBlendState {
		LeGraphicsPipelineBuilder &parent;
		size_t                     index;

	  public:
		AttachmentBlendState( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		AttachmentBlendState &setColorBlendOp( const le::BlendOp &blendOp ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.set_color_blend_op( parent.self, index, blendOp );
			return *this;
		}

		AttachmentBlendState &setAlphaBlendOp( const le::BlendOp &blendOp ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.set_alpha_blend_op( parent.self, index, blendOp );
			return *this;
		}

		AttachmentBlendState &setSrcColorBlendFactor( const le::BlendFactor &blendFactor ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.set_src_color_blend_factor( parent.self, index, blendFactor );
			return *this;
		}

		AttachmentBlendState &setDstColorBlendFactor( const le::BlendFactor &blendFactor ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.set_dst_color_blend_factor( parent.self, index, blendFactor );
			return *this;
		}

		AttachmentBlendState &setSrcAlphaBlendFactor( const le::BlendFactor &blendFactor ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.set_src_alpha_blend_factor( parent.self, index, blendFactor );
			return *this;
		}

		AttachmentBlendState &setDstAlphaBlendFactor( const le::BlendFactor &blendFactor ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.set_dst_alpha_blend_factor( parent.self, index, blendFactor );
			return *this;
		}

		AttachmentBlendState &setColorWriteMask( const LeColorComponentFlags &write_mask ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.set_color_write_mask( parent.self, index, write_mask );
			return *this;
		}

		AttachmentBlendState &usePreset( const le::AttachmentBlendPreset &preset ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.blend_attachment_state_i.use_preset( parent.self, index, preset );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}

		friend class LeGraphicsPipelineBuilder;
	};

	AttachmentBlendState mAttachmentBlendState{*this};

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

	LeGraphicsPipelineBuilder &addShaderStage( le_shader_module_o *shaderModule ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.add_shader_stage( self, shaderModule );
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

	InputAssembly &withInputAssembly() {
		return mInputAssembly;
	}

	AttachmentBlendState &withAttachmentBlendState( uint32_t attachmentIndex = 0 ) {
		mAttachmentBlendState.index = attachmentIndex;
		return mAttachmentBlendState;
	}
};

#endif // __cplusplus

#endif
