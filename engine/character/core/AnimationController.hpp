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

    // Advances anim.time_seconds and decays anim.blend_weight.
    // anim.speed = 0 during hitstop; this function still runs but advances no time.
    static void tick(AnimationState& anim, const AnimClip* clip, float dt);

    // Switch to new_clip with a crossfade. The previous clip fades out over blend_duration.
    // No-op if new_clip == anim.active_clip.
    static void crossfade_to(AnimationState& anim,
                             const std::string& new_clip,
                             float blend_duration = 0.12f,
                             bool looping = false);

    // Returns normalized clip time in [0, 1]. Returns 0 if clip is null.
    [[nodiscard]] static float normalized_time(const AnimationState& anim,
                                               const AnimClip* clip);

    // Samples bone matrices, blending blend_clip -> active_clip when blend_weight > 0.
    // Blending is done at TRS level (slerp rotation, lerp translation/scale).
    // blend_clip may be null; if so, no blending occurs.
    [[nodiscard]] static std::vector<glm::mat4> sample_bone_matrices(
        const AnimationState& anim,
        const AnimClip* clip,
        const SkinnedMeshData& mesh,
        const AnimClip* blend_clip = nullptr);
};

} // namespace engine::character
