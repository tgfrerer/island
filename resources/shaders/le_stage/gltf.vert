#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs (vertex attributes)
layout (location = LOC_POSITIONS ) in vec3 a_pos[NUM_POSITIONS];

#ifdef LOC_NORMALS
	layout (location = LOC_NORMALS) in vec3 a_normal[NUM_NORMALS];
#endif

#ifdef LOC_TANGENTS
	layout (location = LOC_TANGENTS) in vec4 a_tangent[NUM_TANGENTS];
#endif 

#ifdef LOC_TEXCOORDS
	layout (location = LOC_TEXCOORDS) in vec2 a_tex_coord[NUM_TEXCOORDS];
#endif

#ifdef NUM_JOINT_SETS
	layout(location = LOC_JOINT_SETS) in uvec4 a_Joint[NUM_JOINT_SETS];
#endif

#ifdef NUM_JOINT_WEIGHTS_SET
	layout(location = LOC_JOINT_WEIGHTS_SET) in vec4 a_Weight[NUM_JOINT_WEIGHTS_SET];
#endif

// Uniform Arguments
layout (std140, set = 0, binding = 0) uniform UboMatrices {
	mat4 viewProjectionMatrix; // (projection * view) matrix
	mat4 normalMatrix;
	mat4 modelMatrix;
	vec3 camera_position; // camera position in world space
};

layout (std140, set = 0, binding = 1) uniform UboPostProcessing {
	float exposure;
} postProcessing;

#ifdef MORPH_TARGET_COUNT
	// we receive morph target weights as an array of vec4's because
	// if we would use an array of floats this would waste 3 floats 
	// per array element.
	layout (std140, set = 0, binding = 2) uniform UboMorphTargetWeights{
		vec4 morphTargetWeights[((MORPH_TARGET_COUNT+3)/4)+1];
	};
#	ifdef NUM_JOINT_SETS
	layout (std140, set=0, binding = 3 ) readonly buffer UboJointMatrices {
    	mat4 u_jointMatrix [];
	};
	layout (std140, set=0, binding = 4 ) readonly buffer UboJointNormalMatrices {
    	mat4 u_jointNormalMatrix [];
	};
#	endif
#else // !MORPH_TARGET_COUNT
#	ifdef NUM_JOINT_SETS
	layout (std140, set=0, binding = 2 ) readonly buffer UboJointMatrices {
    	mat4 u_jointMatrix [];
	};
	layout (std140, set=0, binding = 3 ) readonly buffer UboJointNormalMatrices {
    	mat4 u_jointNormalMatrix [];
	};
#	endif
#endif

#include "animation.glsl"

#if defined(MATERIAL_SPECULARGLOSSINESS) || defined(MATERIAL_METALLICROUGHNESS)
layout (std140, set = 1, binding = 0) uniform UboMaterialParams {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
};
#endif

// Per-vertex Output to following shader stages
layout (location = 0) out VertexData {
	vec3 v_position;
#ifdef LOC_NORMALS
#ifdef LOC_TANGENTS
	mat3 v_tbn;
#endif // !HAS_TANGENTS
	vec3 v_normal;
#endif
#if NUM_TEXCOORDS > 1
	vec2 v_tex_coord[NUM_TEXCOORDS];
#else 
	vec2 v_tex_coord[1];
#endif
};

// We override the built-in fixed function outputs to have 
// more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};

vec4 getPosition(){
	vec3 pos = a_pos[0];

#ifdef MORPH_TARGET_COUNT
	getTargetPosition(pos);
#endif

#ifdef NUM_JOINT_SETS
    pos = (getSkinningMatrix() * vec4(pos,1)).xyz;
#endif

	return vec4(pos, 1);
}

#ifdef LOC_NORMALS
vec4 getNormal(){
	vec3 normal = a_normal[0];

#ifdef MORPH_TARGET_COUNT
	getTargetNormal(normal);
#endif

#ifdef NUM_JOINT_SETS
    normal = (getSkinningNormalMatrix() * vec4(normal,0)).xyz;
#endif

	return normalize(vec4(normal, 0));
}
#endif

#ifdef LOC_TANGENTS
vec4 getTangent(){

	vec4 tangent = a_tangent[0];

#ifdef MORPH_TARGET_COUNT
    getTargetTangent(tangent);
#endif

#ifdef NUM_JOINT_SETS
    tangent = getSkinningMatrix() * tangent;
#endif

	return normalize(tangent);
}
#endif

void main() {

    vec4 pos = modelMatrix * getPosition(); 	// world position
    v_position = vec3(pos.xyz) / pos.w; 		// un-project 
	
	
#ifdef LOC_NORMALS
#	ifdef LOC_TANGENTS
	vec4 tangent    = getTangent();
	vec3 normalW    = normalize(vec3(normalMatrix * vec4(getNormal().xyz, 0.0)));
	vec3 tangentW   = normalize(vec3(modelMatrix * vec4(tangent.xyz, 0.0)));
	vec3 bitangentW = cross(normalW, tangentW) * tangent.w;
	v_tbn = mat3(tangentW, bitangentW, normalW);
#	else // !LOC_TANGENTS
    v_normal = normalize(vec3(normalMatrix * vec4(getNormal().xyz, 0.0)));
#	endif
#endif // !HAS_NORMALS

#ifdef LOC_TEXCOORDS
	for(int i =0; i != NUM_TEXCOORDS; i++){
		v_tex_coord[i] = a_tex_coord[i];
	}
#else
	// If no texture coordinate is given, we use the vertex index to 
	// create texture coordinates: 
	// http://www.saschawillems.de/?page_id=2122
	v_tex_coord[0] = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
#endif

	gl_Position = viewProjectionMatrix * pos;
}
