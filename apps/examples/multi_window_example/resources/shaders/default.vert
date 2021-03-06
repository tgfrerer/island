#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 normal;
// layout (location = 2) in vec2 texCoord;
layout (location = 3) in vec4 colour;

// outputs 
layout (location = 0) out VertexData {
	vec4 position;
	vec3 normal;
	vec4 colour;
} outData;


// arguments
layout (set = 0, binding = 0) uniform MVP_Default 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};



void main() 
{
	outData.colour = colour;
	outData.position = viewMatrix * modelMatrix * vec4(pos,1);

	vec4 normal = transpose(inverse(viewMatrix * modelMatrix)) * vec4(normal,1);
	outData.normal = normal.xyz;

	gl_Position = projectionMatrix * outData.position;
}
