#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in VertexData {
	vec2 bezControl;
	vec4 texColor;
} inData;

// outputs
layout (location = 0) out vec4 outFragColor;

// layout (set = 0, binding = 1) uniform sampler2D tex_unit_0;

layout (set = 0, binding = 0) uniform Mvp 
{
	mat4 modelMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout (set = 0, binding = 1) uniform Color 
{
	vec4 color;
};

void main(){
	
  vec2 p = inData.bezControl;
  
  vec2 px = dFdx(p);
  vec2 py = dFdy(p);
  
  // Chain rule
  float fx = (2*p.x)*px.x - px.y;
  float fy = (2*p.x)*py.x - py.y;
  
  // Signed distance
  float sd = (p.x*p.x - p.y)/sqrt(fx*fx + fy*fy);
  
  // Linear alpha
  float alpha = 0.5 - sd;
  
  vec4 color = inData.texColor;

  if (alpha > 1) {
   // Inside
   color.a = 1;
  }      
  else if (alpha < 0) {
   // Outside
   discard;
  } else{
    // Near boundary
    color.a = alpha; 
  }                 

	// outFragColor = vec4(inData.bezControl, 0, 1);
	// outFragColor = inData.texColor;
	outFragColor = color;
}