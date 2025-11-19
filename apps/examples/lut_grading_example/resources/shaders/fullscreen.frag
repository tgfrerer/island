#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable

// vertex shader inputs
layout( location = 0 ) in vec2 inTexCoord;

// uniforms
// layout (set = 0, binding = 1) uniform sampler2D src_tex_unit_2;
// layout (set = 0, binding = 1) uniform sampler3D src_tex_unit_1;

layout( set = 1, binding = 0 ) uniform Params {
	float x_value;
};

layout( push_constant ) uniform tex_handles {
	uint tex_0;
	uint tex_1;
	uint tex_lut;
};

// it's possible to overlap registers in vulkan -- this means i
// can access a descriptor in different ways:
layout( set = 2, binding = 0 ) uniform sampler3D bindless_textures3D[];
layout( set = 2, binding = 0 ) uniform sampler2D bindless_textures[];

// outputs
layout( location = 0 ) out vec4 outFragColor;

// --------------------------------------------------

uint get_texture_id( uint resource_id ) {
	uint idx     = ( resource_id >> 12 ) & 0xfffff;
	uint type    = ( resource_id >> 8 ) & 0xf; // todo: we could check that the type is correct
	uint version = resource_id & 0xff;
	return idx;
}

vec4 texture2d_load( in uint id, in vec2 coords ) {
	return texture( bindless_textures[ nonuniformEXT( get_texture_id( id ) ) ], coords );
}

vec4 texture3d_load( in uint id, in vec3 coords ) {
	return texture( bindless_textures3D[ nonuniformEXT( get_texture_id( id ) ) ], coords );
}

// --------------------------------------------------

void main() {

	uint which_texture_id = int( mod( gl_FragCoord.x + gl_FragCoord.y, 2 ) );

	// vec4 sampleColor = texture(bindless_textures[nonuniformEXT(which_texture_id)], inTexCoord.xy);
	// vec4 gradedColor = texture(bindless_textures3D[nonuniformEXT(2)], sampleColor.rgb);

	vec4 sampleColor = texture2d_load( tex_0, inTexCoord.xy );
	vec4 gradedColor = texture3d_load( tex_lut, sampleColor.rgb );

	outFragColor = mix( sampleColor, gradedColor, step( inTexCoord.x, x_value ) );
}
