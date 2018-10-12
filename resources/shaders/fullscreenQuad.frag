#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0;

void main(){

	outFragColor = texture(src_tex_unit_0, inTexCoord.xy);
	// outFragColor = vec4(inTexCoord.xy, 0, 1);
}
