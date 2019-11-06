#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0;
layout (set = 0, binding = 1) uniform sampler2D src_tex_unit_1[5];

void main(){

	vec3 colorA = texture(src_tex_unit_0, inTexCoord.xy).rgb;
	vec3 colorB = 
		texture(src_tex_unit_1[0], inTexCoord.xy).rgb
		* texture(src_tex_unit_1[1], inTexCoord.xy).rgb
		* texture(src_tex_unit_1[2], inTexCoord.xy).rgb
		* texture(src_tex_unit_1[3], inTexCoord.xy).rgb
		* texture(src_tex_unit_1[4], inTexCoord.xy).rgb
		;

	outFragColor = vec4(colorA * colorB,1);

	outFragColor = vec4(mix(colorA, colorB, 1), 1);
	// outFragColor = vec4( colorA + colorB , 1);
	// outFragColor = vec4(inTexCoord.xy, 0, 1);
}
