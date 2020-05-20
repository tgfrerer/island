#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in VertexData {
	vec2 texCoord;
	vec4 color;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

void main(){
	
	// outFragColor = vec4(inData.texCoord, 0, 1);
	outFragColor = inData.color;
}