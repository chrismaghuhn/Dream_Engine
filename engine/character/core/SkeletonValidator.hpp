#pragma once

#include "engine/character/core/CharacterAsset.hpp"

#include <string>
#include <vector>

namespace engine::character {

struct ValidationResult {
    bool ok = false;
    std::string error;
};

class SkeletonValidator {
public:
    static constexpr int kMaxBones = 128;

    // Verifies that anim_bones is compatible with base_bones for animation retargeting.
    // Checks: same count, same name set, same parent hierarchy, count <= kMaxBones.
    [[nodiscard]] static ValidationResult validate_against_base(
        const std::vector<BoneInfo>& base_bones,
        const std::vector<BoneInfo>& anim_bones);
};

} // namespace engine::character
