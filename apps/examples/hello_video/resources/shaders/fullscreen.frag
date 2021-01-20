#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// vertex shader inputs
layout (location = 0) in vec2 inTexCoord;

// uniforms
layout (set = 0, binding = 0) uniform sampler2D src_video;

// outputs
layout (location = 0) out vec4 outFragColor;

void main(){
    vec4 col = texture(src_video, inTexCoord.xy);
    outFragColor = vec4(col.b, col.g, col.r, col.a);
}