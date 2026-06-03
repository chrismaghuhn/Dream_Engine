#include "engine/character/core/GltfIngest.hpp"

#include "engine/character/core/SkeletonValidator.hpp"

#include <spdlog/spdlog.h>

#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace engine::character {

namespace {

// ---- cgltf RAII wrapper ----

struct CgltfGuard {
    cgltf_data* data = nullptr;
    explicit CgltfGuard(cgltf_data* d) : data(d) {}
    ~CgltfGuard() {
        if (data) {
            cgltf_free(data);
        }
    }
    CgltfGuard(const CgltfGuard&) = delete;
    CgltfGuard& operator=(const CgltfGuard&) = delete;
};

cgltf_data* load_file(const std::string& path) {
    cgltf_options opts{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success) {
        throw std::runtime_error("GltfIngest: cgltf_parse_file failed for " + path);
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("GltfIngest: cgltf_load_buffers failed for " + path);
    }
    return data;
}

// ---- Accessor helpers ----

glm::vec3 read_vec3(const cgltf_accessor* acc, std::size_t i) {
    float out[3] = {0.f, 0.f, 0.f};
    cgltf_accessor_read_float(acc, i, out, 3);
    return {out[0], out[1], out[2]};
}

glm::vec2 read_vec2(const cgltf_accessor* acc, std::size_t i) {
    float out[2] = {0.f, 0.f};
    cgltf_accessor_read_float(acc, i, out, 2);
    return {out[0], out[1]};
}

glm::vec4 read_vec4(const cgltf_accessor* acc, std::size_t i) {
    float out[4] = {0.f, 0.f, 0.f, 0.f};
    cgltf_accessor_read_float(acc, i, out, 4);
    return {out[0], out[1], out[2], out[3]};
}

glm::uvec4 read_uvec4(const cgltf_accessor* acc, std::size_t i) {
    cgltf_uint out[4] = {0, 0, 0, 0};
    cgltf_accessor_read_uint(acc, i, out, 4);
    return {out[0], out[1], out[2], out[3]};
}

glm::quat read_quat(const cgltf_accessor* acc, std::size_t i) {
    float out[4] = {0.f, 0.f, 0.f, 1.f};
    cgltf_accessor_read_float(acc, i, out, 4);
    // cgltf XYZW, glm WXYZ
    return glm::quat(out[3], out[0], out[1], out[2]);
}

// ---- Skin / bone extraction ----

void extract_bones(const cgltf_skin* skin, std::vector<BoneInfo>& bones) {
    if (!skin) {
        return;
    }
    const std::size_t count = skin->joints_count;
    bones.resize(count);

    // Build joint pointer → index map.
    std::unordered_map<const cgltf_node*, int> joint_map;
    joint_map.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        joint_map[skin->joints[i]] = static_cast<int>(i);
        bones[i].name   = skin->joints[i]->name ? skin->joints[i]->name : ("bone_" + std::to_string(i));
        bones[i].parent = -1;
    }

    // Resolve parent indices.
    for (std::size_t i = 0; i < count; ++i) {
        const cgltf_node* node = skin->joints[i];
        if (node->parent) {
            auto it = joint_map.find(node->parent);
            if (it != joint_map.end()) {
                bones[i].parent = it->second;
            }
        }
    }
}

void extract_inverse_bind_matrices(const cgltf_skin* skin,
                                   std::vector<glm::mat4>& out) {
    if (!skin || !skin->inverse_bind_matrices) {
        return;
    }
    const std::size_t count = skin->joints_count;
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        float m[16] = {};
        cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16);
        // cgltf stores column-major, same as GLM.
        std::memcpy(&out[i][0][0], m, sizeof(m));
    }
}

// ---- Texture extraction ----

void extract_base_color_texture(const cgltf_material* mat, SkinnedMeshData& mesh) {
    if (!mat ||
        mat->pbr_metallic_roughness.base_color_texture.texture == nullptr) {
        // Fallback: 2×2 opaque-white texture.
        SPDLOG_WARN("GltfIngest: no base color texture — using 2x2 white fallback");
        mesh.base_color_rgba = {255, 255, 255, 255,
                                255, 255, 255, 255,
                                255, 255, 255, 255,
                                255, 255, 255, 255};
        mesh.base_color_width  = 2;
        mesh.base_color_height = 2;
        return;
    }

    const cgltf_texture* tex =
        mat->pbr_metallic_roughness.base_color_texture.texture;
    if (!tex->image || !tex->image->buffer_view) {
        SPDLOG_WARN("GltfIngest: base color texture has no embedded image data");
        mesh.base_color_rgba = {255, 255, 255, 255,
                                255, 255, 255, 255,
                                255, 255, 255, 255,
                                255, 255, 255, 255};
        mesh.base_color_width  = 2;
        mesh.base_color_height = 2;
        return;
    }

    // Copy raw image bytes — the GPU upload in CharacterPass will decode with stb_image.
    const cgltf_buffer_view* bv = tex->image->buffer_view;
    const auto* src = static_cast<const std::uint8_t*>(bv->buffer->data) + bv->offset;
    mesh.base_color_rgba.assign(src, src + bv->size);
    mesh.base_color_width  = -1; // sentinel: raw encoded bytes, decode on GPU upload
    mesh.base_color_height = -1;
}

