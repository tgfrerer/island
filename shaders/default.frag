#version 420 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

layout (set = 0, binding = 0) uniform MatrixStack 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (set = 0, binding = 1) uniform Color 
{
	vec4 color;
};

#include "include_test.glsl"

void main(){
	// outFragColor = getColor(inTexCoord);
	outFragColor = vec4(inTexCoord, 0, 1);
	//outFragColor = color;
}