
#include "sdf_ops.glsl"
#include "sdf_primitives.glsl"

/// this method describes the complete scene, 
/// and returns the estimated signed distance from point P
/// to the closest point in the scene.
///
float scene_distance(vec3 P){
	float t = 0.;

	//t = sdf_box(vec3(-100,-100,-100), vec3(100,100,100));
	
	t = sdf_intersect(t,sdf_sphere(P,50));

	return t;
}
