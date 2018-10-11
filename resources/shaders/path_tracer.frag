#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;
// outputs
layout (location = 0) out vec4 outFragColor;

#include "analytical_ops.glsl"

// arguments
layout (set = 0, binding = 0) uniform MatrixStack 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (set = 0, binding = 1) uniform  RayInfo 
{
	vec3 rayTL;
	vec3 rayTR;
	vec3 rayBL;
	vec3 rayBR;
	vec3 eye;
	vec2 clipNearFar;
};

#include "lighting.glsl"
#include "noise.glsl"

// parallel sun ray 
const vec3 sun_ray = normalize(vec3(0, 0.2, -.05));
const vec3 sun_pos = sun_ray * 1000;
const vec3 v3InvWavelength = pow(vec3( 0.650, 0.550 , 0.440 ), vec3(-4));

#include "scattering.glsl"		// scattering calculations 

// P        -> eye origin 
// w        -> ray direction
// L_o      -> radiance outgoing from scene point in direction w
// distance -> (input: start-distance, then output: end-distance) to scene
bool trace_ray(in vec3 P, in vec3 w, inout float distance, int maxIterations){
	
	const float maxDistance = 1e10;
	const float closeEnough = 1e-1;
	
	float t   = 0.f;
	float dt  = 0.f;

	for (int i=0; i < maxIterations; ++i){
		
		float old_dt = dt;
		dt = scene_distance(P + w * t); // how far is the scene away from a point at P + w*t

		t += dt *.707;

		if (dt < closeEnough){
			distance = t;
			//< use this if you want to return steepness 
			//L_o = vec3(i/float(maxIterations));	
			return true; 
		} 
		else if (t > distance){
			// too far, there is some known closer intersection
			return false;
		}
	};

	return false;
}

float mod289(float x){return x - floor(x * (1.0 / 289.0)) * 289.0;}
vec4 mod289(vec4 x){return x - floor(x * (1.0 / 289.0)) * 289.0;}
vec4 perm(vec4 x){return mod289(((x * 34.0) + 1.0) * x);}

float noise3(vec3 p){
    vec3 a = floor(p);
    vec3 d = p - a;
    d = d * d * (3.0 - 2.0 * d);

    vec4 b = a.xxyy + vec4(0.0, 1.0, 0.0, 1.0);
    vec4 k1 = perm(b.xyxy);
    vec4 k2 = perm(k1.xyxy + b.zzww);

    vec4 c = k2 + a.zzzz;
    vec4 k3 = perm(c);
    vec4 k4 = perm(c + 1.0);

    vec4 o1 = fract(k3 * (1.0 / 41.0));
    vec4 o2 = fract(k4 * (1.0 / 41.0));

    vec4 o3 = o2 * d.z + o1 * (1.0 - d.z);
    vec2 o4 = o3.yw * d.x + o3.xz * (1.0 - d.x);

    return o4.y * d.y + o4.x * (1.0 - d.y);
}

void main(){
	
	// ALL CALCULATIONS IN WORLD SPACE!

	// we use bilinear interpolation between the frustum edge rays to get 
	// to the direction of our ray, based on the pixel coordinate.  
	// we can do this because the near plane is guaranteed to be co-planar,
	// i.e., all points sit on the same plane (the near plane).
	// See R.Buss, 3-D Computer Graphics, 2003 [p.108] 
	
	vec3 dir = mix(
		mix(rayBL, rayTL, inTexCoord.y), 
		mix(rayBR, rayTR, inTexCoord.y),
		inTexCoord.x 
		);


	// after interpolation, we need to make sure to have the ray in unit length
	// dir = normalize(dir);

	vec3 sampleColor = vec3(0);
	float distance = 1e4;

	vec2 t = vec2(0);
	vec3 boxMin = vec3(-100,-150,-100)*1.5;
	vec3 boxMax = vec3(100,250,100)*1.5;

	float zDepth = distance;

	float alpha_density = 0; // we use this to calculate coverage for a fragment

	// t.y is far  intersection
	// t.x is near intersection
	if (box_intersect(boxMin, boxMax, eye, dir, t) && (t.y < zDepth || t.x < zDepth) ){
		// we intersect, and we're inside the box.

		// raydistance is the distance the ray travels through the box,
		// capped if it hits terrain on its way.
		float t_far = min(zDepth, t.y);
		float t_near = max(t.x, 0);
		float raydistance = (t_far - t_near);// * noise3( ((dir*t_far) * (dir*t_near)) * 0.00003 );

		// distance the ray travels along the eye ray through the box * the avereage density along the ray
		
		float averageDensity = 0.015;
		float opticalDepth = 0;

		opticalDepth = raydistance*0.00125;

		// mie scattering
		//sampleColor += (1.0-smoothstep(0,zDepth,t.y)) * ( FMiePhase(dot(sun_ray,dir), 0.993) * opticalDepth)  * ( 0.2 );
		
		// raleigh scattering
		vec3 c = ( FRaleighPhase(dot(sun_ray, dir))) * opticalDepth * (v3InvWavelength * 0.14 + 1.2 ) ;
		
		sampleColor += c;
		alpha_density += dot(c, vec3(1));
	}

	vec3 hdrColor;
	{
		const vec3 gamma = vec3(1.3);
		const vec3 shift = vec3(9);
		const vec3 scale = vec3(8);

		hdrColor = vec3(1) / (vec3(1) + pow(gamma, shift - scale * sampleColor ));
	} 
	outFragColor = vec4(hdrColor, alpha_density );
	// outFragColor = vec4(rayTR*0.5+vec3(0.5),  1.0);
	//outFragColor = vec4(dir.xyz * 0.5 + vec3(0.5),  1.0);
	// outFragColor = vec4(ray.color,1);
	// outFragColor = vec4(inTexCoord.xy, 0 , 1.0);
	 // outFragColor = vec4(sampleColor,1);
}