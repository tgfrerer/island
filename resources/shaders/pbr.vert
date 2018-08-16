#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable


layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;

layout (set = 0, binding = 0) uniform UBO {
	mat4 projection;
	mat4 model;
	mat4 view;
} ubo;

layout (set = 1, binding = 0) uniform UBONode {
	mat4 matrix;
} node;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outNormal;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	vec4 locPos;
		locPos = ubo.model * node.matrix * vec4(inPos, 1.0);
		outNormal = normalize(transpose(inverse(mat3(ubo.model * node.matrix))) * inNormal);
	locPos.y = -locPos.y;
	outWorldPos = locPos.xyz / locPos.w;
	gl_Position =  ubo.projection * ubo.view * locPos;
}