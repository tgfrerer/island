//     _____    ___
//    /    /   /  /     gradient.glsl
//   /  __/ * /  /__    (c) ponies & light, 2014. All rights reserved.
//  /__/     /_____/    poniesandlight.co.uk
//
//  Created by tgfrerer on 18/03/2014.
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


/// @brief  calculates a gradient mapped colour 
/// @detail the calculations are based on colour stops c1, c2, c3, 
///         and one normlised input value, emphasises constant 
///         execution time.
/// @param c1 [rgbw] first colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param c2 [rgbw] second colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param value the input value for gradient mapping, a normalised 
///        float
/// @note   values are interpolated close to sinusoidal, using 
///         smoothstep, sinusoidal interpolation being what Photoshop
///         uses for its gradients.
/// @author @tgfrerer
vec3 getGradient(vec4 c1, vec4 c2, float value_){
	float blend = smoothstep(c1.w, c2.w, value_);
	return mix(c1.rgb, c2.rgb, blend);
}

// ----------------------------------------------------------------------

/// @brief  calculates a gradient mapped colour 
/// @detail the calculations are based on colour stops c1, c2, c3, 
///         and one normlised input value, emphasises constant 
///         execution time.
/// @param c1 [rgbw] first colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param c2 [rgbw] second colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param c3 [rgbw] third colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param value the input value for gradient mapping, a normalised 
///        float
/// @note   values are interpolated close to sinusoidal, using 
///         smoothstep, sinusoidal interpolation being what Photoshop
///         uses for its gradients.
/// @author @tgfrerer
vec3 getGradient(vec4 c1, vec4 c2, vec4 c3, float value_){
	
	float blend1 = smoothstep(c1.w, c2.w, value_);
	float blend2 = smoothstep(c2.w, c3.w, value_);
	
	vec3 
	col = mix(c1.rgb, c2.rgb, blend1);
	col = mix(col, c3.rgb, blend2);
	
	return col;
}

// ----------------------------------------------------------------------

/// @brief  calculates a gradient mapped colour 
/// @detail the calculations are based on colour stops c1, c2, c3, 
///         and one normlised input value, emphasises constant 
///         execution time.
/// @param c1 [rgbw] first colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param c2 [rgbw] second colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param c3 [rgbw] third colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param c4 [rgbw] fourth colour stop, with .w being the normalised 
///         position of the colour stop on the gradient beam.
/// @param value the input value for gradient mapping, a normalised 
///        float
/// @note   values are interpolated close to sinusoidal, using 
///         smoothstep, sinusoidal interpolation being what Photoshop
///         uses for its gradients.
/// @author @tgfrerer
vec3 getGradient(vec4 c1, vec4 c2, vec4 c3, vec4 c4, float value_){
	
	float blend1 = smoothstep(c1.w, c2.w, value_);
	float blend2 = smoothstep(c2.w, c3.w, value_);
	float blend3 = smoothstep(c3.w, c4.w, value_);
	
	vec3 
	col = mix(c1.rgb, c2.rgb, blend1);
	col = mix(col, c3.rgb, blend2);
	col = mix(col, c4.rgb, blend3);
	
	return col;
}
