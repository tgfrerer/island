#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;

// outputs 
layout (location = 0) out vec2 outTexCoord;

layout (set = 0, binding = 0) uniform DefaultMatrices 
{
	mat4 modelViewProjectionMatrix;
};

layout (set =0, binding = 2) uniform TestMatrix {
	mat4 testMatrix;
};

// we override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
	outTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	// vec4 test = vec4(1);
	//  test = test * modelViewProjectionMatrix;
	gl_Position = vec4(pos.xy * 2.0f + -1.0f, 0.f, 1.0f);
}
