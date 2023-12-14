#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 tex_coord;

// outputs 
layout (location = 0) out VertexData {
	vec2 texCoord;
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

	outData.texCoord = tex_coord;

	vec4 position = projectionMatrix * viewMatrix * modelMatrix * vec4(pos,1);

	gl_Position = position;
}
