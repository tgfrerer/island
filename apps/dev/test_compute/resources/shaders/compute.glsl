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


void main(){

	uint index = gl_GlobalInvocationID.x; // globalInvocationId is how we were dispatched.

	 particles[index].pos.y = particles[index].pos.w 
	 						+ particles[index].pos.z * 0.10 * sin(((index % 256) / 256.f) * 2 * 3.14159);


}