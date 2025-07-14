#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_shading_language_include : enable

#include "noise3D.glsl"

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0 ) uniform sampler2D src_tex_unit_0;

layout (constant_id = 0) const float blur_x = 0.f;
layout (constant_id = 1) const float blur_y = 0.f;
layout (constant_id = 2) const float KERNEL_RADIUS = 10.f;
layout (constant_id = 3) const float SIGMA = 0.6995f*10.8;

const vec2 direction = vec2(blur_x, blur_y);


// -----------------------------------------------------------------------------
// Fractional Brownian Motion: self-similar noise
// 
// H is the Hurst exponent, controls fractional part of fbm fractional brownian motion
// H typically goes from 0..1, with 1 creating Brownian Noise, 0 Pink (sharper) Noise.
float noise_fbm(in vec3 st, in float H){

    // for background on this see: https://iquilezles.org/articles/fbm/

#ifndef NOISE_NUM_OCTAVES
#define NOISE_NUM_OCTAVES 10
#endif

  float n=0;
  float f=1;

  float G = exp2(-H); // 1/(2^H), // G stands for "Gain factor"
  float a = 1.0;
  float n_max = 1.0;

  for(int i=0; i!=NOISE_NUM_OCTAVES; i++){
    n += a * snoise(st * f);
    f *= 2.0;
    n_max += a;
    a *= G;
  }

  return n/(n_max*2)+0.5; // map froma -n_max..n_max to 0..1
}

float gaussianPdf(in float x, in float sigma) {
  return 0.39894 * exp( -0.5 * x * x/( sigma * sigma))/sigma;
}


// Generally, you would want to scale sigma in proportion with the 
// kernel size: larger sigma means wider radius of influence for each 
// pixel; it also means a blurrier image.
vec4 blur(float sigma, float kernel_radius){

  vec2 invSize = 1.0 / textureSize(src_tex_unit_0,0);

  float fSigma = float(sigma);
  float weightSum = gaussianPdf(0.0, fSigma);
  vec4 diffuseSum = texture( src_tex_unit_0, inTexCoord ) * weightSum;
  
  for( int i = 1; i < kernel_radius; i ++ ) {
    float x = float(i);
    float w = gaussianPdf(x, fSigma);
    vec2 uvOffset = direction * invSize * x;
    vec4 sample1 = texture( src_tex_unit_0, inTexCoord + uvOffset);
    vec4 sample2 = texture( src_tex_unit_0, inTexCoord - uvOffset);
    diffuseSum += (sample1 + sample2) * w;
    weightSum += 2.0 * w;
  }

   float decay = 0; 
   // decay = 0.88;
   decay  = 1; // disables decay 
  // weightSum /= decay;
  return diffuseSum * decay/weightSum;

}



void main() {

  vec4 b = blur(SIGMA*.7, KERNEL_RADIUS*.9);
  vec4 s = blur(SIGMA*0.125, KERNEL_RADIUS*0.325);

  vec4 p = texture( src_tex_unit_0, inTexCoord );

  // float n = noise_fbm(vec3(inTexCoord*2, 0.45 ), 0.2);

  outFragColor = vec4(
    mix(
      p.rgb,
      s.rgb,
      clamp(pow(vec3(p.a),vec3(12.2)),vec3(0),vec3(0.1))
      ), // RGB 
    b.a * 0.999  // wetness
    + clamp(dot(s.rgb, b.rgb),0, 0.0075 ) // higher means more meanders, here
  ) 
  ;
  // outFragColor = vec4(
  //   mix(
  //     p.rgb,
  //     s.rgb,
  //     clamp(pow(p.a,22.2),0.0,0.03)), // RGB 
  //   b.a * 0.97 + clamp(dot(s.rgb, s.rgb),0.0, 0.3 )
  //   * (1-smoothstep(0.12, 0.15, n * 0.5 + 0.5) ) );
}
