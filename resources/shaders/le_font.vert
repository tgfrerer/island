#version 450 core

// This shader built after a technique introduced in:
// http://www.saschawillems.de/?page_id=2122

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec4 vertex;

// outputs 
layout (location = 0) out VertexData {
	vec2 texCoord;
} outData;

#ifdef NO_MVP

// arguments for without camera
layout (set = 0, binding = 0) uniform Extents
{
	vec4 screenExtents; // x,y,w,h
};

#else 

// arguments for rendering using 
layout (set = 0, binding = 0) uniform MVP 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

#endif

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};



void main() 
{
	outData.texCoord = vertex.zw;
	
	vec4 position;
#ifdef NO_MVP

	position = vec4((((screenExtents.xy+vertex.xy) * 2.f)/screenExtents.zw ), 0, 1);
#else
	position = projectionMatrix * viewMatrix * modelMatrix * vec4(vertex.xy, 0, 1);
#endif

	gl_Position = position;
}
