#if defined(NUM_JOINT_SETS) && defined(NUM_JOINT_WEIGHTS_SET)

mat4 getSkinningMatrix() 
{
    mat4 skin = mat4(0);

    #if (NUM_JOINT_WEIGHTS_SET == NUM_JOINT_SETS) && (NUM_JOINT_SETS > 0)
    skin +=
        a_Weight[0].x * u_jointMatrix[a_Joint[0].x] +
        a_Weight[0].y * u_jointMatrix[a_Joint[0].y] +
        a_Weight[0].z * u_jointMatrix[a_Joint[0].z] +
        a_Weight[0].w * u_jointMatrix[a_Joint[0].w];
    #endif

    #if (NUM_JOINT_WEIGHTS_SET == NUM_JOINT_SETS) && (NUM_JOINT_SETS > 1)
    skin +=
        a_Weight[1].x * u_jointMatrix[a_Joint[1].x] +
        a_Weight[1].y * u_jointMatrix[a_Joint[1].y] +
        a_Weight[1].z * u_jointMatrix[a_Joint[1].z] +
        a_Weight[1].w * u_jointMatrix[a_Joint[1].w];
    #endif

    return skin;
}

mat4 getSkinningNormalMatrix() 
{
    mat4 skin = mat4(0);

    #if (NUM_JOINT_WEIGHTS_SET == NUM_JOINT_SETS) && (NUM_JOINT_SETS > 0)
    skin +=
        a_Weight[0].x * u_jointNormalMatrix[a_Joint[0].x] +
        a_Weight[0].y * u_jointNormalMatrix[a_Joint[0].y] +
        a_Weight[0].z * u_jointNormalMatrix[a_Joint[0].z] +
        a_Weight[0].w * u_jointNormalMatrix[a_Joint[0].w];
    #endif

    #if (NUM_JOINT_WEIGHTS_SET == NUM_JOINT_SETS) && (NUM_JOINT_SETS > 1)
    skin +=
        a_Weight[1].x * u_jointNormalMatrix[a_Joint[1].x] +
        a_Weight[1].y * u_jointNormalMatrix[a_Joint[1].y] +
        a_Weight[1].z * u_jointNormalMatrix[a_Joint[1].z] +
        a_Weight[1].w * u_jointNormalMatrix[a_Joint[1].w];
    #endif

    return skin;
}

#endif // !USE_SKINNING

#ifdef MORPH_TARGET_COUNT
void getTargetPosition(inout vec3 pos)
{
    for (int i = 0; i != MORPH_TARGET_COUNT; i++){
        pos += morphTargetWeights[i / MORPH_TARGET_COUNT][i % MORPH_TARGET_COUNT] * a_pos[i+1];
    }
}

#ifdef LOC_NORMALS
void getTargetNormal(inout vec3 normal)
{
    for (int i = 0; i != MORPH_TARGET_COUNT; i++){
        normal += morphTargetWeights[i / MORPH_TARGET_COUNT][i % MORPH_TARGET_COUNT] * a_normal[i+1];
    }
}
#endif

#ifdef LOC_TANGENTS
void getTargetTangent(inout vec4 tangent)
{
    for (int i = 0; i != MORPH_TARGET_COUNT; i++){
        tangent += morphTargetWeights[i / MORPH_TARGET_COUNT][i % MORPH_TARGET_COUNT] * a_tangent[i+1];
    }
}
#endif

#endif // !MORPH_TARGET_COUNT
