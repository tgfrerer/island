#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0[5];

layout (set = 1, binding = 0) uniform Params {
	float bloomStrength;
	float bloomRadius;
};


float bloomFactors[5] = {1.0, 0.8, 0.6, 0.4, 0.2 };
vec3 bloomTintColors[5] = {vec3(1),vec3(1),vec3(1),vec3(1),vec3(1)};

float lerpBloomFactor(const in float factor) { 
	float mirrorFactor = 1.2 - factor;
	return mix(factor, mirrorFactor, bloomRadius);
}

void main() {

	outFragColor = bloomStrength * ( 
		lerpBloomFactor(bloomFactors[0]) * vec4(bloomTintColors[0], 1.0) * texture(src_tex_unit_0[0], inTexCoord) + 
		lerpBloomFactor(bloomFactors[1]) * vec4(bloomTintColors[1], 1.0) * texture(src_tex_unit_0[1], inTexCoord) + 
		lerpBloomFactor(bloomFactors[2]) * vec4(bloomTintColors[2], 1.0) * texture(src_tex_unit_0[2], inTexCoord) + 
		lerpBloomFactor(bloomFactors[3]) * vec4(bloomTintColors[3], 1.0) * texture(src_tex_unit_0[3], inTexCoord) + 
		lerpBloomFactor(bloomFactors[4]) * vec4(bloomTintColors[4], 1.0) * texture(src_tex_unit_0[4], inTexCoord) 
	);
}