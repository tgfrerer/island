#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec3 pos;
layout (location = 1) in vec4 col;

// outputs 
layout (location = 0) out VertexData {
	vec2 bezControl;
	vec4 texColor;
} outData;


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
};


void main() 
{

	// creates control points: (0,0); (1,1); (0.5,0) for indices 0,1,2
	outData.bezControl = 0.5 * vec2((gl_VertexIndex%3 << 1) % 3, (gl_VertexIndex%3 << 1) & 2);
	outData.texColor = col;

	int numRows = 10;

	mat4 instanceTranslate = mat4(
		vec4(1,0,0,0),
		vec4(0,1,0,0),
		vec4(0,0,1,0),
		vec4(600 * ((gl_InstanceIndex%10)-0.5*(numRows-1)),
			 600 * ((gl_InstanceIndex/10)-0.5*(numRows-1)),0,1));

	vec4 position = projectionMatrix * viewMatrix * instanceTranslate* modelMatrix * vec4(pos,1);
	position.y = -position.y;

	gl_Position = position;
}
