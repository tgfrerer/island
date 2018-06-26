#version 420 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform DefaultMatrices 
{
	mat4 modelViewProjectionMatrix;
};

layout (set = 0, binding = 2) uniform sampler2D tex_unit_0[3];

#include "include_test.glsl"

void main(){
	outFragColor = getColor(inTexCoord);
}