// ---- Primitive mesh data ----

void extract_primitive(const cgltf_primitive& prim,
                       const cgltf_skin* skin,
                       SkinnedMeshData& mesh) {
    if (prim.type != cgltf_primitive_type_triangles) {
        throw std::runtime_error("GltfIngest: non-triangle primitive not supported");
    }

    const cgltf_accessor* pos_acc    = nullptr;
    const cgltf_accessor* norm_acc   = nullptr;
    const cgltf_accessor* uv_acc     = nullptr;
    const cgltf_accessor* joints_acc = nullptr;
    const cgltf_accessor* weights_acc = nullptr;

    for (std::size_t a = 0; a < prim.attributes_count; ++a) {
        const auto& attr = prim.attributes[a];
        switch (attr.type) {
        case cgltf_attribute_type_position: pos_acc    = attr.data; break;
        case cgltf_attribute_type_normal:   norm_acc   = attr.data; break;
        case cgltf_attribute_type_texcoord: if (attr.index == 0) uv_acc = attr.data; break;
        case cgltf_attribute_type_joints:   if (attr.index == 0) joints_acc = attr.data; break;
        case cgltf_attribute_type_weights:  if (attr.index == 0) weights_acc = attr.data; break;
        default: break;
        }
    }

    if (!pos_acc) {
        throw std::runtime_error("GltfIngest: mesh primitive has no POSITION");
    }
    if (!joints_acc || !weights_acc) {
        throw std::runtime_error("GltfIngest: skinned mesh has no JOINTS/WEIGHTS");
    }

    const std::size_t vert_count = pos_acc->count;
    mesh.positions.resize(vert_count);
    mesh.normals.resize(vert_count);
    mesh.uvs.resize(vert_count);
    mesh.joint_indices.resize(vert_count);
    mesh.joint_weights.resize(vert_count);

    for (std::size_t i = 0; i < vert_count; ++i) {
        mesh.positions[i]     = read_vec3(pos_acc, i);
        mesh.normals[i]       = norm_acc ? read_vec3(norm_acc, i) : glm::vec3(0.f, 1.f, 0.f);
        mesh.uvs[i]           = uv_acc   ? read_vec2(uv_acc, i)   : glm::vec2(0.f);
        mesh.joint_indices[i] = read_uvec4(joints_acc, i);
        mesh.joint_weights[i] = read_vec4(weights_acc, i);
    }

    if (prim.indices) {
        const std::size_t idx_count = prim.indices->count;
        mesh.indices.resize(idx_count);
        for (std::size_t i = 0; i < idx_count; ++i) {
            mesh.indices[i] = static_cast<std::uint32_t>(
                cgltf_accessor_read_index(prim.indices, i));
        }
    } else {
        mesh.indices.resize(vert_count);
        for (std::size_t i = 0; i < vert_count; ++i) {
            mesh.indices[i] = static_cast<std::uint32_t>(i);
        }
    }

    // Material / texture.
    extract_base_color_texture(prim.material, mesh);
}

// ---- Animation channel extraction ----

void extract_animation(const cgltf_animation& anim,
                       const std::vector<BoneInfo>& bones,
                       AnimClip& clip) {
    // Build joint-name→index lookup.
    std::unordered_map<std::string, int> bone_map;
    bone_map.reserve(bones.size());
    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        bone_map[bones[i].name] = i;
    }

    clip.name             = anim.name ? anim.name : "clip";
    clip.duration_seconds = 0.f;

    // Group channels by target joint.
    std::unordered_map<std::string, AnimChannel> by_joint;

    for (std::size_t ci = 0; ci < anim.channels_count; ++ci) {
        const cgltf_animation_channel& ch = anim.channels[ci];
        if (!ch.target_node || !ch.target_node->name) {
            continue;
        }
        const std::string joint_name = ch.target_node->name;
        if (bone_map.find(joint_name) == bone_map.end()) {
            continue; // not a skinned joint
        }

        const cgltf_animation_sampler* sampler = ch.sampler;
        const std::size_t key_count = sampler->input->count;

        AnimChannel& ac = by_joint[joint_name];
        ac.target_joint = joint_name;

        if (ac.key_times.empty()) {
            ac.key_times.resize(key_count);
            for (std::size_t k = 0; k < key_count; ++k) {
                float t = 0.f;
                cgltf_accessor_read_float(sampler->input, k, &t, 1);
                ac.key_times[k] = t;
                clip.duration_seconds = std::max(clip.duration_seconds, t);
            }
        }

        if (ch.target_path == cgltf_animation_path_type_translation) {
            ac.translations.resize(key_count);
            for (std::size_t k = 0; k < key_count; ++k) {
                ac.translations[k] = read_vec3(sampler->output, k);
            }
        } else if (ch.target_path == cgltf_animation_path_type_rotation) {
            ac.rotations.resize(key_count);
            for (std::size_t k = 0; k < key_count; ++k) {
                ac.rotations[k] = read_quat(sampler->output, k);
            }
        } else if (ch.target_path == cgltf_animation_path_type_scale) {
            ac.scales.resize(key_count);
            for (std::size_t k = 0; k < key_count; ++k) {
                ac.scales[k] = read_vec3(sampler->output, k);
            }
        }
    }

    clip.channels.reserve(by_joint.size());
    for (auto& [name, ac] : by_joint) {
        clip.channels.push_back(std::move(ac));
    }
}

