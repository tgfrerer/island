#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

// uniforms
layout (push_constant) uniform Params
{
	vec2 u_mouse;
	vec2 u_resolution;
	float u_time;
};

void main(){

	vec2 aspect_ratio = vec2((u_resolution.x / u_resolution.y), 1);

	vec2 st = inTexCoord.xy;
	vec2 m  = u_mouse;

	// Scale by aspect ratio in case we encounter a non-square canvas
	st *= aspect_ratio;
	m  *= aspect_ratio;

	float dist_to_mouse = distance( st, m );

	float highlight = smoothstep( 1-(51/u_resolution.x), 1-(50/u_resolution.x), 1 - dist_to_mouse );

	vec3 color = vec3( inTexCoord.xy, 1 * abs(sin(u_time)) );
	color     += vec3( highlight * 0.5 );

	outFragColor = vec4( color, 1 );
}