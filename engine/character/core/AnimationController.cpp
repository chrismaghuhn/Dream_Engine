#include "engine/character/core/AnimationController.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine::character {

namespace {

struct BonePose {
    glm::vec3 t{0.f};
    glm::quat r{1.f, 0.f, 0.f, 0.f};
    glm::vec3 s{1.f};
};

template <typename T>
T lerp_channel(const std::vector<float>& times,
               const std::vector<T>& values,
               float t,
               T identity) {
    if (values.empty() || times.empty()) {
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
    const float denom = times[hi] - times[lo];
    const float alpha = denom > 1e-5f ? (t - times[lo]) / denom : 0.f;
    return glm::mix(values[lo], values[hi], alpha);
}

glm::quat slerp_channel(const std::vector<float>& times,
                        const std::vector<glm::quat>& values,
                        float t) {
    const glm::quat identity(1.f, 0.f, 0.f, 0.f);
    if (values.empty() || times.empty()) {
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
    const float denom = times[hi] - times[lo];
    const float alpha = denom > 1e-5f ? (t - times[lo]) / denom : 0.f;
    return glm::slerp(values[lo], values[hi], alpha);
}

glm::mat4 trs_matrix(const BonePose& pose) {
    const glm::mat4 T = glm::translate(glm::mat4(1.f), pose.t);
    const glm::mat4 R = glm::toMat4(pose.r);
    const glm::mat4 S = glm::scale(glm::mat4(1.f), pose.s);
    return T * R * S;
}

std::vector<BonePose> sample_pose(const AnimClip* clip,
                                  float time_seconds,
                                  const SkinnedMeshData& mesh) {
    const std::size_t bone_count = mesh.bones.size();
    std::vector<BonePose> pose(bone_count);
    if (!clip || clip->channels.empty()) {
        return pose;
    }

    std::unordered_map<std::string, const AnimChannel*> by_bone;
    by_bone.reserve(clip->channels.size());
    for (const AnimChannel& ch : clip->channels) {
        by_bone[ch.target_joint] = &ch;
    }

    for (std::size_t i = 0; i < bone_count; ++i) {
        auto it = by_bone.find(mesh.bones[i].name);
        if (it == by_bone.end()) {
            continue;
        }

        const AnimChannel& ch = *it->second;
        pose[i].t = lerp_channel(ch.translation_times, ch.translations,
                                 time_seconds, glm::vec3(0.f));
        pose[i].r = slerp_channel(ch.rotation_times, ch.rotations, time_seconds);
        pose[i].s = lerp_channel(ch.scale_times, ch.scales,
                                 time_seconds, glm::vec3(1.f));
    }

    return pose;
}

std::vector<glm::mat4> poses_to_bone_matrices(const std::vector<BonePose>& pose,
                                              const SkinnedMeshData& mesh) {
    const std::size_t bone_count = mesh.bones.size();
    std::vector<glm::mat4> local(bone_count, glm::mat4(1.f));
    std::vector<glm::mat4> global(bone_count, glm::mat4(1.f));

    for (std::size_t i = 0; i < bone_count; ++i) {
        local[i] = trs_matrix(pose[i]);
    }

    for (std::size_t i = 0; i < bone_count; ++i) {
        const int parent = mesh.bones[i].parent;
        if (parent < 0) {
            global[i] = local[i];
        } else {
            global[i] = global[static_cast<std::size_t>(parent)] * local[i];
        }
    }

    std::vector<glm::mat4> result(bone_count, glm::mat4(1.f));
    for (std::size_t i = 0; i < bone_count; ++i) {
        result[i] = i < mesh.inverse_bind_matrices.size()
            ? global[i] * mesh.inverse_bind_matrices[i]
            : global[i];
    }

    return result;
}

} // namespace

std::string AnimationController::select_locomotion(float speed, bool grounded) {
    if (!grounded) {
        return "Walk";
    }
    if (speed < 0.1f) {
        return "Idle";
    }
    if (speed < 3.0f) {
        return "Walk";
    }
    return "Run";
}

void AnimationController::tick(AnimationState& anim, const AnimClip* clip, float dt) {
    if (clip && clip->duration_seconds > 0.f) {
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

    if (anim.blend_weight > 0.f && anim.blend_duration > 1e-5f) {
        anim.blend_time += dt;
        anim.blend_weight -= dt / anim.blend_duration;
        if (anim.blend_weight <= 0.f) {
            anim.blend_weight = 0.f;
            anim.blend_clip.clear();
            anim.blend_time = 0.f;
        }
    }
}

void AnimationController::crossfade_to(AnimationState& anim,
                                       const std::string& new_clip,
                                       float blend_duration,
                                       bool looping) {
    if (anim.active_clip == new_clip) {
        return;
    }

    anim.blend_clip = anim.active_clip;
    anim.blend_time = anim.time_seconds;
    anim.blend_weight = anim.blend_clip.empty() ? 0.f : 1.f;
    anim.blend_duration = blend_duration;
    anim.active_clip = new_clip;
    anim.time_seconds = 0.f;
    anim.looping = looping;
    anim.speed = 1.f;
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
    const SkinnedMeshData& mesh,
    const AnimClip* blend_clip) {

    const std::vector<BonePose> primary = sample_pose(clip, anim.time_seconds, mesh);

    if (blend_clip && anim.blend_weight > 1e-5f) {
        const std::vector<BonePose> secondary =
            sample_pose(blend_clip, anim.blend_time, mesh);
        const float weight = std::clamp(anim.blend_weight, 0.f, 1.f);
        const std::size_t bone_count = mesh.bones.size();
        std::vector<BonePose> blended(bone_count);
        for (std::size_t i = 0; i < bone_count; ++i) {
            blended[i].t = glm::mix(primary[i].t, secondary[i].t, weight);
            blended[i].r = glm::slerp(primary[i].r, secondary[i].r, weight);
            blended[i].s = glm::mix(primary[i].s, secondary[i].s, weight);
        }
        return poses_to_bone_matrices(blended, mesh);
    }

    return poses_to_bone_matrices(primary, mesh);
}

} // namespace engine::character
