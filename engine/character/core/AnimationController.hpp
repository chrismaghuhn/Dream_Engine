#pragma once

#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace engine::character {

class AnimationController {
public:
    // Selects idle/walk/run based on horizontal speed and grounded state.
    [[nodiscard]] static std::string select_locomotion(float speed, bool grounded);

    // Advances anim.time_seconds by dt * anim.speed; wraps if looping.
    static void tick(AnimationState& anim, const AnimClip* clip, float dt);

    // Returns normalized clip time in [0, 1]. Returns 0 if clip is null.
    [[nodiscard]] static float normalized_time(const AnimationState& anim,
                                               const AnimClip* clip);

    // Samples the clip at anim.time_seconds and returns one world-space bone
    // matrix per bone (model space, pre-multiplied with inverse bind).
    // Returns identity matrices if clip is null.
    [[nodiscard]] static std::vector<glm::mat4> sample_bone_matrices(
        const AnimationState& anim,
        const AnimClip* clip,
        const SkinnedMeshData& mesh);
};

} // namespace engine::character
