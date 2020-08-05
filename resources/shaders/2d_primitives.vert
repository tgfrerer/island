#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// uniforms (resources)
layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 mvp;
};

// inputs (vertex attributes)
layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inTexCoord;

layout (location = 2) in vec2  translation;
layout (location = 3) in vec2  scale;
layout (location = 4) in float rotation_ccw;
layout (location = 5) in uint  color;


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
	
	vec4 col = 
	vec4(
		((color>>24) & 0xff), 
		((color>>16) & 0xff),
		((color>>8) & 0xff),
		((color) & 0xff)) / 255.f;

	outColor = col;

	// apply translation from instance data

	mat4 transform = mat4(1);
	transform[3].xy = translation.xy;

	gl_Position = mvp * transform * vec4(inPos,0,1);
}