// Find the first skin in the file.
const cgltf_skin* find_skin(const cgltf_data* data) {
    if (data->skins_count > 0) {
        return &data->skins[0];
    }
    return nullptr;
}

// Find the first skinned mesh primitive.
const cgltf_mesh* find_skinned_mesh(const cgltf_data* data) {
    for (std::size_t m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        for (std::size_t p = 0; p < mesh.primitives_count; ++p) {
            for (std::size_t a = 0; a < mesh.primitives[p].attributes_count; ++a) {
                if (mesh.primitives[p].attributes[a].type == cgltf_attribute_type_joints) {
                    return &mesh;
                }
            }
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------

CharacterAsset GltfIngest::load_base(const std::string& glb_path) {
    CgltfGuard guard(load_file(glb_path));
    const cgltf_data* data = guard.data;

    const cgltf_skin* skin = find_skin(data);
    if (!skin) {
        throw std::runtime_error("GltfIngest: no skin in " + glb_path);
    }

    const cgltf_mesh* mesh_node = find_skinned_mesh(data);
    if (!mesh_node) {
        throw std::runtime_error("GltfIngest: no skinned mesh in " + glb_path);
    }

    CharacterAsset asset;
    asset.source_path = glb_path;

    extract_bones(skin, asset.mesh.bones);
    extract_inverse_bind_matrices(skin, asset.mesh.inverse_bind_matrices);

    if (asset.mesh.bones.size() > static_cast<std::size_t>(SkeletonValidator::kMaxBones)) {
        throw std::runtime_error("GltfIngest: bone count " +
                                 std::to_string(asset.mesh.bones.size()) +
                                 " exceeds max " +
                                 std::to_string(SkeletonValidator::kMaxBones) +
                                 " in " + glb_path);
    }

    extract_primitive(mesh_node->primitives[0], skin, asset.mesh);

    SPDLOG_INFO("GltfIngest: loaded base '{}' — {} verts, {} bones",
                glb_path,
                asset.mesh.positions.size(),
                asset.mesh.bones.size());

    return asset;
}

void GltfIngest::load_animation_clip(CharacterAsset& asset,
                                     const std::string& anim_glb_path,
                                     const std::string& clip_name) {
    CgltfGuard guard(load_file(anim_glb_path));
    const cgltf_data* data = guard.data;

    if (data->animations_count == 0) {
        throw std::runtime_error("GltfIngest: no animations in " + anim_glb_path);
    }

    // Extract bones from the animation GLB's skin for validation.
    const cgltf_skin* anim_skin = find_skin(data);
    std::vector<BoneInfo> anim_bones;
    if (anim_skin) {
        extract_bones(anim_skin, anim_bones);
    } else {
        // Fallback: derive joint names from animation channel target nodes.
        std::unordered_map<std::string, int> seen;
        for (std::size_t ci = 0; ci < data->animations[0].channels_count; ++ci) {
            const cgltf_node* node = data->animations[0].channels[ci].target_node;
            if (node && node->name && seen.find(node->name) == seen.end()) {
                const int idx = static_cast<int>(anim_bones.size());
                seen[node->name] = idx;
                BoneInfo b;
                b.name   = node->name;
                b.parent = node->parent && seen.count(node->parent->name)
                               ? seen.at(node->parent->name)
                               : -1;
                anim_bones.push_back(std::move(b));
            }
        }
    }

    const ValidationResult val =
        SkeletonValidator::validate_against_base(asset.mesh.bones, anim_bones);
    if (!val.ok) {
        throw std::runtime_error("GltfIngest: skeleton mismatch in '" +
                                 anim_glb_path + "': " + val.error);
    }

    AnimClip clip;
    extract_animation(data->animations[0], asset.mesh.bones, clip);
    if (!clip_name.empty()) {
        clip.name = clip_name;
    }

    SPDLOG_INFO("GltfIngest: loaded clip '{}' from '{}' — {:.2f}s, {} channels",
                clip.name,
                anim_glb_path,
                clip.duration_seconds,
                clip.channels.size());

    asset.clips.push_back(std::move(clip));
}

} // namespace engine::character
