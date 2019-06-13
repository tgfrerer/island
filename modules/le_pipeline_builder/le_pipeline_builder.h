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
struct le_gpso_handle_t; // Opaque handle for graphics pipeline state

struct le_compute_pipeline_builder_o;
struct le_cpso_handle_t; // opaque handle for compute pipeline state

struct le_vertex_input_binding_description;
struct le_vertex_input_attribute_description;
struct VkPipelineMultisampleStateCreateInfo;
struct VkPipelineDepthStencilStateCreateInfo;

struct LeColorComponentFlags;

namespace le {
enum class PrimitiveTopology : uint32_t;
enum class BlendOp : uint32_t;
enum class BlendFactor : uint32_t;
enum class AttachmentBlendPreset : uint32_t;
enum class PolygonMode : uint32_t;
enum class FrontFace : uint32_t;
enum class CullModeFlagBits : uint32_t;
enum class SampleCountFlagBits : uint32_t;
enum class CompareOp : uint32_t;
enum class StencilOp : uint32_t;
} // namespace le

void register_le_pipeline_builder_api( void *api );

// clang-format off
struct le_pipeline_builder_api {
	static constexpr auto id      = "le_pipeline_builder";
	static constexpr auto pRegFun = register_le_pipeline_builder_api;

	struct le_graphics_pipeline_builder_interface_t {

		le_graphics_pipeline_builder_o * ( * create          ) ( le_pipeline_manager_o *pipeline_cache );
		void                             ( * destroy         ) ( le_graphics_pipeline_builder_o* self );

		void     ( * add_shader_stage                        ) ( le_graphics_pipeline_builder_o* self,  le_shader_module_o* shaderStage);

		void     ( * set_vertex_input_attribute_descriptions ) ( le_graphics_pipeline_builder_o* self, le_vertex_input_attribute_description* p_input_attribute_descriptions, size_t count);
		void     ( * set_vertex_input_binding_descriptions   ) ( le_graphics_pipeline_builder_o* self, le_vertex_input_binding_description* p_input_binding_descriptions, size_t count);

		void     ( * set_multisample_info                    ) ( le_graphics_pipeline_builder_o *self, const VkPipelineMultisampleStateCreateInfo &multisampleInfo );
		void     ( * set_depth_stencil_info                  ) ( le_graphics_pipeline_builder_o *self, const VkPipelineDepthStencilStateCreateInfo &depthStencilInfo );

		le_gpso_handle_t* ( * build             ) ( le_graphics_pipeline_builder_o* self );

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

		struct rasterization_state_t{
			void (*set_depth_clamp_enable         )(le_graphics_pipeline_builder_o *self, bool const & enable);
			void (*set_rasterizer_discard_enable  )(le_graphics_pipeline_builder_o *self, bool const & enable);
			void (*set_polygon_mode               )(le_graphics_pipeline_builder_o *self, le::PolygonMode const & polygon_mode);
			void (*set_cull_mode                  )(le_graphics_pipeline_builder_o *self, le::CullModeFlagBits const & cull_mode_flag_bits);
			void (*set_front_face                 )(le_graphics_pipeline_builder_o *self, le::FrontFace const & front_face);
			void (*set_depth_bias_enable          )(le_graphics_pipeline_builder_o *self, bool const & enable);
			void (*set_depth_bias_constant_factor )(le_graphics_pipeline_builder_o *self, float const & factor);
			void (*set_depth_bias_clamp           )(le_graphics_pipeline_builder_o *self, float const & clamp);
			void (*set_depth_bias_slope_factor    )(le_graphics_pipeline_builder_o *self, float const & factor);
			void (*set_line_width                 )(le_graphics_pipeline_builder_o *self, float const & line_width);
		};

		struct multisample_state_t{
			void (*set_rasterization_samples    )(le_graphics_pipeline_builder_o *self, le::SampleCountFlagBits const & num_samples);
			void (*set_sample_shading_enable    )(le_graphics_pipeline_builder_o *self, bool const & enable);
			void (*set_min_sample_shading       )(le_graphics_pipeline_builder_o *self, float const & min_sample_shading);
			void (*set_alpha_to_coverage_enable )(le_graphics_pipeline_builder_o *self, bool const& enable);
			void (*set_alpha_to_one_enable      )(le_graphics_pipeline_builder_o *self, bool const & enable);
		};

