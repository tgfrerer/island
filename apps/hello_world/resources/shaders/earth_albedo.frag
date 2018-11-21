#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in VertexData {
	vec4 position;
	vec3 normal;
	vec2 texCoord;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform CameraParams 
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (set=1, binding = 0) uniform ModelParams
{
	mat4 modelMatrix;
	vec4 sunInEyeSpace;
};

layout (set = 1, binding = 1) uniform sampler2D tex_unit_0;
layout (set = 1, binding = 2) uniform sampler2D tex_unit_1;


void main(){


	vec3 normal = normalize(inData.normal);

	vec3 L = normalize(sunInEyeSpace.xyz); // parallel rays to distant sun, we don't care about the origin point that much;

	float gourand = dot(normal,L);
	
	outFragColor = vec4(inData.texCoord, 0, 1);

	outFragColor = vec4((normal * 0.5 + vec3(0.5)),1);

	vec3 outColor = vec3(1);
	 outColor = texture(tex_unit_0, inData.texCoord).rgb;
	 outColor = texture(tex_unit_1, inData.texCoord).rgb;

	outColor *= gourand;

	outFragColor = vec4(outColor,1);
	// outFragColor = inData.color;
}