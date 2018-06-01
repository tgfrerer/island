#ifndef GUARD_LE_PIPELINE_TYPES_H
#define GUARD_LE_PIPELINE_TYPES_H

struct le_shader_module_o; // defiend in le_backend_vk.cpp

struct le_graphics_pipeline_state_o {

        // TODO (pipeline) : add fields to pso object

        le_shader_module_o *shaderModuleVert = nullptr;
        le_shader_module_o *shaderModuleFrag = nullptr;

	//struct le_vertex_input_binding_description *vertexInputBindingDescrition;
	//struct le_vertex_attribute_description *    vertexAttributeDescrition;
};



#endif
