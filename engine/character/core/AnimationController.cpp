#include "engine/character/core/AnimationController.hpp"

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::character {

namespace {

// Linear interpolation helpers for animation channels.

template <typename T>
T lerp_channel(const std::vector<float>& times,
               const std::vector<T>& values,
               float t,
               T identity) {
    if (values.empty()) {
        return identity;
    }
    if (values.size() == 1 || t <= times.front()) {
        return values.front();
    }
    if (t >= times.back()) {
        return values.back();
    }
    // Binary search for the frame just before t.
    auto it = std::lower_bound(times.begin(), times.end(), t);
    const std::size_t hi = static_cast<std::size_t>(it - times.begin());
    const std::size_t lo = hi - 1;
    const float alpha = (t - times[lo]) / (times[hi] - times[lo]);
    return glm::mix(values[lo], values[hi], alpha);
}

glm::quat slerp_channel(const std::vector<float>& times,
                        const std::vector<glm::quat>& values,
                        float t) {
    const glm::quat identity(1.f, 0.f, 0.f, 0.f);
    if (values.empty()) {
        return identity;
    }
    if (values.size() == 1 || t <= times.front()) {
        return values.front();
    }
    if (t >= times.back()) {
        return values.back();
    }
    auto it = std::lower_bound(times.begin(), times.end(), t);
    const std::size_t hi = static_cast<std::size_t>(it - times.begin());
    const std::size_t lo = hi - 1;
    const float alpha = (t - times[lo]) / (times[hi] - times[lo]);
    return glm::slerp(values[lo], values[hi], alpha);
}

glm::mat4 trs_matrix(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    const glm::mat4 T = glm::translate(glm::mat4(1.f), t);
    const glm::mat4 R = glm::toMat4(r);
    const glm::mat4 S = glm::scale(glm::mat4(1.f), s);
    return T * R * S;
}

} // namespace

// ---------------------------------------------------------------------------

std::string AnimationController::select_locomotion(float speed, bool grounded) {
    if (!grounded || speed < 0.1f) {
        return "Walk"; // idle — use first frame of Walk until we have a dedicated idle clip
    }
    if (speed < 3.0f) {
        return "Walk";
    }
    return "Run";
}

void AnimationController::tick(AnimationState& anim, const AnimClip* clip, float dt) {
    if (!clip || clip->duration_seconds <= 0.f) {
        return;
    }
    anim.time_seconds += dt * anim.speed;
    if (anim.looping) {
        anim.time_seconds = std::fmod(anim.time_seconds, clip->duration_seconds);
        if (anim.time_seconds < 0.f) {
            anim.time_seconds += clip->duration_seconds;
        }
    } else {
        anim.time_seconds = std::min(anim.time_seconds, clip->duration_seconds);
    }
}

float AnimationController::normalized_time(const AnimationState& anim, const AnimClip* clip) {
    if (!clip || clip->duration_seconds <= 0.f) {
        return 0.f;
    }
    return anim.time_seconds / clip->duration_seconds;
}

std::vector<glm::mat4> AnimationController::sample_bone_matrices(
    const AnimationState& anim,
    const AnimClip* clip,
    const SkinnedMeshData& mesh) {

    const std::size_t bone_count = mesh.bones.size();
    std::vector<glm::mat4> local(bone_count, glm::mat4(1.f));
    std::vector<glm::mat4> global(bone_count, glm::mat4(1.f));

    if (clip && !clip->channels.empty()) {
        const float t = anim.time_seconds;

        // Build bone-name → channel map.
        std::unordered_map<std::string, const AnimChannel*> by_bone;
        by_bone.reserve(clip->channels.size());
        for (const AnimChannel& ch : clip->channels) {
            by_bone[ch.target_joint] = &ch;
        }

        // Sample local TRS for each bone.
        for (std::size_t i = 0; i < bone_count; ++i) {
            auto it = by_bone.find(mesh.bones[i].name);
            if (it == by_bone.end()) {
                continue; // bone not animated — stays identity (bind pose)
            }
            const AnimChannel& ch = *it->second;

            const glm::vec3 trans = lerp_channel(ch.key_times, ch.translations,
                                                  t, glm::vec3(0.f));
            const glm::quat rot   = slerp_channel(ch.key_times, ch.rotations, t);
            const glm::vec3 scale = lerp_channel(ch.key_times, ch.scales,
                                                  t, glm::vec3(1.f));
            local[i] = trs_matrix(trans, rot, scale);
        }
    }

    // Compute global transforms in bone-index order (parent always has smaller index
    // in well-formed glTF skins, but we handle arbitrary order via parent lookup).
    for (std::size_t i = 0; i < bone_count; ++i) {
        const int parent = mesh.bones[i].parent;
        if (parent < 0) {
            global[i] = local[i];
        } else {
            global[i] = global[static_cast<std::size_t>(parent)] * local[i];
        }
    }

    // Final bone matrix = global_transform * inverse_bind_matrix.
    std::vector<glm::mat4> result(bone_count, glm::mat4(1.f));
    for (std::size_t i = 0; i < bone_count; ++i) {
        if (i < mesh.inverse_bind_matrices.size()) {
            result[i] = global[i] * mesh.inverse_bind_matrices[i];
        } else {
            result[i] = global[i];
        }
    }

    return result;
}

} // namespace engine::character
