#version 420 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

layout (set = 0, binding = 0) uniform MainColor 
{
	vec4 color;
};

layout (set = 0, binding = 1) uniform AnotherColor 
{
	vec4 color2;
};


#include "include_test.glsl"

void main(){
	// outFragColor = getColor(inTexCoord);
	outFragColor = color * color2;
}