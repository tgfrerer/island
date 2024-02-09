#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// #pragma include <utils.glsl>

// inputs 
layout (location = 0) in VertexData {
	vec2 texCoord;
	vec4 worldPos;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};
layout (set=0, binding=1) uniform sampler2D tex_unit_0; 


// // Constant thickness grid lines 
// // -- uses a technique described by Evan Wallace in: https://www.madebyevan.com/shaders/grid/
// // License: CC0 (http://creativecommons.org/publicdomain/zero/1.0/)
float get_grid(in const vec2 coord, in const float line_thickness){
	vec2 grid = abs(fract(coord)-0.5) / (fwidth(coord) * line_thickness);
	float line = min(grid.x, grid.y);
	return line;
}

// Constant thickness lines 
// -- uses a technique described by Evan Wallace in: https://www.madebyevan.com/shaders/grid/
// License: CC0 (http://creativecommons.org/publicdomain/zero/1.0/)
float get_lines(in const float coord, in const float line_thickness){
	float line = abs(fract(coord)-0.5) / (fwidth(coord)*line_thickness);
	return line;
}

void main(){
	
	// float height = 1-get_grid(inData.texCoord.xy*100, 0.75);
	float height = texture(tex_unit_0,inData.texCoord).x * 50;
	// float height = inData.worldPos.y;

	float darkness = 1.f - min(0.95,get_lines(height*2,1));
	// outFragColor = vec4(inData.texCoord, 0, 1);

	outFragColor = vec4(vec3(darkness),1);
}