// ------------------------------------------------------------
// Math
// ------------------------------------------------------------

float map(in float val_, in float range_min_, in float range_max_, in float min_, in float max_){
	return ( min_ + ((max_ - min_)/(range_max_ - range_min_)) * clamp(val_, range_min_, range_max_));
}

vec2 map(in vec2 val_, in vec2 range_min_, in vec2 range_max_, in vec2 min_, in vec2 max_){
	return ( min_ + ((max_ - min_)/(range_max_ - range_min_)) * clamp(val_, range_min_, range_max_));
}

vec3 map(in vec3 val_, in vec3 range_min_, in vec3 range_max_, in vec3 min_, in vec3 max_){
	return ( min_ + ((max_ - min_)/(range_max_ - range_min_)) * clamp(val_, range_min_, range_max_));
}

vec4 map(in vec4 val_, in vec4 range_min_, in vec4 range_max_, in vec4 min_, in vec4 max_){
	return ( min_ + ((max_ - min_)/(range_max_ - range_min_)) * clamp(val_, range_min_, range_max_));
}

// ---------

vec4 getQuaternionForRotation(	
	const in float angle, /// angle in radians 
	const in vec3 axis) /// axis as a normalised vec3
{
	vec4 _v;
	
	const float epsilon = 0.0000001;
	
	float length = length(axis);
	if (length < epsilon) {
		// ~zero length axis, so reset rotation to zero.
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	
	float inversenorm  = 1.0 / length;
	float coshalfangle = cos( 0.5 * angle );
	float sinhalfangle = sin( 0.5 * angle );
	
	_v.x = axis.x * sinhalfangle * inversenorm;
	_v.y = axis.y * sinhalfangle * inversenorm;
	_v.z = axis.z * sinhalfangle * inversenorm;
	_v.w = coshalfangle;
	
	return _v;
}


// ---------

mat4 quatAsMatrix4x4(const in vec4 q)
	// from OpenGL Mathematics (glm.g-truc.net)
	// glm/gtc/quaternion.inl
{
	mat4 _mat;
	_mat[0][0] = 1 - 2 * q.y * q.y - 2 * q.z * q.z;
	_mat[0][1] = 2 * q.x * q.y + 2 * q.w * q.z;
	_mat[0][2] = 2 * q.x * q.z - 2 * q.w * q.y;
	_mat[0][3] = 0.0;
	
	_mat[1][0] = 2 * q.x * q.y - 2 * q.w * q.z;
	_mat[1][1] = 1 - 2 * q.x * q.x - 2 * q.z * q.z;
	_mat[1][2] = 2 * q.y * q.z + 2 * q.w * q.x;
	_mat[1][3] = 0.0;
	
	_mat[2][0] = 2 * q.x * q.z + 2 * q.w * q.y;
	_mat[2][1] = 2 * q.y * q.z - 2 * q.w * q.x;
	_mat[2][2] = 1 - 2 * q.x * q.x - 2 * q.y * q.y;
	_mat[2][3] = 0.0;
	
	_mat[3][0] = 0.0;
	_mat[3][1] = 0.0;
	_mat[3][2] = 0.0;
	_mat[3][3] = 1.0;
	return _mat;
}

// ------------------------------------------------------------

float saturate( in float v )
{
    return clamp(v,0.0,1.0);
}

float expose( in float l, in float e )
{
    return (1.5 - exp(-l*e));
}

const vec4 lumi = vec4(0.30, 0.59, 0.11, 0);

float luminosity( in vec4 clr )
{
    return dot(clr, lumi);
}

vec4  normal_color( in vec3 n )
{
    return vec4((n*vec3(0.5)+vec3(0.5)), 1);
}

float attenuation( in float distance, in float atten )
{
    return min( 1.0/(atten*distance*distance), 1.0 );
}

// Returns vec4 float color from 8 digit hex color
// parameter hexVal given as: 0xRRGGBBAA
vec4 colorFromHexRGBA(in uint hexVal ){

	vec4 result = vec4(0);

	result.a = (hexVal >>  0) & 0xff;
	result.b = (hexVal >>  8) & 0xff;
	result.g = (hexVal >> 16) & 0xff;
	result.r = (hexVal >> 24) & 0xff;

	return (result/255.f);
}

// Generates map function for types: float, vec2, vec3, vec4
//
// Maps value t (expected to be in range [min_val..max_val]) to range [new_min..new_max]
//
// Expectations: min_val < max_val && new_min < new_max 
#define GEN_MAP( Typename )                                                                                      \
	Typename map(Typename t, Typename min_val, Typename max_val, Typename new_min, Typename new_max ){           \
		return new_min + (new_max - new_min) * ((clamp(t, min_val, max_val) - min_val) / ( max_val - min_val )); \
	}
	GEN_MAP(float) // maps float type
	GEN_MAP(vec2)  // maps vec2  type
	GEN_MAP(vec3)  // maps vec3  type
	GEN_MAP(vec4)  // maps vec4  type
#undef GEN_MAP