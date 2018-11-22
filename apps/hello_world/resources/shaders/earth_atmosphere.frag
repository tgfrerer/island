#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

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

// -----
// O'Neill scale function, valid for atmosphere width = radius * 0.025
// the constants in this function are valid for scaleHeight = 0.25 and 
// an atmosphere to earth radius ratio of 1.025 : 1 
// -----

float scale(float fCos)
{
	float x = 1.0 - fCos;
	return fScaleDepth * exp(-0.00287 + x*(0.459 + x*(3.83 + x*(-6.80 + x*5.25))));
}


// ----------------------------------------------------------------------

float FRaleighPhase(float cosPhi){
	return (1+cosPhi*cosPhi) * 0.75;
}

// ----------------------------------------------------------------------

float FMiePhase(float cosPhi, float g){
	float g2 = g*g;
	float sunScale = 0.5;
	return 3 * ((1.0 - g2) / (2.0 + g2)) * (1.0 + cosPhi*cosPhi) / pow(1.0 + g2 - 2.0*g*cosPhi, sunScale);
}

// ----------------------------------------------------------------------
/// \brief 		Perform sphere / ray intersection. 
/// \note 		we're following the naming of variables as in Real-Time Rendering, 3rd ed. pp. 741
/// \param		sphere_ 	given as (xyz,r)
/// \param		ray_ 		assumed to be normalized
bool raySphereIntersect(in vec3 ray_, in vec3 rayOrigin_,in vec4 sphere_, inout float t1, inout float t2, inout float m2, inout float s){

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
void getScattering(const in vec3 vertexInEyeSpace, inout vec3 colourRayleigh_, inout vec3 colourMie_ )
{
	
	// note: we do all calculations in eye = camera space, 
	// whilst the original o'neill paper uses world space.

	// see your paper notes from november 11, 2013
	// ----------
	
	vec3  uEyeRay = normalize(vertexInEyeSpace);	// ray from camera to vertex, now unit length
	
	float t1, t2, t3, t4, cA, cB, s1, s2;
	bool hitsAtmo  = raySphereIntersect( uEyeRay, vec3(0), vec4(worldCentreInEyeSpace.xyz, fOuterRadius) , t1, t2, cA, s1 );
	 //bool hitsEarth = true;
	bool hitsEarth = raySphereIntersect( uEyeRay, vec3(0), vec4(worldCentreInEyeSpace.xyz, fInnerRadius) , t3, t4, cB, s2 ); 

	t1 = max(t1, 0);

	// distance caluclation is correct!
	float fDistance = (hitsEarth && t3 > 0) ?  (t3 - t1) : (t2 - t1);

	vec3  v3Start = vec3(0) + uEyeRay * t1; // camera pos == origin in eye space
	vec3  uEyeVertical = normalize(v3Start-worldCentreInEyeSpace.xyz); // vector from world centre to camera

	float fCamHeight = length(worldCentreInEyeSpace.xyz) ;    // how high above the world centre is our camera?
	float fDepth = exp(fScaleOverScaleDepth * (fInnerRadius - fCamHeight)); // fCamHeight is the camera height


	float fStartAngle = dot(normalize(sunInEyeSpace.xyz - v3Start), uEyeVertical); // calculate angle between eye ray and camera vertical, divide by height for lookup-table == scale function
	// float fStartOffset = scale(fStartAngle) *  (1.0 - smoothstep(fInnerRadius , fOuterRadius, fCamHeight ));
	float fStartOffset = scale(fStartAngle) * exp( fScaleOverScaleDepth * (fInnerRadius - length(v3Start-worldCentreInEyeSpace.xyz)) );

	// -------------------- initialise scattering loop variables

	float fSampleLength = float(fDistance / fSamples); // total length divided in equal steps, by default 5.
	float fScaledLength = fSampleLength * fScale; // length step divided by total atmosphere thickness

	vec3 v3SampleRay = uEyeRay * fSampleLength; // get first sample segment, with length samplelength in direction of ray 
	vec3 v3SamplePoint = v3Start; // move sample segment to start position, get point down the ray, halfway into first sample segment

	// -------------------- now loop though the sample rays 

	vec3 lightIntensity = vec3(0.0) ;

	vec3 v3Attenuate;
	float depthAccumulate = fDepth;

	for (int i=0; i < nSamples; i++){

		vec3 uLightDir = normalize(sunInEyeSpace.xyz - v3SamplePoint); // unit direction from sample point to light . 
	
		vec3  v3SampleVertical = v3SamplePoint - worldCentreInEyeSpace.xyz;  // ray from world centre to current sample pos
		float fHeight = length(v3SampleVertical);			       // height over worldCentre of the current sample pos
		vec3  uSampleVertical = v3SampleVertical / fHeight;        // unit direction of current sample pos

		fDepth = exp(fScaleOverScaleDepth * -(fHeight - fInnerRadius)); // fHeight is the current sample height
		depthAccumulate += fDepth;

		float fLightAngle = dot(uSampleVertical, uLightDir );  	 // since we're using unit vectors, we don't have to divide by length to get the cosPhi angle through a dot product.

		float dynamicOffset = 1* (-0.5 + smoothstep(fInnerRadius-fOuterRadius, fOuterRadius - fInnerRadius, fCamHeight - fHeight ));
		float offsetScale = smoothstep(fInnerRadius, fOuterRadius, fHeight );
		dynamicOffset = max(dynamicOffset,0);
		// max(dynamicOffset,0) is to get rid of blue blotsches.
		float fScatter   =  -dynamicOffset + offsetScale * fStartOffset + fDepth * (scale(fLightAngle));  // calculate scattering based on lookups for light, eye angles

		v3Attenuate = exp(- fScatter * (v3InvWavelength * fKr4PI + fKm4PI)); // calculate attenuation, taking into account different colour wavelengths' respones

		lightIntensity   += v3Attenuate * (fDepth * fScaledLength ); // 
		v3SamplePoint    += v3SampleRay; // move one line segment along the ray
	}

	 colourRayleigh_ = lightIntensity * (v3InvWavelength * fKrESun);
	 colourMie_      = lightIntensity *  fKmESun ;

}

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

	getScattering(inData.position.xyz, colourRayleigh, colourMie);

	vec3 uLightDirection = normalize(sunInEyeSpace.xyz - worldCentreInEyeSpace.xyz); // unit direction from sample point(= vertex) to light. 

	vec3 normal   = normalize(inData.normal);

	vec3 L = normalize(sunInEyeSpace.xyz); // parallel rays to distant sun, we don't care about the origin point that much;

	// calculate specular + diffuse light terms.	
	BlinnPhong Atmosphere;
	Atmosphere.V = normalize(-inData.position.xyz); // (negative view ray direction) : ray from sample point to camera
	Atmosphere.N = normal;
	Atmosphere.L = normalize(sunInEyeSpace.xyz - inData.position.xyz);

	calculateLighting(Atmosphere);

	float cosPhi = dot(Atmosphere.V, uLightDirection); 

	const float mieConstGAtmo = -0.92;
	vec3 scatteredLightColour = vec3(0);

	scatteredLightColour +=  1 * colourMie * FMiePhase(cosPhi, mieConstGAtmo); // it's not the phase functions
	scatteredLightColour +=  0.8 * colourRayleigh * FRaleighPhase(cosPhi);

	float diffuse = 1.f;
	diffuse = dot(normal,L);
	
	vec3 outColor;
	// outColor = vec3(1) * (Atmosphere.diffuse + 0.2 * pow(Atmosphere.specularH, 23)) ;
	outColor = scatteredLightColour;
	outFragColor = vec4(outColor,1);
	// outFragColor = vec4((bumpNormal * 0.5 + vec3(0.5)),1);
	// outFragColor = inData.color;
}