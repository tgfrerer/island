#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// vertex shader inputs
layout (location = 0) in vec2 inTexCoord;

// uniforms
layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0;
layout (set = 0, binding = 1) uniform sampler3D src_tex_unit_1;

layout (set = 1, binding = 0 ) uniform Params {
	float x_value;
};

// outputs
layout (location = 0) out vec4 outFragColor;

void main(){

	vec4 sampleColor = texture(src_tex_unit_0, inTexCoord.xy);
	vec4 gradedColor = texture(src_tex_unit_1, sampleColor.rgb);
	
	outFragColor = mix(sampleColor, gradedColor, step(inTexCoord.x, x_value));
}