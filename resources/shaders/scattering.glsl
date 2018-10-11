
// calculate ray distance in terms of how far a ray would 
// travel through the air box.

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

float scale(float fCos)
{
 float x = 1.0 - fCos;
 return 0.2 * (0.00287 + x*(0.459 + x*(3.83 + x*(-6.80 + x*5.25))));
}
