#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>

namespace engine::character {

struct AttackDef {
    std::string id;
    std::string clip;
    float hit_start_norm = 0.f;
    float hit_end_norm = 0.f;
    float range = 0.f;
    float radius = 0.f;
    float recovery_seconds = 0.2f;
    float cancel_start_norm = 0.7f;
    float dodge_cancel_start_norm = 0.6f;
    // Appended (preserve positional aggregate initialization in tests): trimmed
    // playback region of the raw clip, playback speed multiplier, per-attack hitstop.
    float clip_start_norm = 0.f;   // [0,1) of raw clip
    float clip_end_norm   = 1.f;   // (0,1] of raw clip
    float time_scale      = 1.f;   // >0; playback multiplier
    int   hitstop_frames  = 4;
};

using AttackTable = std::unordered_map<std::string, AttackDef>;

[[nodiscard]] inline float attack_clip_start_seconds(const AttackDef& def,
                                                     float clip_duration) {
    return def.clip_start_norm * clip_duration;
}
[[nodiscard]] inline float attack_clip_end_seconds(const AttackDef& def,
                                                   float clip_duration) {
    return def.clip_end_norm * clip_duration;
}

// Normalized [0,1] time within the trimmed region.
[[nodiscard]] inline float attack_norm_time(float time_seconds,
                                            const AttackDef& def,
                                            float clip_duration) {
    const float start_s = attack_clip_start_seconds(def, clip_duration);
    const float end_s   = attack_clip_end_seconds(def, clip_duration);
    const float span    = end_s - start_s;
    if (span <= 1e-5f) {
        return 1.f;
    }
    return std::clamp((time_seconds - start_s) / span, 0.f, 1.f);
}

class AttackData {
public:
    [[nodiscard]] static AttackTable load(const std::string& path);
};

} // namespace engine::character
