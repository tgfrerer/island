#include "le_backend_vk.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include <vulkan/vulkan.hpp>

#include <vector>

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

	std::vector<vk::VertexInputAttributeDescription> explicitVertexAttributeDescriptions;    // only used if contains values, otherwise use from vertex shader reflection
	std::vector<vk::VertexInputBindingDescription>   explicitVertexInputBindingDescriptions; // only used if contains values, otherwise use from vertex shader reflection
};
