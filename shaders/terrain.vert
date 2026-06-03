#version 450

layout(location = 0) in uint in_packed_pos;
layout(location = 1) in uint in_material;

layout(set = 0, binding = 0) uniform FrameUniform {
    mat4 view;
    mat4 proj;
    vec4 render_origin;
} frame;

layout(push_constant) uniform Push {
    vec3 model_translation;
} push;

layout(location = 0) flat out uint v_material;
layout(location = 1) flat out uint v_face;
layout(location = 2) out vec3 v_world;

void main() {
    const uint packed = in_packed_pos;
    const float x = float(packed & 31u);
    const float y = float((packed >> 5u) & 31u);
    const float z = float((packed >> 10u) & 31u);
    v_face = (packed >> 15u) & 7u;
    const vec3 world = push.model_translation + vec3(x, y, z);
    gl_Position = frame.proj * frame.view * vec4(world, 1.0);
    v_material = in_material;
    // Render-space position (origin-rebased, block-aligned). Used to derive
    // per-block tiling UVs in the fragment shader via fract().
    v_world = world;
}
