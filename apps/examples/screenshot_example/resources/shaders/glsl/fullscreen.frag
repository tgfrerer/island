#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define PI 3.14159265358979323f

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

// uniforms
layout (push_constant) uniform Params
{
    mat4x4 model_matrix;
	vec2 u_resolution;
	float u_time;
    int show_grid;
};

layout (set = 0, binding = 0) uniform sampler2D tex_0;


float saturate(const in float x){
    return min(1,max(0,x));
}

// Constant thickness grid lines 
// -- uses a technique described by Evan Wallace in: https://www.madebyevan.com/shaders/grid/
// License: CC0 (http://creativecommons.org/publicdomain/zero/1.0/)
float get_grid(in const vec2 coord, in const vec2 factor){
    

    vec2 c = coord*factor;

    // There is a discontinuity at the seams which we want 
    // to mask. It happens because the angle goes from 
    // 359 to 0.
    // 
    // We mask this by testing two fwidth, one for each end of the 
    // mathematical ring of radians, and then we pick the smaller 
    // one...
    //
    vec2 delta = min(
        fwidth(coord*factor),
        fwidth(mod(coord*factor,vec2(1,1)*3/factor))
        ); 

	vec2 grid = abs(fract(c)-0.5) / (delta);
    
    // Make grid lines a bit more delicate.
    //
    // -- In case we have dark lines this
    //    looks better.
    //
    // grid = pow(grid,vec2(0.5));
	
    float line = abs(min(grid.x, grid.y));

	return saturate(line);
}


void main(){


	vec2 st = inTexCoord.xy;
    
    float lon = (st.x-0.5) * PI * 2;
    float lat = -(st.y-0.5) * PI;
    
    vec3 pos = vec3(
        cos(lat) * cos(lon), 
        cos(lat) * sin(lon), 
        sin(lat)
        );

    pos = (model_matrix * vec4(pos,0)).xyz;

    lon = atan(pos.y, pos.x); // xy angle (camera faces into -z) [-PI/2..PI/2]
    lat = asin(pos.z);        // [-PI..PI]

    float highlight = get_grid(vec2(lat/(PI*2),lon/(PI)),vec2(8) * vec2(2,1));

	vec3 color = texture(tex_0,vec2(0.5*(1+(lon/PI)), 0.5-(lat/PI)),0).rgb;

    if (show_grid == 1){
        // equivalent to pre-multiplied alpha blending 
        color.rgb = mix(color.rgb, vec3(0.25,0.63,0.78), 1-highlight);
    } 

	outFragColor = vec4(color, 1 );
}
