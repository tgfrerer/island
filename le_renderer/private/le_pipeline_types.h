#ifndef GUARD_LE_PIPELINE_TYPES_H
#define GUARD_LE_PIPELINE_TYPES_H

#include <stdint.h>

struct le_shader_module_o; // defiend in le_backend_vk.cpp

// NOTE: this should probably go to backend

struct le_graphics_pipeline_state_o {

	uint64_t hash = 0; ///< hash of pipeline state, but not including but shader modules

	le_shader_module_o *shaderModuleVert = nullptr;
	le_shader_module_o *shaderModuleFrag = nullptr;

	// TODO (pipeline) : -- add fields to pso object
	//struct le_vertex_input_binding_description *vertexInputBindingDescrition;
	//struct le_vertex_attribute_description *    vertexAttributeDescrition;
};

#endif
