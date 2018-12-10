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

		float intensity = max(0,1-vertex.distanceToBorder);
		 
		if (intensity < EPSILON ) discard;

		float gradient = 1.0;

		vec2 texPos = 2.0 * (vertex.texcoord.xy - vec2(0.5));
		float texDistance = length(texPos) + EPSILON;
		vec2 nVec = vec2(1,0);

		float cosPhi = dot(texPos, nVec) / texDistance ;
		
		if (cosPhi < EPSILON) discard;

		vec3 chroma = vec3( 
			pow(sin(map(texDistance+0.0, 0.2, 1.4, 0, PI)), 2.0)*0.2 ,
			pow(sin(map(texDistance+0.27, 0.25, 1.4, 0, PI)), 3.0)*0.26 ,
			pow(sin(map(texDistance+0.5, 0.4, 1.4, 0, PI)), 2.3) *0.4
			);

		gradient = map(acos(cosPhi - EPSILON), 0, PI, 0.75, 0.5);
		gradient *= map(cosPhi,0.65,1,0,1); // narrow arc 

		// outer ring feather
		const float blurwidth = 0.3;
		float ringIntensity = (1-smoothstep(0.9-blurwidth*0.25, 0.9+blurwidth*0.25, texDistance)); //< narrowness of border blur
		// inner ring feather
		ringIntensity *= (smoothstep(0.5-blurwidth*0.5, 0.5+blurwidth*0.5, texDistance)); //< narrowness of border blur
		intensity = ringIntensity * intensity;

		if (intensity < EPSILON ) discard;
		
		 // fragColor = vec4(vec3(1) * intensity, 1);
		fragColor = vec4(chroma * gradient * intensity * vertex.intensity, 1);

	} else if (vertex.flare_type == 2){

		// multi-iris lens flares.

		// discard;
		float intensity = vertex.distanceToBorder;
		intensity =  max(1, 1.0 - abs(0.5 - smoothstep(0.1, 0.8, intensity))  * 2 );

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

		// sun glare
		
		if (uHowClose < 100) discard; ///< do not render if sun is behind earth. 

		vec2 p = (mod(vertex.texcoord.xy*4,vec2(2,2)) - vec2(1)) * -sign(vertex.texcoord.xy-vec2(0.5)); // range -1,1 

		float pL = length(p);
		
		float r = pL;
		
		float intensity =  min(10,max(0, pow(max(p.x*1,0.0),23) + pow(max(p.y,0),44) ));
		intensity *= (p.x+0.75);
		intensity *= (p.y+0.75);

		vec2 cVec = vertex.texcoord.xy * 2.0 - vec2(1);
		
		float gradient = vertex.intensity - 0.5 * length( cVec ); // intensity mask for glare, conrolled by vertex.intensity
		gradient = smoothstep(0.6,0.7,gradient);
		
		intensity = (intensity * gradient );

		intensity *= 0.25 * vertex.intensity;

		if (intensity < EPSILON) discard;

		vec3 chroma = vec3(180,230,255) * TO_RGB;
		
		fragColor = vec4(vertex.distanceToBorder * min(chroma * intensity, vec3(1,1,1)) , 1);
		// fragColor = vec4(attenuation*vec3(1),1);

	} else if (vertex.flare_type == 4){

		// Sun highlight
		float intensity = 1.0 ; 
		float attenuation = smoothstep(0,0.8,max(0,1.0 - length(vertex.texcoord.xy * 2.0 - vec2(1))));

		// do not render if not really contributing to image.
		if (attenuation < EPSILON ) discard;

		vec3 color = vec3(0.9,0.9,0.7) * pow(attenuation,7);

		fragColor = vec4(min(vec3(1), color * 2.0 * pow(vertex.intensity,4)), 1);
	
	} else {

		// discard;
		// Glow dot
		float intensity = 1.0 ; //abs( 0.5 - fract( length(vertex.texcoord.xy * 2.0 - vec2(1)) / 0.2));
		float attenuation = pow(1.0 - length(vertex.texcoord.xy * 2.0 - vec2(1)),3);

		// do not render if not really contributing to image.
		if (attenuation < EPSILON ) discard;

		vec3 gradient = (intensity * attenuation).xxx;
		vec3 color = gradient * vec3(0.9,0.9,0.7)*0.5;

		fragColor = vec4(color * 1.0 * vertex.intensity, 1);
	}
} else {
	fragColor = vec4(vertex.texcoord.xy,0,1);
	//fragColor = vec4(1);
}

}