		struct stencil_op_state_t{
			void (*set_fail_op       )(le_graphics_pipeline_builder_o *self, le::StencilOp const & op);
			void (*set_pass_op       )(le_graphics_pipeline_builder_o *self, le::StencilOp const & op);
			void (*set_depth_fail_op )(le_graphics_pipeline_builder_o *self, le::StencilOp const & op);
			void (*set_compare_op    )(le_graphics_pipeline_builder_o *self, le::CompareOp const & op);
			void (*set_compare_mask  )(le_graphics_pipeline_builder_o *self, uint32_t const &mask);
			void (*set_write_mask    )(le_graphics_pipeline_builder_o *self, uint32_t const & mask);
			void (*set_reference     )(le_graphics_pipeline_builder_o *self, uint32_t const & reference);
		};

		struct depth_stencil_state_t {
			void (*set_depth_test_enable        )(le_graphics_pipeline_builder_o *self, bool const & enable);
			void (*set_depth_write_enable       )(le_graphics_pipeline_builder_o *self, bool const& enable);
			void (*set_depth_compare_op         )(le_graphics_pipeline_builder_o *self, le::CompareOp const & compare_op);
			void (*set_depth_bounds_test_enable )(le_graphics_pipeline_builder_o *self, bool const & enable);
			void (*set_stencil_test_enable      )(le_graphics_pipeline_builder_o *self, bool const& enable);
			void (*set_min_depth_bounds         )(le_graphics_pipeline_builder_o *self, float const & min_bounds);
			void (*set_max_depth_bounds         )(le_graphics_pipeline_builder_o *self, float const & max_bounds);
		};

		input_assembly_state_t   input_assembly_state_i;
		blend_attachment_state_t blend_attachment_state_i;
		tessellation_state_t     tessellation_state_i;
		rasterization_state_t    rasterization_state_i;
		multisample_state_t      multisample_state_i;
		stencil_op_state_t       stencil_op_state_front_i;
		stencil_op_state_t       stencil_op_state_back_i;
		depth_stencil_state_t    depth_stencil_state_i;
	};

	le_graphics_pipeline_builder_interface_t le_graphics_pipeline_builder_i;

	// ---------- Compute Pipeline Builder is much simpler, as there are fewer parameters to set

	struct le_compute_pipeline_builder_interface_t {
		le_compute_pipeline_builder_o * ( * create           ) ( le_pipeline_manager_o *pipeline_cache );
		void                            ( * destroy          ) ( le_compute_pipeline_builder_o* self );
		void                            ( * set_shader_stage ) ( le_compute_pipeline_builder_o* self,  le_shader_module_o* shaderStage);
		le_cpso_handle_t*               ( * build            ) ( le_compute_pipeline_builder_o* self );
	};

	le_compute_pipeline_builder_interface_t le_compute_pipeline_builder_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

// ----------------------------------------------------------------------

namespace le_pipeline_builder {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_pipeline_builder_api>( true );
#	else
const auto api = Registry::addApiStatic<le_pipeline_builder_api>();
#	endif

static const auto &le_graphics_pipeline_builder_i = api -> le_graphics_pipeline_builder_i;
static const auto &le_compute_pipeline_builder_i  = api -> le_compute_pipeline_builder_i;

} // namespace le_pipeline_builder

// ----------------------------------------------------------------------

class LeComputePipelineBuilder : NoCopy, NoMove {

	le_compute_pipeline_builder_o *self;

  public:
	LeComputePipelineBuilder( le_pipeline_manager_o *pipelineCache )
	    : self( le_pipeline_builder::le_compute_pipeline_builder_i.create( pipelineCache ) ) {
	}

	~LeComputePipelineBuilder() {
		le_pipeline_builder::le_compute_pipeline_builder_i.destroy( self );
	}

	le_cpso_handle_t *build() {
		return le_pipeline_builder::le_compute_pipeline_builder_i.build( self );
	}

	LeComputePipelineBuilder &setShaderStage( le_shader_module_o *shaderModule ) {
		le_pipeline_builder::le_compute_pipeline_builder_i.set_shader_stage( self, shaderModule );
		return *this;
	}
};

// ----------------------------------------------------------------------

class LeGraphicsPipelineBuilder;

class LeGraphicsPipelineBuilder : NoCopy, NoMove {

