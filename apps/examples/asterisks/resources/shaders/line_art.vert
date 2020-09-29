#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;

// outputs 
layout (location = 0) out VertexData {
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

	outData.vertexColor = vec4(1);

	mat4 offset_mat = mat4(1);

	// We draw everything 5 times, so that we draw elements which fall across the edge 
	// correctly: these need to be drawn on both sides of the edge.

	int x = (gl_InstanceIndex % 2) * (gl_InstanceIndex - 2);        //  0, -1, 0, 1, 0
	int y = ((gl_InstanceIndex+1) % 2) * (gl_InstanceIndex / 2 -1); // -1,  0, 0, 0, 1

	offset_mat[3] = vec4(x * 640, 480 * y, 0, 1);

	vec4 position = projectionMatrix * viewMatrix * modelMatrix * offset_mat * vec4(pos,1);

	gl_Position = position;
}
