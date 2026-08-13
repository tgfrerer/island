#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// uniforms 
layout (push_constant) uniform Params
{
  vec2 u_resolution;    // screen witdh, height (given in pixels)
  vec2 u_quad_position; // given in pixels
  vec2 u_quad_extents;  // given in pixels
};

// inputs
layout (location = 0) in vec3 vert; // normalized vertex coordinates

// outputs 
layout (location = 0) out vec2 outTexCoord;


// Override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{

	outTexCoord = vec2((12 >> gl_VertexIndex) &1, (9 >> gl_VertexIndex ) &1);

	vec2 scale_factor = vec2(2.0)/(u_resolution); // so that any more scale is directly a pixel.

	// we calculate the texture coordinate here, just because we can 
	outTexCoord = vec2((12 >> gl_VertexIndex) &1, (9 >> gl_VertexIndex ) &1);

	vec4 position = vec4(0,0,0,1);
          	
	position.xy = 
		(
			(vert.xy + vec2(0.5)) * u_quad_extents
			+ u_quad_position 
		) * scale_factor 
		- vec2(1);

	gl_Position = position;
}
