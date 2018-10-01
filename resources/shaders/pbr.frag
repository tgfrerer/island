#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable


layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 0) out vec4 outColor;
void main()
{
	vec4 normalColor = vec4(inNormal*0.5 + vec3(0.5),1);
	outColor = normalColor;
}
