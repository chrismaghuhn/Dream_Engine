#version 450

layout(location = 0) flat in uint v_material;
layout(location = 0) out vec4 out_color;

void main() {
    if (v_material != 4u) {
        discard;
    }

    const vec3 deep = vec3(0.05, 0.22, 0.48);
    const vec3 shallow = vec3(0.18, 0.55, 0.78);
    const vec3 water = mix(deep, shallow, 0.55);
    out_color = vec4(water, 0.72);
}
