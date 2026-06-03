#include "engine/character/core/SkeletonValidator.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace engine::character {

ValidationResult SkeletonValidator::validate_against_base(
    const std::vector<BoneInfo>& base_bones,
    const std::vector<BoneInfo>& anim_bones) {

    if (base_bones.size() > static_cast<std::size_t>(kMaxBones)) {
        return {false, "base skeleton exceeds max bone count of " +
                            std::to_string(kMaxBones)};
    }
    if (anim_bones.size() > static_cast<std::size_t>(kMaxBones)) {
        return {false, "animation skeleton exceeds max bone count of " +
                            std::to_string(kMaxBones)};
    }
    if (base_bones.size() != anim_bones.size()) {
        return {false, "joint count mismatch: base=" + std::to_string(base_bones.size()) +
                            " anim=" + std::to_string(anim_bones.size())};
    }

    // Build name→index map for base skeleton.
    std::unordered_map<std::string, int> base_name_to_index;
    base_name_to_index.reserve(base_bones.size());
    for (int i = 0; i < static_cast<int>(base_bones.size()); ++i) {
        base_name_to_index[base_bones[i].name] = i;
    }

    // Verify every anim bone name exists in base.
    for (const BoneInfo& ab : anim_bones) {
        if (base_name_to_index.find(ab.name) == base_name_to_index.end()) {
            return {false, "animation has unknown joint '" + ab.name + "' not in base"};
        }
    }

    // Build name→parent-name map for base.
    auto parent_name = [&](const std::vector<BoneInfo>& bones, int idx) -> std::string {
        if (idx < 0 || idx >= static_cast<int>(bones.size())) {
            return "";
        }
        const int parent_idx = bones[static_cast<std::size_t>(idx)].parent;
        if (parent_idx < 0) {
            return "";
        }
        return bones[static_cast<std::size_t>(parent_idx)].name;
    };

    // For each anim bone, verify the parent name matches the base hierarchy.
    std::unordered_map<std::string, int> anim_name_to_index;
    anim_name_to_index.reserve(anim_bones.size());
    for (int i = 0; i < static_cast<int>(anim_bones.size()); ++i) {
        anim_name_to_index[anim_bones[i].name] = i;
    }

    for (int ai = 0; ai < static_cast<int>(anim_bones.size()); ++ai) {
        const BoneInfo& ab = anim_bones[static_cast<std::size_t>(ai)];
        const int base_i   = base_name_to_index.at(ab.name);
        const std::string base_parent = parent_name(base_bones, base_i);
        const std::string anim_parent = parent_name(anim_bones, ai);
        if (base_parent != anim_parent) {
            return {false, "parent hierarchy mismatch for joint '" + ab.name +
                               "': base parent='" + base_parent +
                               "' anim parent='" + anim_parent + "'"};
        }
    }

    return {true, {}};
}

} // namespace engine::character
