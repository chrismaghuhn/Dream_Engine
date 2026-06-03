#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
} frame;

layout(set = 0, binding = 1) uniform BoneMatrices {
    mat4 bones[128];
} boneUBO;

layout(push_constant) uniform PushConst {
    mat4 model;
} pc;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;

void main() {
    mat4 skin =
        inWeights.x * boneUBO.bones[inJoints.x] +
        inWeights.y * boneUBO.bones[inJoints.y] +
        inWeights.z * boneUBO.bones[inJoints.z] +
        inWeights.w * boneUBO.bones[inJoints.w];

    vec4 skinnedPos    = skin * vec4(inPosition, 1.0);
    vec4 skinnedNormal = skin * vec4(inNormal,   0.0);

    gl_Position = frame.proj * frame.view * pc.model * skinnedPos;

    outUV     = inUV;
    outNormal = normalize(mat3(pc.model) * skinnedNormal.xyz);
}
