#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// uniforms (resources)
layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 modelViewProjectionMatrix;
};

// inputs (vertex attributes)
layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inTexCoord;

// outputs 
layout (location = 0) out vec4 outColor;
layout (location = 1) out vec2 outTexCoord;

// we override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
	outTexCoord = inTexCoord;
	outColor = vec4(1);
	gl_Position = modelViewProjectionMatrix * vec4(inPos,0,1);
}