#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 mvp;
	vec4 colour;
};

void main(){
	outFragColor = colour;
}