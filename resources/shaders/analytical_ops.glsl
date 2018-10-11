
// analytical ray-box intersection
//
// _w is ray direction (normalised)
// _P is starting point
// t_.x will be near distance
// t_.y will be far distance
bool box_intersect(in vec3 _boxMin, in vec3 _boxMax, in vec3 _P, in vec3 _w,  out vec2 t_) {
  // we use the slabs method.

  // by dividing through w.x, we use the projection of ray onto the x axis to
  // approach a plane intersecting the x axis at _boxMin.x
  // then we do the same for a plane intersecting the x axis at _boxMax.x

  vec3 t1 = (_boxMin - _P) / _w;
  vec3 t2 = (_boxMax - _P) / _w;

  // now we know how far along ray.x we have to travel to reach the plane
  // defined by _boxMin.x and _boxMax.x


  // tNear = min(t1.x, t2.x);             // <- only the closest one is the near one 
  // tNear = max(tNear, min(t1.y, t2.y)); // find the near one on the next coordinate, and swap near if we would have to travel further on the other coordinate  
  // tNear = max(tNear, min(t1.z, t2.z)); // see if we would have to travel even further on the other coordinate
  vec3 minT = min(t1,t2);
  float tNear = max(max(minT.x, minT.y), minT.z);

  // tFar  = max(t1.x, t2.x);
  // tFar  = min(tFar , max(t1.y, t2.y));
  // tFar  = min(tFar , max(t1.z, t2.z));
  vec3 maxT = max(t1, t2);
  float tFar = min(min(maxT.x, maxT.y), maxT.z);

  t_.x = tNear;
  t_.y = tFar;

  // intersection only happens if tNear < tFar, and the closest t should be tNear.
  // we do, however, return both tNear and tFar, so that we can render
  // the inside of a box as well.

  return (tNear < tFar);

}

// ----------------------------------------------------------------------

float rayDistance(in vec3 _boxMin, in vec3 _boxMax, in vec3 _P, in vec3 _ray ){
  
  vec2 nearFar;

  float raydistance = 0;
  bool isect = box_intersect(_boxMin,_boxMax, _P, _ray, nearFar);
   if (isect){
    // float zDepth = 1./0.; // infinity, TODO: calculate screenspace intersection with depth map.
    // float t_far = min(zDepth, nearFar.y);
    float t_far = nearFar.y;
    float t_near = max(nearFar.x,0);
    raydistance = t_far - t_near;
   } 
  return raydistance;

}