#version 420 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// #include <test.glsl>

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;


#include "include_test.glsl"

void main(){
	outFragColor = getColor(inTexCoord);
}