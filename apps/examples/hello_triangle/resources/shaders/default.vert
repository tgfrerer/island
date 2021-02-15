#version 450 core

// This shader built after a technique introduced in:
// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;
layout (location = 1) in vec4 col;

// outputs 
layout (location = 0) out VertexData {
	vec2 texCoord;
	vec4 vertexColor;
} outData;


// arguments
layout (set = 0, binding = 0) uniform Mvp {
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex {
    vec4 gl_Position;
};



void main() {

	outData.texCoord    = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	outData.vertexColor = col;

	vec4 position = projectionMatrix * viewMatrix * modelMatrix * vec4(pos,1);

	gl_Position = position;
}
