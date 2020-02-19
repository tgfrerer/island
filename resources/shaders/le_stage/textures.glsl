// General Material

#define TEXTURE_COMPONENTS( NAME )                        \
    float u_ ## NAME ## Scale;                            \
    uint  u_ ## NAME ## UVSet;      /* which uv set    */ \
    uint  u_ ## NAME ## TextureIdx; /* which texture_id*/ \
    uint  u_ ## NAME ## _padding; /* padding because std140 requires vec4 as base-alignment as soon as there is a 3-component element*/ \

#if (defined HAS_NORMAL_MAP)        \
    || (defined HAS_EMISSIVE_MAP)   \
    || (defined HAS_BASE_COLOR_MAP)

layout (set=1, binding=1, std140) uniform UboTextureParams {  // base alignment: vec3
    #ifdef HAS_NORMAL_MAP
        #ifdef HAS_NORMAL_UV_TRANSFORM // only if has transform
        mat3  u_NormalUVTransform;  
        #endif
        TEXTURE_COMPONENTS( Normal )
    #endif
    #ifdef HAS_OCCLUSION_MAP
        #ifdef HAS_OCCLUSION_UV_TRANSFORM
            mat3  u_OcclusionUVTransform;  // only if has transform
        #endif
        TEXTURE_COMPONENTS( Occlusion )
    #endif
    #ifdef HAS_EMISSIVE_MAP
        #ifdef HAS_EMISSIVE_UV_TRANSFORM
            mat3 u_EmissiveUVTransform;  // only if has transform
        #endif
        TEXTURE_COMPONENTS( Emissive )
            vec3 u_EmissiveFactor;
            uint u_EmissiveFactor_padding;
    #endif
    // Metallic Roughness Material
    #ifdef HAS_BASE_COLOR_MAP
        #ifdef HAS_BASE_COLOR_UV_TRANSFORM // only if has transform
            mat3  u_BaseColorUVTransform;  
        #endif
        TEXTURE_COMPONENTS( BaseColor )
    #endif
    #ifdef HAS_METALLIC_ROUGHNESS_MAP
        #ifdef HAS_METALLICROUGHNESS_UV_TRANSFORM
           mat3  u_MetallicRoughnessUVTransform;   
        #endif
        TEXTURE_COMPONENTS( MetallicRoughness)
    #endif
};

#endif

#ifdef HAS_TEXTURES
    layout (set=1, binding=2) uniform sampler2D src_tex_unit[HAS_TEXTURES];
#endif 


// Specular Glossiness Material
#ifdef HAS_DIFFUSE_MAP
uniform sampler2D u_DiffuseSampler;
uniform int u_DiffuseUVSet;
uniform mat3 u_DiffuseUVTransform;
#endif

#ifdef HAS_SPECULAR_GLOSSINESS_MAP
uniform sampler2D u_SpecularGlossinessSampler;
uniform int u_SpecularGlossinessUVSet;
uniform mat3 u_SpecularGlossinessUVTransform;
#endif

// IBL
#ifdef USE_IBL
uniform samplerCube u_DiffuseEnvSampler;
uniform samplerCube u_SpecularEnvSampler;
uniform sampler2D u_brdfLUT;
#endif

vec2 getNormalUV()
{
    vec3 uv = vec3(v_tex_coord[0], 1.0);
#ifdef HAS_NORMAL_MAP
    uv.xy = v_tex_coord[u_NormalUVSet];
    #ifdef HAS_NORMAL_UV_TRANSFORM
    uv *= u_NormalUVTransform;
    #endif
#endif
    return uv.xy;
}

vec2 getEmissiveUV()
{
    vec3 uv = vec3(v_tex_coord[0], 1.0);
#ifdef HAS_EMISSIVE_MAP
    uv.xy = v_tex_coord[u_EmissiveUVSet];
    #ifdef HAS_EMISSIVE_UV_TRANSFORM
    uv *= u_EmissiveUVTransform;
    #endif
#endif

    return uv.xy;
}

vec2 getOcclusionUV()
{
    vec3 uv = vec3(v_tex_coord[0], 1.0);
#ifdef HAS_OCCLUSION_MAP
    uv.xy = v_tex_coord[u_OcclusionUVSet];
    #ifdef HAS_OCCLSION_UV_TRANSFORM
    uv *= u_OcclusionUVTransform;
    #endif
#endif
    return uv.xy;
}

vec2 getBaseColorUV()
{
    vec3 uv = vec3(v_tex_coord[0], 1.0);
#ifdef HAS_BASE_COLOR_MAP
    uv.xy = v_tex_coord[u_BaseColorUVSet];
    #ifdef HAS_BASECOLOR_UV_TRANSFORM
    uv *= u_BaseColorUVTransform;
    #endif
#endif
    return uv.xy;
}

vec2 getMetallicRoughnessUV()
{
    vec3 uv = vec3(v_tex_coord[0], 1.0);
#ifdef HAS_METALLIC_ROUGHNESS_MAP
    uv.xy = v_tex_coord[u_MetallicRoughnessUVSet];
    #ifdef HAS_METALLICROUGHNESS_UV_TRANSFORM
    uv *= u_MetallicRoughnessUVTransform;
    #endif
#endif
    return uv.xy;
}

vec2 getSpecularGlossinessUV()
{
    vec3 uv = vec3(v_tex_coord[0], 1.0);
#ifdef HAS_SPECULAR_GLOSSINESS_MAP
    uv.xy = v_tex_coord[u_SpecularGlossinessUVSet];
    #ifdef HAS_SPECULARGLOSSINESS_UV_TRANSFORM
    uv *= u_SpecularGlossinessUVTransform;
    #endif
#endif
    return uv.xy;
}

vec2 getDiffuseUV()
{
    vec3 uv = vec3(v_tex_coord[0], 1.0);
#ifdef HAS_DIFFUSE_MAP
    uv.xy = v_tex_coord[u_DiffuseUVSet];
    #ifdef HAS_DIFFUSE_UV_TRANSFORM
    uv *= u_DiffuseUVTransform;
    #endif
#endif
    return uv.xy;
}