	le_graphics_pipeline_builder_o *self;

	class InputAssemblyState {
		LeGraphicsPipelineBuilder &parent;

	  public:
		InputAssemblyState( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		InputAssemblyState &setPrimitiveRestartEnable( uint32_t const &primitiveRestartEnable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.input_assembly_state_i.set_primitive_restart_enable( parent.self, primitiveRestartEnable );
			return *this;
		}

        InputAssemblyState &setTopology( le::PrimitiveTopology const &topology ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.input_assembly_state_i.set_topology( parent.self, topology );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	InputAssemblyState mInputAssembly{*this};

	class DepthStencilState {
		LeGraphicsPipelineBuilder &parent;

	  public:
		DepthStencilState( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		DepthStencilState &setDepthTestEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.depth_stencil_state_i.set_depth_test_enable( parent.self, enable );
			return *this;
		}

		DepthStencilState &setDepthWriteEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.depth_stencil_state_i.set_depth_write_enable( parent.self, enable );
			return *this;
		}

		DepthStencilState &setDepthCompareOp( le::CompareOp const &compare_op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.depth_stencil_state_i.set_depth_compare_op( parent.self, compare_op );
			return *this;
		}

		DepthStencilState &setDepthBoundsTestEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.depth_stencil_state_i.set_depth_bounds_test_enable( parent.self, enable );
			return *this;
		}

		DepthStencilState &setStencilTestEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.depth_stencil_state_i.set_stencil_test_enable( parent.self, enable );
			return *this;
		}

		DepthStencilState &setMinDepthBounds( float const &min_bounds ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.depth_stencil_state_i.set_min_depth_bounds( parent.self, min_bounds );
			return *this;
		}

