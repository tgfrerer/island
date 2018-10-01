#include "le_backend_vk.h"

// NOTE: This header *must not* be included by anyone else but le_backend_vk.cpp or le_pipeline_builder.cpp.
//       Its sole purpose of being is to create a dependency inversion, so that both these compilation units
//       may share the same types for creating pipelines.

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include <vector>

#include "le_renderer/private/le_renderer_types.h" // for `le_vertex_input_attribute_description`, `le_vertex_input_binding_description`

struct le_graphics_pipeline_builder_data {

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{};
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	vk::PipelineTessellationStateCreateInfo  tessellationState{};
	vk::PipelineMultisampleStateCreateInfo   multisampleState{};
	vk::PipelineDepthStencilStateCreateInfo  depthStencilState{};

	std::array<vk::PipelineColorBlendAttachmentState, MAX_VULKAN_COLOR_ATTACHMENTS> blendAttachmentStates{};
};

struct graphics_pipeline_state_o {
	le_graphics_pipeline_builder_data data{};

	struct le_shader_module_o *shaderModuleVert = nullptr; // refers opaquely to a shader module (or not)
	struct le_shader_module_o *shaderModuleFrag = nullptr; // refers opaquely to a shader module (or not)

	std::vector<le_vertex_input_attribute_description> explicitVertexAttributeDescriptions;    // only used if contains values, otherwise use from vertex shader reflection
	std::vector<le_vertex_input_binding_description>   explicitVertexInputBindingDescriptions; // only used if contains values, otherwise use from vertex shader reflection
};
