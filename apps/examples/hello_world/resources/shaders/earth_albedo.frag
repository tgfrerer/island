#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define EPSILON 0.0001f

layout (early_fragment_tests) in;

// inputs 
layout (location = 0) in VertexData {
	vec4 position;
	vec3 normal;
	vec2 texCoord;
	vec3 tangent;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform CameraParams 
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (set=1, binding = 0) uniform ModelParams
{
	mat4 modelMatrix;
	vec4 sunInEyeSpace;
	vec4 worldCentreInEyeSpace;
};

layout (set = 1, binding = 1) uniform sampler2D tex_unit_0;
layout (set = 1, binding = 2) uniform sampler2D tex_unit_1;
layout (set = 1, binding = 3) uniform sampler2D tex_unit_2;
layout (set = 1, binding = 4) uniform sampler2D tex_clouds;

struct Albedo {
	// inputs
	vec3 V; // view vector
	vec3 L; // light vector
	vec3 N; // normal vector
	// outputs
	vec3 H; // half vector
	vec3 R; // reflected light vector
	float diffuse;
	float specularR; // based on reflect
	float specularH; // based on half vector
};

// ----------------------------------------------------------------------

#include "scattering_constants.glsl"

// ----------------------------------------------------------------------

void calculateLighting(inout Albedo b){

	b.H = normalize(b.V + b.L );
	b.R = reflect( -b.L, b.N);

	b.diffuse   = max( dot(b.L, b.N), 0.f);
	b.specularH = max( dot(b.N, b.H) , 0.f);
	b.specularR = max( dot(b.R, b.V) , 0.f);
}

bool raySphereIntersect(in vec3 ray_, in vec3 rayOrigin_, in vec4 sphere_, inout float t1, inout float t2, inout float m2, inout float s){

	float r = sphere_.w;
	vec3  l = sphere_.xyz - rayOrigin_ ;  
	s = dot(l, ray_);
	
	m2 = dot(l, l) - s*s; ///< "m squared", this is the (midpoint distance to sphere centre) squared 

	if (m2 > r*r) return false;

	float q = sqrt(r*r - m2);

	t1 = (s - q); // close intersection
	t2 = (s + q); // far   intersection

	return true;
}

// ----------------------------------------------------------------------


void main(){


	const vec3 tangent  = normalize(inData.tangent);
	vec3 normal   = normalize(inData.normal);
	const vec3 biNormal = cross(normal,tangent);
	
	// we store the cloud normal now, because cloud might have a separate bump map,
	// and normal will change.
	vec3 cloudNormal = normal; 
	
	// Tangent space is a space where the: 
	// x-axis is formed by tangent,
	// y-axis is formed by biNormal,
	// z-axis is formed by normal
	mat3 tangentSpace = mat3(tangent, biNormal, normal);

	// we only transform our normal into tangent space if 
	// tangent and normal are not co-planar, otherwise tangent space is not defined.
	if (dot(biNormal,biNormal) > EPSILON )
	{
		vec3 bumpMap = texture(tex_unit_1, inData.texCoord).rgb;
		vec3 bumpNormal = 2 * (bumpMap - vec3(0.5)); // map 0..1 to -1..1
		normal = tangentSpace * bumpNormal; // transform bumpNormal into tangent space, and store in normal
	}

	//vec3 L = normalize(sunInEyeSpace.xyz); // parallel rays to distant sun, we don't care about the origin point that much;

	// calculate specular + diffuse light terms.	
	Albedo albedo;
	Albedo cloudAlbedo;
	albedo.N = normal;
	albedo.V = normalize(-inData.position.xyz); // (negative view ray direction) : ray from sample point to camera
	albedo.L = normalize(sunInEyeSpace.xyz - inData.position.xyz);

	calculateLighting(albedo);	
	
	cloudAlbedo = albedo;
	cloudAlbedo.N = cloudNormal;

	calculateLighting(cloudAlbedo);

	vec2 cloudCoords = inData.texCoord;
	{

		vec3  uEyeRay = normalize( inData.position ).xyz;	// ray from camera to vertex, now unit length, and World Space
		
		float t1, t2, t3, t4, cA, cB, s1, s2;
		bool hitsAtmo  = raySphereIntersect( uEyeRay, vec3(0), vec4(worldCentreInEyeSpace.xyz, mix(fInnerRadius,fOuterRadius, 0.75) ) , t1, t2, cA, s1 );

		t1 = min(t1, t2);

		vec3 worldToHitPoint = worldCentreInEyeSpace.xyz -( vec3(0) + uEyeRay * t1 );

		mat3 normalMatrix = mat3(viewMatrix * modelMatrix);
		vec3 vU =  normalize(worldToHitPoint * normalMatrix);
			
		// Note that atan is not defined for vU.z == 0
		// we must add a mod operator to remove visual artifacts.
		float aPhi =  mod(atan(vU.x, vU.z ) / TWO_PI + 0.75 + 1, 1);
		float aTheta = (acos(vU.y)) / PI ;
		
		if ( hitsAtmo ){
			cloudCoords = vec2(aPhi,aTheta);
		}

	}

	// night on earth is when diffuse would turn negative
	float nightOnEarth = max(0, -dot(albedo.L, albedo.N));

	vec3 daySample        = texture(tex_unit_0, inData.texCoord).rgb;
	float cloudBrightness = texture(tex_clouds, cloudCoords).r;
	vec3 nightLights      = vec3(texture(tex_unit_2, inData.texCoord, -0.3 + cloudBrightness * 5.f).r);

	// we're mixing the clouds on top of the ground texture. 
	// we're using different light for ground and clouds. 
	vec3 daySide   = mix( daySample * (albedo.diffuse + 0.2 * albedo.specularR), vec3(1) * (cloudAlbedo.diffuse + 0.2 * cloudAlbedo.specularR), cloudBrightness);
	vec3 nightSide = vec3(1,0.5,0.2) * nightLights + cloudBrightness * vec3(0.15) * vec3(0.7,0.4,0.2);

	vec3 outColor = vec3(1);
	
	outColor = mix( daySide, nightSide, nightOnEarth );
	// outColor.rgb += cloudBrightness * albedo.diffuse;
	
	outFragColor = vec4(outColor,1);
	// outFragColor = vec4(inData.texCoord, 0, 1);
	// outFragColor = vec4((bumpNormal * 0.5 + vec3(0.5)),1);
	// outFragColor = inData.color;
}