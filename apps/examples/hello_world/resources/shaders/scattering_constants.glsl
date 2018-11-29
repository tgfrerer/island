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