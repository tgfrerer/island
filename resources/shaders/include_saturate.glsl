#ifndef INCLUDE_SATURATE_GLSL
#define INCLUDE_SATURATE_GLSL

float saturate(in float val){
	return min(1,max(0,val));
}

#endif