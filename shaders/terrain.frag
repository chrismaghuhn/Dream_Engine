#version 450

layout(location = 0) flat in uint v_material;
layout(location = 1) flat in uint v_face;
layout(location = 2) in vec3 v_world;

// Block texture atlas as a 2D array. One layer per (material, face-category).
// Layer mapping is kept in sync with BlockTextureArray on the C++ side:
//   0 = stone, 1 = dirt, 2 = grass top, 3 = grass side.
layout(set = 0, binding = 1) uniform sampler2DArray u_block_tex;

layout(location = 0) out vec4 out_color;

int layer_for(uint material_id, uint face) {
    // Material IDs: STONE=1 DIRT=2 GRASS=3 (see BlockRegistry).
    if (material_id == 1u) {
        return 0; // stone
    }
    if (material_id == 2u) {
        return 1; // dirt
    }
    if (material_id == 3u) {
        // Grass: PY (2) = top toward sky; NY (3) = dirt underside; rest = grassy side.
        if (face == 2u) {
            return 2; // grass top
        }
        if (face == 3u) {
            return 1; // dirt underside
        }
        return 3; // grass side
    }
    return 0; // fallback: stone
}

// Project the world position onto the face plane to get tileable UVs.
// Face enum: PX=0 NX=1 PY=2 NY=3 PZ=4 NZ=5.
vec2 uv_for_face(uint face, vec3 world) {
    if (face == 0u) {
        return vec2(-world.z, -world.y);
    }
    if (face == 1u) {
        return vec2(world.z, -world.y);
    }
    if (face == 2u) {
        return vec2(world.x, world.z);
    }
    if (face == 3u) {
        return vec2(world.x, -world.z);
    }
    if (face == 4u) {
        return vec2(world.x, -world.y);
    }
    return vec2(-world.x, -world.y);
}

vec3 face_normal(uint face) {
    if (face == 0u) {
        return vec3(1.0, 0.0, 0.0);
    }
    if (face == 1u) {
        return vec3(-1.0, 0.0, 0.0);
    }
    if (face == 2u) {
        return vec3(0.0, 1.0, 0.0);
    }
    if (face == 3u) {
        return vec3(0.0, -1.0, 0.0);
    }
    if (face == 4u) {
        return vec3(0.0, 0.0, 1.0);
    }
    return vec3(0.0, 0.0, -1.0);
}

void main() {
    const int layer = layer_for(v_material, v_face);
    const vec2 uv = fract(uv_for_face(v_face, v_world));
    const vec3 albedo = texture(u_block_tex, vec3(uv, float(layer))).rgb;

    const vec3 n = face_normal(v_face);
    const vec3 light_dir = normalize(vec3(0.35, 0.85, 0.25));
    const float ndl = clamp(dot(n, light_dir), 0.2, 1.0);
    const vec3 ambient = vec3(0.18, 0.20, 0.24);
    out_color = vec4(albedo * ndl + ambient * 0.35, 1.0);
}
