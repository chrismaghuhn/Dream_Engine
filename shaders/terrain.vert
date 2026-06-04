#version 450

layout(location = 0) in uint in_packed_pos;
layout(location = 1) in uint in_material;
layout(location = 2) in uint in_ao;
layout(location = 3) in uint in_light;

layout(set = 0, binding = 0) uniform FrameUniform {
    mat4 view;
    mat4 proj;
    vec4 render_origin;
} frame;

layout(push_constant) uniform Push {
    vec3 model_translation;
    float vertex_scale;
} push;

layout(location = 0) flat out uint v_material;
layout(location = 1) flat out uint v_face;
layout(location = 2) out float v_ao_mul;
layout(location = 3) out vec2 v_light_levels;
layout(location = 4) out vec3 v_world;

float ao_mul_from_u8(uint ao) {
    if (ao == 0u) return 0.55;
    if (ao == 1u) return 0.72;
    if (ao == 2u) return 0.86;
    return 1.00;
}

void main() {
    const uint packed = in_packed_pos;
    const float x = float(packed & 31u);
    const float y = float((packed >> 5u) & 31u);
    const float z = float((packed >> 10u) & 31u);
    v_face = (packed >> 15u) & 7u;
    const vec3 world = push.model_translation + vec3(x, y, z) * push.vertex_scale;
    gl_Position = frame.proj * frame.view * vec4(world, 1.0);
    v_material = in_material;
    v_world = world;
    v_ao_mul = ao_mul_from_u8(in_ao);
    v_light_levels = vec2(float(in_light >> 4u) / 15.0, float(in_light & 15u) / 15.0);
}