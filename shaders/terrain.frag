#version 450

layout(location = 0) flat in uint v_material;
layout(location = 0) out vec4 out_color;

vec3 material_color(uint material_id) {
    if (material_id == 1u) {
        return vec3(0.52, 0.54, 0.58);
    }
    if (material_id == 2u) {
        return vec3(0.45, 0.30, 0.15);
    }
    if (material_id == 3u) {
        return vec3(0.28, 0.58, 0.22);
    }
    return vec3(0.85, 0.25, 0.85);
}

void main() {
    out_color = vec4(material_color(v_material), 1.0);
}
