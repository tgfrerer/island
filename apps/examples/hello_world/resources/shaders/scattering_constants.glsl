// scattering constants - these shared over all scattering shaders.

#define EARTH_RADIUS 6360

// -----
const float TWO_PI = 6.28318530717959;
const float PI = 3.141592653589793;

const float mieConstG = -0.999;			// mie phase constant G. negative value means more light is scattered in *forward* direction. default value: -0.75 to -0.99. setting to 1 or -1 cancels out phase equations. don't.
//note that we use a positive value here, since for the sky dome we're interested in light that is scattered in the backward direction.
// const vec3 v3InvWavelength = pow(vec3( 0.420, 0.530 , 0.620 ), vec3(-4));	// 1 / pow(wavelength, 4) for the red, green, and blue channels

// TODO: animate v3InvWavelength
const vec3 v3InvWavelength = pow(vec3( 0.650, 0.550 , 0.440 ), vec3(-4));	// 1 / pow(wavelength, 4) for the red, green, and blue channels

const float fOuterRadius = EARTH_RADIUS * 1.025;		// The outer (atmosphere) radius
const float fOuterRadius2 = EARTH_RADIUS * 1.025 * EARTH_RADIUS * 1.025;	// fOuterRadius^2
const float fInnerRadius = EARTH_RADIUS;		// The inner (planetary) radius
const float fInnerRadius2 = EARTH_RADIUS*EARTH_RADIUS;	// fInnerRadius^2

const float Kr = 0.001783;
const float Km = 0.00107;
const float ESun = 42.8;

const float fKrESun = Kr * ESun;		// Kr * ESun
const float fKmESun = Km * ESun;		// Km * ESun
const float fKr4PI = Kr * PI * 4;			// Kr * 4 * PI
const float fKm4PI = Km * PI * 4;			// Km * 4 * PI
const float fScale = 1.f/159.f;			// 1 / (fOuterRadius - fInnerRadius)
const float fScaleDepth = 0.25;		// The scale depth (i.e. the altitude at which the atmosphere's average density is found). default value: 0.25
const float fScaleOverScaleDepth = (1.f/159.f) / 0.25;	// fScale / fScaleDepth

const int nSamples = 4;
const float fSamples = 4.0;

const float hdrExposure = 1.61828;
const vec3 screenGamma = vec3(1, 1, 1); // neutral gamma setting


	// // we initialise the light intensity by adding some background light - the light that 
	// // gets in-scattered through the sun being below the horizon. we calculate it's 
	// // power by looking at the angle epsilon.

	// vec3 sunDir = normalize(worldCentre.xyz - light1.xyz); // < assume paralell rays
	// vec3 worldToVertexDir = normalize(vertex.position - worldCentre.xyz);
	// // note that we calculate the sun backlight cosAngle (epsilon) using a cross product. 
	// // we want to get an angle which complements the angle (sunDir, worldToVertexDir) to PI/2.
	// // we're therefore interested in cos(PI/2 - x)
	// // since cos (PI/2 - x) === sin(x), we can use sin to represent this angle. 
	// // since the length of the cross product of two vectors is sinus of their angle, 
	// // we use the cross product to calculate.
	// float epsilon = clamp(length(cross(sunDir, worldToVertexDir)), -PI,  PI);
	// float backlightTerm = pow(max(epsilon, 0.0) ,  20.2);

// -----
// O'Neill's scale function, valid for atmosphere width = radius * 0.025
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
void getScattering(const in vec3 vertexInEyeSpace, inout vec3 colourRayleigh_, inout vec3 colourMie_, inout float fDistance )
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

	// distance calculation is correct!
	fDistance = (hitsEarth && t3 > 0) ?  (t3 - t1) : (t2 - t1);

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
		float fScatter = -dynamicOffset + offsetScale * fStartOffset + fDepth * (scale(fLightAngle));  // calculate scattering based on lookups for light, eye angles

		v3Attenuate = exp(- fScatter * (v3InvWavelength * fKr4PI + fKm4PI)); // calculate attenuation, taking into account different colour wavelengths' respones
		
		lightIntensity   += v3Attenuate * (fDepth * fScaledLength ); // 
		v3SamplePoint    += v3SampleRay; // move one line segment along the ray
	}

	 colourRayleigh_ = lightIntensity * (v3InvWavelength * fKrESun);
	 colourMie_      = lightIntensity *  fKmESun ;

}