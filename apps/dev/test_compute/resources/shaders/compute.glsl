#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Particle {
	vec2 pos;
	vec2 vel;
	vec4 result;
};


// Binding 0 : Position storage buffer
layout(std430, set = 0, binding = 0) buffer ParticleBuf 
{
   Particle particles[ ];
};

// layout(set=0, binding=1) uniform Parameters {
// 	uint flipFlop; // will be either 0 or 1
// };


layout (local_size_x = 16, local_size_y = 16, local_size_z=4) in;

void main(){

	 particles[gl_LocalInvocationIndex].pos.xy = vec2(gl_LocalInvocationIndex);

}