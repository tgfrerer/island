#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in VertexData {
	vec4 position;
	vec3 normal;
	vec2 texCoord;
	vec3 tangent;
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


	vec3 normal   = normalize(inData.normal);
	vec3 tangent  = normalize(inData.tangent);
	vec3 biNormal = cross(normal,tangent);

	// tangent space is a space where the 
	// x-axis is formed by tangent,
	// y-axis is formed by biNormal,
	// z-axis is formed by normal
	mat3 tangentSpace = mat3(tangent, biNormal, normal);

	vec3 bumpMap = texture(tex_unit_1, inData.texCoord).rgb;
	vec3 bumpNormal = 2 * (bumpMap - vec3(0.5));
	bumpNormal = tangentSpace * bumpNormal;


	vec3 L = normalize(sunInEyeSpace.xyz); // parallel rays to distant sun, we don't care about the origin point that much;

	float diffuse = 1.f;
	diffuse = dot(normal,L);
	diffuse = dot(bumpNormal,L);
	
	outFragColor = vec4(inData.texCoord, 0, 1);

	

	vec3 outColor = vec3(1);
	outColor = texture(tex_unit_0, inData.texCoord).rgb;
	


	outColor *= diffuse;

	outFragColor = vec4(outColor,1);
	// outFragColor = vec4((bumpNormal * 0.5 + vec3(0.5)),1);
	// outFragColor = inData.color;
}