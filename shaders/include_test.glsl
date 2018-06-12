#include "deep_include_test.glsl" // collect saturate

vec4 getColor(in vec2 t){

	float dist = length(t - vec2(0.5)); // distance will be at max 0.5
	dist *= 2; // scale dist to 0..1 range
	float circle_smoothness = 0.02;
	float circle_radius = 0.9;
	float circle = 1. - smoothstep( circle_radius - circle_smoothness, circle_radius, dist);

	return vec4(vec2(t) * (circle), 0, circle);
}
