#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;


// arguments
layout (set = 0, binding = 0) uniform MatrixStack 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
    float gl_PointSize;
};



void main() 
{
	vec4 position = projectionMatrix * viewMatrix * modelMatrix * vec4(pos,1);
	gl_PointSize = 2;
	gl_Position = position;
}
