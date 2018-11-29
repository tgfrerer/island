//     _____    ___
//    /    /   /  /     math_utils.glsl
//   /  __/ * /  /__    (c) ponies & light, 2014. All rights reserved.
//  /__/     /_____/    poniesandlight.co.uk
//
//  Created by tim on 05/06/2014.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


// ----------------------------------------------------------------------
/// \brief 		Perform sphere / ray intersection. 
/// \note 		we're following the naming of variables as in Real-Time Rendering, 3rd ed. pp. 741
/// \param		sphere_ 	given as (xyz,r)
/// \param		ray_ 		assumed to be normalized
bool raySphereIntersect(in vec3 ray_, in vec3 rayOrigin_,in vec4 sphere_, inout float t1, inout float t2, inout float m2, inout float s){

	float r = sphere_.w;
	vec3  l = sphere_.xyz - rayOrigin_ ;  
	s = dot(l, ray_);
	
	m2 = dot(l, l) - s*s; ///< "m squared", this is the (midpoint distance to sphere centre) squared 

	if (m2 > r*r) return false;

	float q = sqrt(r*r - m2);

	t1 = (s - q); // close intersection
	t2 = (s + q); // far   intersection


	return true;
}


// ----------------------------------------------------------------------

float map(float value, float inputMin, float inputMax, float outputMin, float outputMax){
	return ((value - inputMin) / (inputMax - inputMin) * (outputMax - outputMin) + outputMin);
}
