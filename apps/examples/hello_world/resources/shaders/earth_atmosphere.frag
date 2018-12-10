#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// we activate this from version 420 onwards, so that the fragment shader
// is not even run, should depth test fail.
layout (early_fragment_tests) in;

// SET 0 --------------------------------------------------UNIFORM-INPUTS

layout (set = 0, binding = 0) uniform CameraParams 
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

// SET 1 --------------------------------------------------UNIFORM-INPUTS

layout (set=1, binding = 0) uniform ModelParams
{
	mat4 modelMatrix;
	vec4 sunInEyeSpace;
	vec4 worldCentreInEyeSpace;
};

// layout (set = 1, binding = 1) uniform sampler2D tex_clouds;

// ---------------------------------------------------------VERTEX-INPUTS

layout (location = 0) in VertexData {
	vec4 position;
	vec3 normal;
	vec2 texCoord;
} inData;

// --------------------------------------------------------VERTEX-OUTPUTS

layout (location = 0) out vec4 outFragColor;

// ----------------------------------------------------------------------

#include "scattering_constants.glsl"



// ----------------------------------------------------------------------

struct BlinnPhong {
	vec3 V; // view vector
	vec3 L; // light vector
	vec3 N; // normal vector
	vec3 H; // half vector
	vec3 R; // reflected light vector
	float diffuse;
	float specularR; // based on reflect
	float specularH; // based on half vector
};

// ----------------------------------------------------------------------

void calculateLighting(inout BlinnPhong b){

	b.H = normalize(b.V + b.L );
	b.R = reflect( - b.L, b.N);

	b.diffuse = max( dot( b.L, b.N), 0.f);
	b.specularH = max( dot(b.N, b.H) , 0.f);
	b.specularR = max( dot(b.R, b.V) , 0.f);
}

// ------------------------------------------------------------------MAIN

void main(){

	vec3 colourRayleigh = vec3(0);
	vec3 colourMie = vec3(0);

	float fThickness = 0;

	getScattering(inData.position.xyz, colourRayleigh, colourMie, fThickness);

	vec3 uLightDirection = normalize(sunInEyeSpace.xyz - worldCentreInEyeSpace.xyz); // unit direction from sample point(= vertex) to light. 

	vec3 normal   = normalize(inData.normal);

	vec3 L = normalize(sunInEyeSpace.xyz); // parallel rays to distant sun, we don't care about the origin point that much;

	// calculate specular + diffuse light terms.	
	BlinnPhong Atmosphere;
	Atmosphere.V = normalize(-inData.position.xyz); // (negative view ray direction) : ray from sample point to camera
	Atmosphere.N = normal;
	Atmosphere.L = normalize(sunInEyeSpace.xyz - inData.position.xyz);

	calculateLighting(Atmosphere);

	float nightOnEarth = max(0, -dot(Atmosphere.L, Atmosphere.N));
	
	float cosPhi = dot(Atmosphere.V, uLightDirection);
	

	const float mieConstGAtmo = -0.92;
	vec3 scatteredLightColour = vec3(0);

	scatteredLightColour +=  10.0 * colourMie * FMiePhase(cosPhi, mieConstGAtmo); // it's not the phase functions
	scatteredLightColour +=  0.6 * colourRayleigh * FRaleighPhase(cosPhi);


	vec3 outColor;
	outColor = scatteredLightColour + fThickness * vec3(0.04,00.02,0.0) * 0.001;

	// vec3 cloudSample = texture(tex_clouds, inData.texCoord).rrr;
	// outColor += cloudSample * (Atmosphere.diffuse +  Atmosphere.specularR);
	// outColor += -0.3 * cloudSample * FMiePhase(dot(Atmosphere.L,-Atmosphere.N),mieConstGAtmo);

	outFragColor = vec4(outColor,1);
	// outFragColor = vec4(vec3(1) * nightOnEarth, 1);
	// outColor = vec3(1) * (Atmosphere.diffuse + 0.2 * pow(Atmosphere.specularH, 23)) ;
	// outFragColor = vec4((bumpNormal * 0.5 + vec3(0.5)),1);
	// outFragColor = inData.color;
}