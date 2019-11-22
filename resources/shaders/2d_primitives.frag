#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

// inputs 
layout (location = 0) in vec4 inColor;
layout (location = 1) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

void main()
{
	outFragColor = inColor.abgr ;
	//* texture( tex_unit_0, inTexCoord.st);
	// outFragColor = vec4(1) ;//* texture( tex_unit_0, inTexCoord.st);
	// outFragColor = vec4(inTexCoord.xy, 0,1);//* texture( tex_unit_0, inTexCoord.st);
}
