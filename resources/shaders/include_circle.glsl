#ifndef INCLUDE_CIRCLE_GLSL
#define INCLUDE_CIRCLE_GLSL

float getCircle(in vec2 t, float circle_radius){

	float dist = length(t - vec2(0.5)); // distance will be at max 0.5
	dist *= 2; // scale dist to 0..1 range
	float circle_smoothness = circle_radius * 0.125;
	
	float circle = 1. - smoothstep( circle_radius - circle_smoothness, circle_radius, dist);

	return circle;
}

#endif