#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// arguments
layout (set = 0, binding = 1) uniform GlobalColors 
{
	vec4 globalColor;
};

// outputs
layout (location = 0) out vec4 outFragColor;

void main(){
	outFragColor = globalColor;
}