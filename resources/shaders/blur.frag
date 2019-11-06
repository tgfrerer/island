#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#ifndef KERNEL_RADIUS
  #define KERNEL_RADIUS 2
#endif

#ifndef SIGMA
  #define SIGMA KERNEL_RADIUS
#endif


// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0) uniform sampler2D src_tex_unit_0;

layout (set = 0, binding = 1) uniform BlurParams 
{
	vec2 resolution;
	vec2 direction;
} params;

float gaussianPdf(in float x, in float sigma) {
  return 0.39894 * exp( -0.5 * x * x/( sigma * sigma))/sigma;
}

void main() {
  vec2 invSize = 1.0 / params.resolution;
  float fSigma = float(SIGMA);
  float weightSum = gaussianPdf(0.0, fSigma);
  vec3 diffuseSum = texture( src_tex_unit_0, inTexCoord).rgb * weightSum;
  
  for( int i = 1; i < KERNEL_RADIUS; i ++ ) {
    float x = float(i);
    float w = gaussianPdf(x, fSigma);
    vec2 uvOffset = params.direction * invSize * x;
    vec3 sample1 = texture( src_tex_unit_0, inTexCoord + uvOffset).rgb;
    vec3 sample2 = texture( src_tex_unit_0, inTexCoord - uvOffset).rgb;
    diffuseSum += (sample1 + sample2) * w;
    weightSum += 2.0 * w;
  }

  outFragColor = vec4(diffuseSum/weightSum, 1.0);
}