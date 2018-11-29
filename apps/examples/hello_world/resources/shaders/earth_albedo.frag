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
	vec4 worldCentreInEyeSpace;
};

layout (set = 1, binding = 1) uniform sampler2D tex_unit_0;
layout (set = 1, binding = 2) uniform sampler2D tex_unit_1;
layout (set = 1, binding = 3) uniform sampler2D tex_unit_2;


struct BlinnPhong {
	vec3 V; // view vector
	vec3 L; // light vector
	vec3 N; // normal vector
	vec3 H; // half vector
	vec3 R; // reflected light vector
	float diffuse;
	float specularR; // based on reflect
	float specularH; // based on half vector
};

// ----------------------------------------------------------------------

void calculateLighting(inout BlinnPhong b){

	b.H = normalize(b.V + b.L );
	b.R = reflect( - b.L, b.N);

	b.diffuse   = max( dot( b.L, b.N), 0.f);
	b.specularH = max( dot(b.N, b.H) , 0.f);
	b.specularR = max( dot(b.R, b.V) , 0.f);
}

// ----------------------------------------------------------------------

void main(){


	vec3 normal   = normalize(inData.normal);
	vec3 tangent  = normalize(inData.tangent);
	vec3 biNormal = cross(normal,tangent);

	// Tangent space is a space where the: 
	// x-axis is formed by tangent,
	// y-axis is formed by biNormal,
	// z-axis is formed by normal
	mat3 tangentSpace = mat3(tangent, biNormal, normal);

	{
		vec3 bumpMap = texture(tex_unit_1, inData.texCoord).rgb;
		vec3 bumpNormal = 2 * (bumpMap - vec3(0.5)); // map 0..1 to -1..1
		// store bumpNormal in normal
		normal = tangentSpace * bumpNormal; // transform bumpNormal into tangent space
	}

	//vec3 L = normalize(sunInEyeSpace.xyz); // parallel rays to distant sun, we don't care about the origin point that much;

	// calculate specular + diffuse light terms.	
	BlinnPhong Atmosphere;
	Atmosphere.V = normalize(-inData.position.xyz); // (negative view ray direction) : ray from sample point to camera
	Atmosphere.N = normal;
	Atmosphere.L = normalize(sunInEyeSpace.xyz - inData.position.xyz);

	calculateLighting(Atmosphere);	

	// night on earth is when diffuse would turn negative
	float nightOnEarth = max(0, -dot(Atmosphere.L, Atmosphere.N));

	vec3 daySample   = texture(tex_unit_0, inData.texCoord).rgb;
	vec3 nightSample = texture(tex_unit_2, inData.texCoord).rgb;
	
	vec3 outColor = vec3(1);
	outColor = mix(daySample * (Atmosphere.diffuse + 0.6 * pow(Atmosphere.specularH, 23)) , nightSample, nightOnEarth*1.2 );

	outFragColor = vec4(outColor,1);
	// outFragColor = vec4(inData.texCoord, 0, 1);
	// outFragColor = vec4((bumpNormal * 0.5 + vec3(0.5)),1);
	// outFragColor = inData.color;
}