#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0 ) uniform sampler2D src_tex_unit_0;

layout (constant_id = 0) const float blur_x = 0.f;
layout (constant_id = 1) const float blur_y = 0.f;

layout (push_constant) uniform Settings {
  uint u_kernel_radius;
  float u_sigma;
};

// layout (constant_id = 2) const uint KERNEL_RADIUS = 5;
// layout (constant_id = 3) const float SIGMA = 3;

const vec2 direction = vec2(blur_x, blur_y);

// Gaussian probability density function with mean (μ == 0) -- 
// See: <https://en.wikipedia.org/wiki/Normal_distribution>
// 
// This uses the general form:
// \frac{1}{\sigma\sqrt{2\pi}}e^{-\frac{x^2}{2\sigma^2}} 
// 
// -- the first constant (0.39894) corresponds to 1/sqrt(2*PI)
// -- sigma controls the sharpness of the blur - larger sigma
//    means softer blur, but will need a larger blur radius.
// -- blur radius given in pixels unit
//
float gaussianPdf(in float x, in float sigma) {
  return 0.39894 * exp( (-0.5 * x * x)/( sigma * sigma))/sigma;
}

// 
// Generally, you would want to scale sigma in proportion with the 
// kernel size: larger sigma means wider radius of influence for each 
// pixel; it also means a blurrier image.
vec4 blur(float sigma, float kernel_radius){

  vec2 invSize = 1.0 / textureSize(src_tex_unit_0,0);

  float weightSum = gaussianPdf(0.0, sigma);
  vec4 diffuseSum = texture( src_tex_unit_0, inTexCoord ) * weightSum;
  
  for( int i = 1; i < kernel_radius; i ++ ) {
    float x = float(i);
    float w = gaussianPdf(x, sigma);
    vec2 uvOffset = direction * invSize * (x  ); // note offset by 0.5 to hit pixel center
    vec4 sample1 = texture( src_tex_unit_0, inTexCoord + uvOffset);
    vec4 sample2 = texture( src_tex_unit_0, inTexCoord - uvOffset);
    diffuseSum += (sample1 + sample2) * w;
    weightSum += 2.0 * w;
  }
  return diffuseSum / weightSum;
}

// Use linear filtering to accelerate blur by factor of 2.
// See: <https://lisyarus.github.io/blog/posts/compute-blur.html> 
// for a discussion of this technique. 
vec4 fast_blur(float sigma, float kernel_radius){

  vec2 invSize = 1.0 / textureSize(src_tex_unit_0,0);

  float weightSum = gaussianPdf(0.0, sigma);
  vec4 diffuseSum = texture( src_tex_unit_0, inTexCoord) * weightSum;
  
  for( int i = 1; i < kernel_radius; i+=2 ) {

    float x = float(i);
    float w_0 = gaussianPdf(x, sigma);
    float w_1 = gaussianPdf(x+1, sigma);

    float w = w_0 + w_1;
    float t = w_1 / w;

    vec2 uvOffset = direction * invSize * (x + t);

    vec4 sample1 = texture( src_tex_unit_0, inTexCoord + uvOffset);
    vec4 sample2 = texture( src_tex_unit_0, inTexCoord - uvOffset);

    diffuseSum += (sample1 + sample2) * w;
    weightSum += 2.0 * w;
  }
  return diffuseSum / weightSum;
}

void main() {
  outFragColor = fast_blur(u_sigma, u_kernel_radius);
}
