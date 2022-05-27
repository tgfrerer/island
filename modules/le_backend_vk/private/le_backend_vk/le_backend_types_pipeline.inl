// ----------------------------------------------------------------------

struct le_graphics_pipeline_builder_data {

	VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
	VkPipelineTessellationStateCreateInfo  tessellationState{};
	VkPipelineMultisampleStateCreateInfo   multisampleState{};
	VkPipelineDepthStencilStateCreateInfo  depthStencilState{};

	float                               blend_factor_constants[ 4 ]; // only used with blend factors referencing constant color|alpha
	VkPipelineColorBlendAttachmentState blendAttachmentStates[ LE_MAX_COLOR_ATTACHMENTS ];
};

struct graphics_pipeline_state_o {
	le_graphics_pipeline_builder_data data{};

	std::vector<le_shader_module_handle> shaderModules;        // non-owning; refers opaquely to shader modules (or not)
	std::vector<le::ShaderStage>         shaderStagePerModule; // refers to shader module handle of same index

	std::vector<le_vertex_input_attribute_description> explicitVertexAttributeDescriptions;    // only used if contains values, otherwise use from vertex shader reflection
	std::vector<le_vertex_input_binding_description>   explicitVertexInputBindingDescriptions; // only used if contains values, otherwise use from vertex shader reflection
};
struct compute_pipeline_state_o {
	le_shader_module_handle shaderStage; // non-owning; refers opaquely to a compute shader module (or not)
};

struct rtx_pipeline_state_o {
	std::vector<le_shader_module_handle>  shaderStages; // non-owning, refers to a number of shader modules.
	std::vector<le_rtx_shader_group_info> shaderGroups; // references shader modules from shaderStages by index.
};