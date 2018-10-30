#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "include_circle.glsl" // collect getCircle

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0;

layout (set = 0, binding = 1) uniform TimeInfo {
	float pulse;
};

void main(){

	ivec2 textureSize2d = textureSize(src_tex_unit_0,0);
	float aspectX = (float(textureSize2d.x)/float(textureSize2d.y)) ;

	outFragColor = texture(src_tex_unit_0, inTexCoord.xy ) ;
	// outFragColor = vec4(inTexCoord.xy, 0, 1);
	outFragColor.a *= getCircle(inTexCoord.xy*vec2(aspectX,1)-vec2(aspectX*0.5*pulse,0), 1);
}
