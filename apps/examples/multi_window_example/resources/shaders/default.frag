#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in VertexData {
	vec4 position;
	vec3 normal;
	vec4 colour;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

layout (set = 0, binding = 0) uniform MVP_Default 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (set = 0, binding = 1) uniform Uniform_Data
{
	vec4 colour;
};


vec4 sun_pos = vec4(-1000,1000,1000,0);

void main(){


	// we must transform sun to eye space
	// since position and normal is in 
	// eye space too
	vec3 sun = (viewMatrix * sun_pos).xyz;

	vec3 L = normalize(sun-inData.position.xyz);
	vec3 N = normalize(inData.normal);

	float lambert = dot(L,N);

	vec4 out_color = inData.colour * colour * vec4(vec3(max(0.5,lambert)),1);

	
#if defined(SHOW_MONO_COLOUR)
	out_color = colour; // so that it does not get optimised away.
#elif defined(SHOW_NORMAL_COLOUR) 
	out_color = vec4((normalize(inData.normal) * 0.5 + vec3(0.5)),1);
#endif

	outFragColor = out_color;
}