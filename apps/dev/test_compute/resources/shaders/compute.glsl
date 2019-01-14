#version 450

layout (local_size_x = 1, local_size_y = 1, local_size_z=1) in;

struct Particle {
	 vec4 pos;
};

// Binding 0 : Position storage buffer
layout(std430, set = 0, binding = 0) buffer ParticleBuf 
{
   Particle particles[ ];
};

// arguments
layout (set = 0, binding = 1) uniform Uniforms 
{
	float t;
};


#define PI 3.1415926535897932384626433


void main(){

	uint index = gl_GlobalInvocationID.x; // globalInvocationId is how we were dispatched.

	vec2  wave_direction = vec2(0.2,-0.4);
	float wave_length = 30.f;
	float wave_vector_magnitude = (2.f * PI ) / wave_length ;
	vec2  wave_vector = normalize(wave_direction) * wave_vector_magnitude;

	vec2 x_zero=(vec2(index/33,index%33)/32.f - vec2(0.5)) * 1024;

	float amplitude = 20.f;
	float omega = PI * 2;

	particles[index].pos.xz = x_zero - (wave_vector / wave_vector_magnitude) * amplitude * sin(dot(x_zero,wave_vector) - omega*t);
	particles[index].pos.y  = -amplitude * cos(dot(x_zero,wave_vector)-omega	*t);

}