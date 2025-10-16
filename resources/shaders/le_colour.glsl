
// Given in 10 nm bands from 380nm to 760nm
// X,Y,Z
const vec3 cie_1931_std_observer[38]={
	vec3(0.001368000000,0.0000390000000,0.006450001000),
	vec3(0.004243000000,0.0001200000000,0.020050010000),
	vec3(0.014310000000,0.0003960000000,0.067850010000),
	vec3(0.043510000000,0.0012100000000,0.207400000000),
	vec3(0.134380000000,0.0040000000000,0.645600000000),
	vec3(0.283900000000,0.0116000000000,1.385600000000),
	vec3(0.348280000000,0.0230000000000,1.747060000000),
	vec3(0.336200000000,0.0380000000000,1.772110000000),
	vec3(0.290800000000,0.0600000000000,1.669200000000),
	vec3(0.195360000000,0.0909800000000,1.287640000000),
	vec3(0.095640000000,0.1390200000000,0.812950100000),
	vec3(0.032010000000,0.2080200000000,0.465180000000),
	vec3(0.004900000000,0.3230000000000,0.272000000000),
	vec3(0.009300000000,0.5030000000000,0.158200000000),
	vec3(0.063270000000,0.7100000000000,0.078249990000),
	vec3(0.165500000000,0.8620000000000,0.042160000000),
	vec3(0.290400000000,0.9540000000000,0.020300000000),
	vec3(0.433449900000,0.9949501000000,0.008749999000),
	vec3(0.594500000000,0.9950000000000,0.003900000000),
	vec3(0.762100000000,0.9520000000000,0.002100000000),
	vec3(0.916300000000,0.8700000000000,0.001650001000),
	vec3(1.026300000000,0.7570000000000,0.001100000000),
	vec3(1.062200000000,0.6310000000000,0.000800000000),
	vec3(1.002600000000,0.5030000000000,0.000340000000),
	vec3(0.854449900000,0.3810000000000,0.000190000000),
	vec3(0.642400000000,0.2650000000000,0.000049999990),
	vec3(0.447900000000,0.1750000000000,0.000020000000),
	vec3(0.283500000000,0.1070000000000,0.000000000000),
	vec3(0.164900000000,0.0610000000000,0.000000000000),
	vec3(0.087400000000,0.0320000000000,0.000000000000),
	vec3(0.046770000000,0.0170000000000,0.000000000000),
	vec3(0.022700000000,0.0082100000000,0.000000000000),
	vec3(0.011359160000,0.0041020000000,0.000000000000),
	vec3(0.005790346000,0.0020910000000,0.000000000000),
	vec3(0.002899327000,0.0010470000000,0.000000000000),
	vec3(0.001439971000,0.0005200000000,0.000000000000),
	vec3(0.000690078600,0.0002492000000,0.000000000000),
	vec3(0.000332301100,0.0001200000000,0.000000000000)
};

// Given in 10nm bands from 380nm to 760nm 
const float cie_std_illum_d65[38] = 
	{ 49.975, 54.6482, 82.7549, 91.486, 93.4318, 86.6823, 
      104.865, 117.008, 117.812, 114.861, 115.923, 108.811, 
      109.354, 107.802, 104.79, 107.689, 104.405, 104.046, 
      100, 96.3342, 95.788, 88.6856, 90.0062, 89.5991, 
      87.6987, 83.2886, 83.6992, 80.0268, 80.2146, 82.2778, 
      78.2842, 69.7213, 71.6091, 74.349, 61.604, 69.8856, 
      75.087, 63.5927 };


// Returns vec4 float color from 8 digit hex color
// parameter hexVal given as: 0xRRGGBBAA
vec4 rgba_linear_color_from_hex(in uint hexVal ){

	vec4 result = vec4(0);

	result.a = (hexVal >>  0) & 0xff;
	result.b = (hexVal >>  8) & 0xff;
	result.g = (hexVal >> 16) & 0xff;
	result.r = (hexVal >> 24) & 0xff;

	return (result/255.f);
}

// we're setting white point to what we expect our hdr 
// screen to have as default brightness, given in nits.
// we assume 350 or 400 for now.

