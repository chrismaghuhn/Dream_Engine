#version 450

layout(location = 0) flat in uint v_material;
layout(location = 1) in vec2 v_light_levels;

layout(location = 0) out vec4 out_color;

void main() {
    if (v_material != 4u) {
        discard;
    }

    const vec3 deep = vec3(0.05, 0.22, 0.48);
    const vec3 shallow = vec3(0.18, 0.55, 0.78);
    vec3 water = mix(deep, shallow, 0.55);
    const vec3 sky_tint   = vec3(0.85, 0.92, 1.00);
    const vec3 block_tint = vec3(1.00, 0.82, 0.55);
    water *= v_light_levels.x * sky_tint + v_light_levels.y * block_tint + 0.25;
    out_color = vec4(water, 0.72);
}