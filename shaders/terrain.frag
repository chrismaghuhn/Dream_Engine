#version 450

layout(location = 0) flat in uint v_material;
layout(location = 0) out vec4 out_color;

float hash11(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

vec3 material_base(uint material_id) {
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

vec3 shade_material(uint material_id, vec3 base) {
    const float h = hash11(float(material_id) * 17.0 + dot(base, vec3(1.0)));
    const float variation = (h - 0.5) * 0.12;
    vec3 albedo = base * (1.0 + variation);

    const float stripe = hash11(float(material_id) * 3.7);
    if (material_id == 2u) {
        albedo *= 0.92 + 0.08 * stripe;
    }
    if (material_id == 3u) {
        albedo.g *= 0.95 + 0.1 * stripe;
    }

    const vec3 light_dir = normalize(vec3(0.35, 0.85, 0.25));
    const float ndl = clamp(dot(normalize(vec3(0.0, 1.0, 0.0)), light_dir), 0.25, 1.0);
    const vec3 ambient = vec3(0.18, 0.20, 0.24);
    return albedo * ndl + ambient * 0.35;
}

void main() {
    const vec3 base = material_base(v_material);
    out_color = vec4(shade_material(v_material, base), 1.0);
}