// This is adapted from: <https://panoskarabelas.com/blog/posts/hdr_in_under_10_minutes/>
// vec3 linear_srgb_to_hdr10( in vec3 color, in const float white_point_in_nits)
// {
//     // Convert Rec.709 to Rec.2020 color space to broaden the palette
//     const mat3 from709to2020 =
//     {
//         { 0.6274040f, 0.3292820f, 0.0433136f },
//         { 0.0690970f, 0.9195400f, 0.0113612f },
//         { 0.0163916f, 0.0880132f, 0.8955950f }
//     };   
//     // color = color * from709to2020 ;
//      color = color * from709to2020 ;
// 
//     // Normalize HDR scene values ([0..>1] to [0..1]) for ST.2084 curve
//     const float st2084_max = 10000.0f;
//     // color *= white_point_in_nits / st2084_max;
// 
//     // Apply ST.2084 (PQ curve) for HDR10 standard
//     //
// 	// The original inverse-EOTF spec is Equation 5.2, pg.8 in: 
// 	// <https://pub.smpte.org/latest/st2084/st2084-2014.pdf>
//     const float m1 = 2610.0 / 4096.0 / 4;
//     const float m2 = 2523.0 / 4096.0 * 128;
//     const float c1 = 3424.0 / 4096.0;
//     const float c2 = 2413.0 / 4096.0 * 32;
//     const float c3 = 2392.0 / 4096.0 * 32;
// 	vec3 cp             = pow(abs(color), vec3(m1));
//     color               = pow((c1 + c2 * cp) / (1 + c3 * cp), vec3(m2));
// 
//     return color;
// }


// Via http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
// note that we pre-multiply because this is equivalent to a transpose
// D65 Reference White 
vec3 color_transform_xyz_to_linear_srgb(in vec3 c){
	return c * mat3(
	3.2404542, -1.5371385, -0.4985314,
	-0.9692660,  1.8760108,  0.0415560,
 	0.0556434, -0.2040259,  1.0572252
	) ;
}

// Via http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
// note that we pre-multiply because this is equivalent to a transpose
// E Reference White = Equal energy Spectrum 
vec3 color_transform_xyz_to_cie_rgb(in vec3 c){
	return c * mat3(
 		2.3706743, -0.9000405, -0.4706338,
		-0.5138850,  1.4253036,  0.0885814,
 		0.0052982, -0.0146949,  1.0093968
	) ;
}

// This can also be found here: 
// https://antlerpost.com/colour-spaces/ST2084.html
vec3 color_transform_xyz_to_rec_2020(in vec3 c){
	return c * mat3(
		vec3( 1.71666343, -0.35567332, -0.25336809 ),
		vec3( -0.66667384, 1.61645574, 0.0157683 ), 
		vec3( 0.01764248, -0.04277698, 0.94224328 )
	);
}

// This can also be found here: 
// https://antlerpost.com/colour-spaces/ST2084.html
vec3 color_transform_xyz_to_p3(in vec3 c){
	return c * mat3(
        vec3(2.49349691, -0.93138362, -0.40271078),
        vec3(-0.82948897, 1.76266406, 0.02362469),
        vec3(0.03584583, -0.07617239, 0.95688452)
	);
}

// ----------------------------------------------------------------------  
// Electro-Optical Transfer Functions (and their inverses)
// See also: <https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#TRANSFER_CONVERSION>
// ----------------------------------------------------------------------  

// What is a transfer function and why should I care? 
//
// sRGB defines a color space - a gamut. see also:
// <https://en.wikipedia.org/wiki/Rec._709> 
// But it also defines a transfer function.
//
// The electro-optical transfer function (EOTF) is the transfer
// function having the picture or video signal as input and converting
// it into the linear light output of the display. It is the inverse
// of the OETF.
//
// The opto-electronic transfer function (OETF) is the transfer
// function having the scene light as input and converting into the
// picture or video signal as output. 

// ----------------------------------------------------------------------  
// TRANSFER FUNCTIONS FOLLOW
// ----------------------------------------------------------------------  

// Converts a color from non-linear R'G'B' to linear RGB encoding;
// 
// In case you are reading from an image format that is `*_SRGB`, this
// function is automatically applied on texel read. The texel rgb
// values are then assumed to be in linear sRGB colorspace.
// 
// This is the inverse of `inverse_eotf_srgb`.
//
vec3 eotf_srgb(in const vec3 non_linear_rgb) {
    bvec3 cutoff = lessThan(non_linear_rgb, vec3(0.04045));
    vec3 lower = non_linear_rgb/vec3(12.92);
    vec3 higher = pow((non_linear_rgb + vec3(0.055))/vec3(1.055), vec3(2.4));
    return mix(higher, lower, cutoff);
}