		DepthStencilState &setMaxDepthBounds( float const &max_bounds ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.depth_stencil_state_i.set_max_depth_bounds( parent.self, max_bounds );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	DepthStencilState mDepthStencilState{*this};

	class DepthStencilOpFront {
		LeGraphicsPipelineBuilder &parent;

	  public:
		DepthStencilOpFront( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		DepthStencilOpFront &setFailOp( le::StencilOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_front_i.set_fail_op( parent.self, op );
			return *this;
		}

		DepthStencilOpFront &setPassOp( le::StencilOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_front_i.set_pass_op( parent.self, op );
			return *this;
		}

		DepthStencilOpFront &setDepthFailOp( le::StencilOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_front_i.set_depth_fail_op( parent.self, op );
			return *this;
		}

		DepthStencilOpFront &setCompareOp( le::CompareOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_front_i.set_compare_op( parent.self, op );
			return *this;
		}

		DepthStencilOpFront &setCompareMask( uint32_t const &mask ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_front_i.set_compare_mask( parent.self, mask );
			return *this;
		}

		DepthStencilOpFront &setWriteMask( uint32_t const &mask ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_front_i.set_write_mask( parent.self, mask );
			return *this;
		}

		DepthStencilOpFront &setReference( uint32_t const &reference ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_front_i.set_reference( parent.self, reference );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	DepthStencilOpFront mDepthStencilOpFront{*this};

	class DepthStencilOpBack {
		LeGraphicsPipelineBuilder &parent;

	  public:
		DepthStencilOpBack( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		DepthStencilOpBack &setFailOp( le::StencilOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_back_i.set_fail_op( parent.self, op );
			return *this;
		}

		DepthStencilOpBack &setPassOp( le::StencilOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_back_i.set_pass_op( parent.self, op );
			return *this;
		}

		DepthStencilOpBack &setDepthFailOp( le::StencilOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_back_i.set_depth_fail_op( parent.self, op );
			return *this;
		}

		DepthStencilOpBack &setCompareOp( le::CompareOp const &op ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_back_i.set_compare_op( parent.self, op );
			return *this;
		}

		DepthStencilOpBack &setCompareMask( uint32_t const &mask ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_back_i.set_compare_mask( parent.self, mask );
			return *this;
		}

		DepthStencilOpBack &setWriteMask( uint32_t const &mask ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_back_i.set_write_mask( parent.self, mask );
			return *this;
		}

		DepthStencilOpBack &setReference( uint32_t const &reference ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.stencil_op_state_back_i.set_reference( parent.self, reference );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	DepthStencilOpBack mDepthStencilOpBack{*this};

	class MultiSampleState {
		LeGraphicsPipelineBuilder &parent;

	  public:
		MultiSampleState( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		MultiSampleState &setRasterizationSamples( le::SampleCountFlagBits const &num_samples ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.multisample_state_i.set_rasterization_samples( parent.self, num_samples );
			return *this;
		}

		MultiSampleState &setSampleShadingEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.multisample_state_i.set_sample_shading_enable( parent.self, enable );
			return *this;
		}

		MultiSampleState &setMinSampleShading( float const &min_sample_shading ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.multisample_state_i.set_min_sample_shading( parent.self, min_sample_shading );
			return *this;
		}

		MultiSampleState &setAlphaToCoverageEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.multisample_state_i.set_alpha_to_coverage_enable( parent.self, enable );
			return *this;
		}

		MultiSampleState &setAlphaToOneEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.multisample_state_i.set_alpha_to_one_enable( parent.self, enable );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	MultiSampleState mMultiSampleState{*this};

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

	class RasterizationState {
		LeGraphicsPipelineBuilder &parent;

	  public:
		RasterizationState( LeGraphicsPipelineBuilder &parent_ )
		    : parent( parent_ ) {
		}

		RasterizationState &setDepthClampEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_depth_clamp_enable( parent.self, enable );
			return *this;
		}

		RasterizationState &setRasterizerDiscardEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_rasterizer_discard_enable( parent.self, enable );
			return *this;
		}

		RasterizationState &setPolygonMode( le::PolygonMode const &mode ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_polygon_mode( parent.self, mode );
			return *this;
		}

		RasterizationState &setCullMode( le::CullModeFlagBits const &mode ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_cull_mode( parent.self, mode );
			return *this;
		}

		RasterizationState &setFrontFace( le::FrontFace const &frontFace ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_front_face( parent.self, frontFace );
			return *this;
		}

		RasterizationState &setDepthBiasEnable( bool const &enable ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_depth_bias_enable( parent.self, enable );
			return *this;
		}

		RasterizationState &setDepthBiasConstantFactor( float const &factor ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_depth_bias_constant_factor( parent.self, factor );
			return *this;
		}

		RasterizationState &setDepthBiasClamp( float const &clamp ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_depth_bias_clamp( parent.self, clamp );
			return *this;
		}

		RasterizationState &setDepthBiasSlopeFactor( float const &factor ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_depth_bias_slope_factor( parent.self, factor );
			return *this;
		}

		RasterizationState &setLineWidth( float const &lineWidth ) {
			using namespace le_pipeline_builder;
			le_graphics_pipeline_builder_i.rasterization_state_i.set_line_width( parent.self, lineWidth );
			return *this;
		}

		LeGraphicsPipelineBuilder &end() {
			return parent;
		}
	};

	RasterizationState mRasterizationState{*this};

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

	le_gpso_handle_t *build() {
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

	LeGraphicsPipelineBuilder &setMultisampleInfo( const VkPipelineMultisampleStateCreateInfo &info ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_multisample_info( self, info );
		return *this;
	}

	LeGraphicsPipelineBuilder &setDepthStencilInfo( const VkPipelineDepthStencilStateCreateInfo &info ) {
		le_pipeline_builder::le_graphics_pipeline_builder_i.set_depth_stencil_info( self, info );
		return *this;
	}

	InputAssemblyState &withInputAssemblyState() {
		return mInputAssembly;
	}

	RasterizationState &withRasterizationState() {
		return mRasterizationState;
	}

	TessellationState &withTessellationState() {
		return mTessellationState;
	}

	MultiSampleState &withMultiSampleState() {
		return mMultiSampleState;
	}

	DepthStencilState &withDepthStencilState() {
		return mDepthStencilState;
	}

	DepthStencilOpBack &withDepthStencilOpBack() {
		return mDepthStencilOpBack;
	}

	DepthStencilOpFront &withDepthStencilOpFront() {
		return mDepthStencilOpFront;
	}

	AttachmentBlendState &withAttachmentBlendState( uint32_t attachmentIndex = 0 ) {
		mAttachmentBlendState.index = attachmentIndex;
		return mAttachmentBlendState;
	}
};

#endif // __cplusplus

#endif