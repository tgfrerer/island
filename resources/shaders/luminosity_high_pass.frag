#version 450 core

/**
 * adapted via three.js
 * original three.js credits:
 *
 * @author bhouston / http://clara.io/
 *
 * Luminosity
 * http://en.wikipedia.org/wiki/Luminosity
 */

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0;

layout (set = 0, binding = 1) uniform Params 
{
  vec3  defaultColor; // vec3(0)
  float defaultOpacity; // 0
  float luminosityThreshold; // 1.f
  float smoothWidth; // 1.0
};

void main() {
 
 vec4 texel = texture( src_tex_unit_0, inTexCoord );
 vec3 luma  = vec3( 0.299, 0.587, 0.114 );
 
 float v = dot( texel.xyz, luma );
 
 vec4 outputColor = vec4( defaultColor.rgb, defaultOpacity );
 float alpha      = smoothstep( luminosityThreshold, luminosityThreshold + smoothWidth, v );

 outFragColor = mix( outputColor, texel, alpha );
}