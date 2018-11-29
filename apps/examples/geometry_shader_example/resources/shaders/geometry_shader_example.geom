#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// arguments
layout (set = 0, binding = 0) uniform MatrixStack 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

layout (location = 0) in VertexAttrib 
{
	     vec3  position;
	flat int   flare_type;
	flat float radius;
	flat float distanceToBorder;
	flat float rotation;
	flat float intensity;
} vertex[];

layout (location = 0) out VertexAttrib {
	vec3 position;
	vec2 texcoord;
	flat float distanceToBorder;
	flat int flare_type;
	flat float intensity;
} out_vertex;

// We override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};


const float ROOT_TWO = 1.4142135623730950488016887242097;

#define CO_PLANAR // whether our billboard should be rendered as co-planar to the camera projection plane

void main()
{
	// orient towards camera: get ray from vertex to camera.

	// everything happens in eye space here.

	mat4 modelViewMatrix = viewMatrix * modelMatrix;

	vec3 vertexToCamera = (-vertex[0].position);
	vec3 up = vec3(0,1,0);

#ifdef CO_PLANAR
	vec3 right = vec3(1,0,0); ///< will make the billboards be co-planar with the camera projection plane
#else
	vec3 right = -normalize(cross(vertexToCamera, up)); ///< will make the billboards face the camera
#endif

	float offset = ROOT_TWO * vertex[0].radius;

	float cosRot = cos(vertex[0].rotation);
	float sinRot = sin(vertex[0].rotation);
	mat2 rotationMatrix = transpose(mat2( -cosRot, sinRot, -sinRot, -cosRot  ));

	// same for all
	out_vertex.flare_type       = vertex[0].flare_type;
	out_vertex.distanceToBorder = vertex[0].distanceToBorder;
	out_vertex.intensity        = vertex[0].intensity;

	// 0
	out_vertex.texcoord = vec2(0,0);
	out_vertex.position = vertex[0].position + vec3((rotationMatrix * (- right + up ).xy),0) * offset;
	
	gl_Position =  projectionMatrix * vec4(out_vertex.position,1);
	EmitVertex();

	// 1
	out_vertex.flare_type       = vertex[0].flare_type;
	out_vertex.distanceToBorder = vertex[0].distanceToBorder;
	out_vertex.intensity        = vertex[0].intensity;


	out_vertex.texcoord = vec2(0,1);
	out_vertex.position = vertex[0].position + vec3((rotationMatrix * (- right - up ).xy),0) * offset;

	gl_Position = projectionMatrix * vec4(out_vertex.position,1);
	EmitVertex();

	// 2

	out_vertex.flare_type       = vertex[0].flare_type;
	out_vertex.distanceToBorder = vertex[0].distanceToBorder;
	out_vertex.intensity        = vertex[0].intensity;

	out_vertex.texcoord = vec2(1,0);
	out_vertex.position = vertex[0].position + vec3((rotationMatrix * (+ right + up ).xy),0) * offset;
	gl_Position = projectionMatrix *vec4(out_vertex.position,1);
	EmitVertex();

	// 3

	out_vertex.flare_type       = vertex[0].flare_type;
	out_vertex.distanceToBorder = vertex[0].distanceToBorder;
	out_vertex.intensity        = vertex[0].intensity;

	out_vertex.texcoord = vec2(1,1);
	out_vertex.position = vertex[0].position + vec3((rotationMatrix * (+ right - up ).xy),0) * offset;
	gl_Position =  projectionMatrix *vec4(out_vertex.position,1);
	EmitVertex();

}
