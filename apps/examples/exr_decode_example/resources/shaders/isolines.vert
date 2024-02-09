#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable


// inputs 
layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;


// outputs 
layout (location = 0) out VertexData {
	vec2 texCoord;
	vec4 worldPos;
} outData;


// arguments
layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};
layout (set=0, binding=1) uniform sampler2D tex_unit_0; 

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};


void main() 
{

	outData.texCoord = uv;

	vec4 position = vec4(pos,1);
	position.y += texture(tex_unit_0,uv).x * -500;

	outData.worldPos = position;

	position = projectionMatrix * viewMatrix * modelMatrix * position;

	gl_Position = position;
}
