#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::character {

struct BoneInfo {
    std::string name;
    int parent = -1;
};

struct AnimChannel {
    std::string target_joint;
    std::vector<float> key_times;
    std::vector<glm::vec3> translations;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;
};

struct AnimClip {
    std::string name;
    float duration_seconds = 0.f;
    std::vector<AnimChannel> channels;
};

struct SkinnedMeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::uvec4> joint_indices;
    std::vector<glm::vec4> joint_weights;
    std::vector<std::uint32_t> indices;
    std::vector<BoneInfo> bones;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<std::uint8_t> base_color_rgba;
    int base_color_width = 0;
    int base_color_height = 0;
};

struct CharacterAsset {
    std::string source_path;
    SkinnedMeshData mesh;
    std::vector<AnimClip> clips;
};

} // namespace engine::character
