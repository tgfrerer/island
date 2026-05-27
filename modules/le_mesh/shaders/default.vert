#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;

// arguments
layout (set = 0, binding = 0) uniform Mvp {
	mat4 mvp;
	vec4 colour;
};

// ---------------------------------------------------------------------- 

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
	gl_Position = mvp * vec4(pos,1);
}
