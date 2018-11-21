#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 texCoord;

// outputs 
layout (location = 0) out VertexData {
	vec4 position;
	vec3 normal;
	vec2 texCoord;
} outData;


// arguments
layout (set = 0, binding = 0) uniform CameraParams 
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (set=1, binding = 0) uniform ModelParams
{
	mat4 modelMatrix;
	vec4 sunInEyeSpace;
};

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};


void main() 
{
	outData.position =  viewMatrix * modelMatrix * vec4(pos,1);
	outData.texCoord = texCoord;	
	
	vec4 normal = transpose(inverse(viewMatrix * modelMatrix)) * vec4(normal,1);
	outData.normal = normal.xyz;

	gl_Position = projectionMatrix * outData.position;
}
