#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in VertexData {
	vec2 texCoord;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

// Note the suffix to the sampler name: `__ycbcr__` - this signals to Island
// that the image is a yuv image, and that it must create an immutable color 
// conversion sampler for this image.
//
layout (set = 1, binding = 0 ) uniform sampler2D tex_video__ycbcr__; 

layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

void main(){
	outFragColor = vec4( texture(tex_video__ycbcr__, inData.texCoord).rgb, 1 );
}
