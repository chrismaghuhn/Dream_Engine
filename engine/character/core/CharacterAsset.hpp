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
    // Each TRS component has its own sampler in glTF and therefore its own
    // time array. Sharing a single key_times caused out-of-bounds access when
    // different channels of the same joint have different keyframe counts.
    std::vector<float> translation_times;
    std::vector<glm::vec3> translations;
    std::vector<float> rotation_times;
    std::vector<glm::quat> rotations;
    std::vector<float> scale_times;
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
    // World transform of the mesh node in the source GLB (column-major, GLM layout).
    // Contains the root scale factor (Meshy exports in cm → 0.01 scale to convert to m).
    // Apply this as a right-hand factor of the model matrix: model * node_transform.
    glm::mat4 node_transform{1.f};
};

} // namespace engine::character