// Converts a color from linear RGB to non-linear R'G'B'. 
//
// This uses the correct OETF (opto-electrical transfer function)
// based on the bt.709 standard:
// <https://en.wikipedia.org/wiki/Rec._709>
//
// In case you are writing to an image format that is `*_SRGB`, this
// function is automatically applied on texel save by the driver; you
// don't have to apply it again.
//
// This is the inverse of `eotf_srgb`.
//
vec3 inverse_eotf_srgb(in const vec3 linear_rgb) { 
	bvec3 cutoff = lessThan(linear_rgb, vec3(0.0031308));
	vec3 lower = linear_rgb * vec3(12.92);
	vec3 higher = vec3(1.055)*pow(linear_rgb, vec3(1.0/2.4)) - vec3(0.055);
    return mix(higher, lower, cutoff);
}

/// alias for inverse eotf
vec3 oetf_srgb(in const vec3 linear_rgb){
	return inverse_eotf_srgb(linear_rgb);
}

// ----------------------------------------------------------------------
// ITU Transfer functions -- These apply to BT.601, BT.709, and BT.2020
// ----------------------------------------------------------------------

// non-linear to linear
// Converts a color from non-linear R'G'B' to linear RGB encoding.
vec3 eotf_itu(in const vec3 non_linear_rgb) {
    bvec3 cutoff = lessThan(non_linear_rgb, vec3(0.0181));
	vec3 lower = non_linear_rgb / vec3(4.5);
    vec3 higher = pow((non_linear_rgb+vec3(1.0993)) / vec3(1.0993), vec3(1.f/0.45f));
    return mix(higher, lower, cutoff);
}

// linear to non-linear
// Converts a color from linear RGB to non-linear R'G'B' encoding.
vec3 inverse_eotf_itu(in const vec3 linear_rgb) {
    bvec3 cutoff = lessThan(linear_rgb, vec3(0.0181));
	vec3 lower = linear_rgb * vec3(4.5);
    vec3 higher = 1.0993 * pow(linear_rgb, vec3(0.45))- vec3(1.0993 - 1.);
    return mix(higher, lower, cutoff);
}

/// alias for inverse_eotf_itu
vec3 oetf_itu(in const vec3 linear_rgb){
	return inverse_eotf_itu(linear_rgb);
}

// ---------------------------------------------------------------------- 
// HLG (Hybrid Log Gamma) Normalized
// ----------------------------------------------------------------------

// bt.2100-2: non-linear normalized R'G'B' to linear normalized RGB
vec3 eotf_hlg_normalized(in const vec3 non_linear_rgb){
    bvec3 cutoff = lessThan(non_linear_rgb, vec3(0.5));
	vec3 lower = (non_linear_rgb * non_linear_rgb) / vec3(3.f);
	const vec3 a = vec3(0.17883277);
	const vec3 b = vec3(1) - vec3(4) * a;
	const vec3 c = vec3(0.5) - a * log(vec3(4) * a);
    vec3 higher = 1/12. * (b + exp((non_linear_rgb - c) / a));
    return mix(higher, lower, cutoff);
}

// bt.2100-2: linear normalized RGB to non-linear normalized R'G'B'
vec3 inverse_eotf_hlg_normalized(in const vec3 linear_rgb){
    bvec3 cutoff = lessThan(linear_rgb, vec3(1.f/12.f));
	vec3 lower = pow(3 * linear_rgb, vec3(0.5));
	const vec3 a = vec3(0.17883277);
	const vec3 b = vec3(1) - vec3(4) * a;
	const vec3 c = vec3(0.5) - a * log(vec3(4) * a);
    vec3 higher = a * log((vec3(12)*linear_rgb) - b) + c;
    return mix(higher, lower, cutoff);
}

// alias for inverse_eotf_hlg_normalized
vec3 oetf_hlg_normalized(in const vec3 linear_rgb){
	return inverse_eotf_hlg_normalized(linear_rgb);
}

