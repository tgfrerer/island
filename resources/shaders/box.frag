#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in VertexData {
	vec4 pos;
	vec4 normal;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

layout (set = 0, binding = 0) uniform MatrixStack 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

void main(){
	
	outFragColor = vec4(inData.normal.xyz * 0.5 + vec3(0.5),1);
}