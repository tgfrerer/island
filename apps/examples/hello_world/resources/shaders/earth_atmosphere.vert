#version 450 core

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
	vec4 worldCentreInEyeSpace;
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
	
	mat3 normalMatrix = transpose(inverse(mat3(viewMatrix) * mat3(modelMatrix)));
	outData.normal  = normalMatrix * normal;

	gl_Position = projectionMatrix * outData.position;
}
