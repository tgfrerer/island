#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#ifndef BUF_W 
#define BUF_W 32
#endif

#ifndef BUF_H
#define BUF_H 32
#endif


// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

// uniforms
// layout (set = 0, binding = 1) uniform Params
// {
// 	vec2 u_mouse;
// 	vec2 u_resolution;
// 	float u_time;
// };


layout (set=0, binding = 0) buffer SortData 
{
	uint value[];
};


void main(){

	// vec2 aspect_ratio = vec2((u_resolution.x / u_resolution.y), 1);

	// vec2 st = inTexCoord.xy;
	// vec2 m  = u_mouse;

	// // Scale by aspect ratio in case we encounter a non-square canvas
	// st *= aspect_ratio;
	// m  *= aspect_ratio;

	// float dist_to_mouse = distance( st, m );

	// float highlight = smoothstep( 1-(51/u_resolution.x), 1-(50/u_resolution.x), 1 - dist_to_mouse );

	// vec3 color = vec3( inTexCoord.xy, 1 * abs(sin(u_time)) );
	// color     += vec3( highlight * 0.5 );

	vec4 color = vec4(1);
	ivec2 rect_coords = ivec2(floor(inTexCoord.xy * ivec2(BUF_W,BUF_H)));

	color.r = (value[rect_coords.y * BUF_W + rect_coords.x] & 255 ) / 255.f;
	color.g = ((value[rect_coords.y * BUF_W + rect_coords.x] >> 8) & 255  ) / 255.f;
	color.b = ((value[rect_coords.y * BUF_W + rect_coords.x] >> 16) & 255  ) / 255.f;
	// color.a = ((value[rect_coords.y * BUF_W + rect_coords.x] >> 24) & 255  ) / 255.f;

	outFragColor = vec4( color);
}