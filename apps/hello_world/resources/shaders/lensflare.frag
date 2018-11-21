#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// -- uniforms 

layout (set=1, binding=0) uniform LensflareParams {
// uCanvas:
// .x -> global canvas height (in pixels) 
// .y -> global canvas width (in pixels)
// .z -> identity distance, that is the distance at which canvas is rendered 1:1
	vec3 uCanvas; 
	vec3 uLensflareSource; ///< source of flare in clip space
	float uHowClose;
};

// -- inputs

layout (location = 0) in VertexAttrib {
	vec3 position;
	vec2 texcoord;
	flat float distanceToBorder;
	flat int flare_type;
	flat float intensity; // intensity based on how much of the sun is visible
} vertex;

// -- outputs 

layout (location = 0) out vec4 fragColor;

// -- constants

const float TWO_PI = 6.28318530717959;
const float PI = 3.141592653589793;
const float ROOT_TWO = 1.4142135623730950488016887242097;
#define EPSILON 0.00001
const float TO_RGB = 1.f/255.f;

// --------

#include "rgb2hsv.glsl"
#include "math_utils.glsl"

// --------

void main(){
	
if ( true ) // fancy effects on / off 
{
	if (vertex.flare_type == 1){

		// rainbow lens flare

		float intensity = 1.0 - vertex.distanceToBorder;
		 // intensity = 1.0;
		if (intensity < EPSILON ) discard;

		float gradient = 1.0;

		vec2 texPos = 2.0 * (vertex.texcoord.xy - vec2(0.5));
		float texDistance = length(texPos);
		vec2 nVec = vec2(1,0);

		float cosPhi = dot(texPos, nVec) / texDistance ;
		
		//if (cosPhi < EPSILON) discard;

		vec3 chroma = vec3( 
			pow(sin(map(texDistance+0.0, 0.0, 1.0, 0, PI)), 3.0)*0.2 ,
			pow(sin(map(texDistance+0.3, 0.25, 1.0, 0, PI)), 3.0)*0.26 ,
			pow(sin(map(texDistance+0.5, 0.4, 1.0, 0, PI)), 2.3) *0.4
			);

		gradient = map(acos(cosPhi - EPSILON), 0, PI, 0.75, -0.2);
		gradient *= cosPhi;

		//gradient = 1;
		// outer ring feather
		intensity *= (1.0 - smoothstep(0.8, 0.98, texDistance)); //< narrowness of border blur
		// inner ring feather
		intensity *=  smoothstep(0.1-gradient*0.5, 0.65, texDistance);

		intensity *= 0.9;

		//intensity = 1;

		if (intensity < EPSILON ) discard;
		
		fragColor = vec4(chroma * gradient * intensity * vertex.intensity, 1);

	} else if (vertex.flare_type == 2){

		// multi-iris lens flares.

		// discard;
		float intensity = vertex.distanceToBorder;
		intensity =  1.0 - abs(0.5 - smoothstep(0.1, 0.8, intensity))  * 2 ;

		float gradient = 1.0;

		vec2 texPos = 2.0 * (vertex.texcoord.xy - vec2(0.5));
		vec2 nVec = vec2(1,0);

		float cosPhi = dot(normalize(texPos), nVec);

		if (cosPhi < EPSILON) discard;

		gradient = map(acos(cosPhi-EPSILON), 0, PI, 0.75, -0.2);
		
		gradient *=  cosPhi * cosPhi ;
		gradient = pow(gradient, 1.6) * 0.5; // spherical blur, central highlight

		intensity = intensity * (1.0 - smoothstep(0.76, 0.87, length(texPos))); //< narrowness of border blur

		if (intensity < EPSILON ) discard;
		vec3 chroma = vec3(64,230,255) * TO_RGB;

		fragColor = vec4(chroma * gradient * intensity * vertex.intensity * 1.1, 1);

	} else if (vertex.flare_type == 3){

		// discard;
		// white hot sun point flare
		
		// if (uHowClose > 100) discard; ///< do not render if sun is behind earth. 
									///< if the sun is unobstructed, uHowClose will be < 0

		float intensity = 1.0 ; //abs( 0.5 - fract( length(vertex.texcoord.xy * 2.0 - vec2(1)) / 0.2));
		float attenuation = 1.0 - length(vertex.texcoord.xy * 2.0 - vec2(1));
		//attenuation *= attenuation;
		// do not render if not really contributing to image.
		if (attenuation < EPSILON ) discard;
		attenuation *= attenuation + 0.4 * smoothstep(0.5, 0.8, attenuation);

		vec3 gradient = (intensity * attenuation).xxx;
		vec3 color = gradient * vec3(1.0);

		fragColor = vec4(color * 1.0 * vertex.intensity, 1);

	} else {

		// discard;
		float intensity = 1.0 ; //abs( 0.5 - fract( length(vertex.texcoord.xy * 2.0 - vec2(1)) / 0.2));
		float attenuation = pow(1.0 - length(vertex.texcoord.xy * 2.0 - vec2(1)),3);

		// do not render if not really contributing to image.
		if (attenuation < EPSILON ) discard;

		vec3 gradient = (intensity * attenuation).xxx;
		vec3 color = gradient * vec3(0.9,0.9,0.7);

		fragColor = vec4(color * 1.0 * vertex.intensity, 1);
	}
} else {

	discard;
	fragColor = vec4(vertex.texcoord.xy,0,1);
	//fragColor = vec4(1);
}

}

