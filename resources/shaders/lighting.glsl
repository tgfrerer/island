#include "scene.glsl"

//---------------------------------------------------
// from iq. https://www.shadertoy.com/view/Xds3zN
vec3 calc_normal ( in vec3 p )
{
    vec3 delta = vec3( 0.004, 0.0, 0.0 );
    int mtl;
    vec3 n;
    n.x = scene_distance ( p+delta.xyz ) - scene_distance( p-delta.xyz);
    n.y = scene_distance ( p+delta.zxy ) - scene_distance( p-delta.zxy);
    n.z = scene_distance ( p+delta.yzx ) - scene_distance( p-delta.yzx);
    return normalize( n );
}

//---------------------------------------------------
// from iq. https://www.shadertoy.com/view/Xds3zN
float ambient_occlusion( in vec3 pos, in vec3 nor )
{
    float occ = 0.0;
    float sca = 1.0;
    int mtl;
    for( int i=0; i<5; i++ )
    {
        float hr = 0.01 + 0.12*float(i)/4.0;
        vec3 aopos =  nor * hr + pos;
        float dd = scene_distance(aopos);
        occ += -(dd-hr)*sca;
        sca *= 0.95;
    }
    return clamp( 1.0 - 3.0*occ, 0.0, 1.0 );
}

//---------------------------------------------------
// from iq. https://www.shadertoy.com/view/Xds3zN
float soft_shadow( in vec3 ro, in vec3 rd, in float mint, in float tmax, float k )
{
    float res = 1.0;
    float t = mint;
    int mtl;
    for( int i=0; i<72; i++ )
    {
        float h = scene_distance( ro + rd*t);
        res = min( res, k*h/t );
        t += h;
        if( h<0.001 || t>tmax ) break;
    }
    return clamp( res, 0.0, 1.0 );
}

