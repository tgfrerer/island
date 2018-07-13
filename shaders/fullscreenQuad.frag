#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0;


vec4 getCircle(in vec2 t){

	float dist = length(t - vec2(0.5)); // distance will be at max 0.5
	dist *= 2; // scale dist to 0..1 range
	float circle_smoothness = 0.002;
	float circle_radius = 1;
	float circle = 1. - smoothstep( circle_radius - circle_smoothness, circle_radius, dist);

	return vec4(vec3(1), circle);
}


void main(){

	ivec2 textureSize2d = textureSize(src_tex_unit_0,0);
	float aspectX = float(textureSize2d.x)/float(textureSize2d.y);

	outFragColor = texture(src_tex_unit_0, inTexCoord.xy);
	//outFragColor = vec4(inTexCoord.xy, 0, 1);
	outFragColor *= getCircle(inTexCoord.xy*vec2(aspectX,1)-vec2(aspectX*0.15,0));
}
