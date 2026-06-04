// Combat animation inspector — diagnostic CLI (no combat behaviour changes).
//
// Loads the player character clip set and the combat attack table, then prints
// the real per-clip duration alongside the frame-data windows resolved into
// seconds and 60 Hz frames. Used to ground combat-feel tuning in measured clip
// lengths instead of guessed normalized values.

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterCatalog.hpp"
#include "engine/character/core/GltfIngest.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

namespace {

constexpr float kHz = 60.f;

const engine::character::AnimClip* find_clip(const engine::character::CharacterAsset& asset,
                                             const std::string& name) {
    for (const auto& clip : asset.clips) {
        if (clip.name == name) {
            return &clip;
        }
    }
    return nullptr;
}

int frames(float seconds) {
    return static_cast<int>(seconds * kHz + 0.5f);
}

} // namespace

int main() {
    using namespace engine::character;

    CharacterAsset player;
    try {
        player = CharacterCatalog::load_player_set();
    } catch (const std::exception& ex) {
        std::printf("ERROR loading player set: %s\n", ex.what());
        return 1;
    }

    const std::string attack_path =
        std::string(ENGINE_SOURCE_DIR) + "/assets/character/combat_attacks.txt";
    AttackTable attacks;
    try {
        attacks = AttackData::load(attack_path);
    } catch (const std::exception& ex) {
        std::printf("ERROR loading attack table: %s\n", ex.what());
        return 1;
    }

    std::printf("\n=== Player clip durations ===\n");
    std::printf("%-22s %8s %8s\n", "clip", "dur(s)", "frames");
    std::vector<const AnimClip*> sorted;
    for (const auto& clip : player.clips) {
        sorted.push_back(&clip);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const AnimClip* a, const AnimClip* b) { return a->name < b->name; });
    for (const AnimClip* clip : sorted) {
        std::printf("%-22s %8.3f %8d\n", clip->name.c_str(),
                    clip->duration_seconds, frames(clip->duration_seconds));
    }

    std::printf("\n=== Attack frame data (resolved to seconds @ %.0f Hz) ===\n", kHz);
    std::printf("%-18s %-16s %7s | %-13s %-13s %-13s | %-11s %-11s | %-12s | %-13s\n",
                "attack", "clip", "dur(s)",
                "startup", "active", "recov-tail",
                "cancel@", "dodge@", "chain-window", "eff(trim/scl)");

    std::vector<std::string> ids;
    for (const auto& [id, def] : attacks) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());

    for (const std::string& id : ids) {
        const AttackDef& def = attacks.at(id);
        const AnimClip* clip = find_clip(player, def.clip);
        if (!clip) {
            std::printf("%-18s %-16s   MISSING CLIP\n", id.c_str(), def.clip.c_str());
            continue;
        }

        const float dur          = clip->duration_seconds;
        const float startup_end  = def.hit_start_norm * dur;
        const float active_end    = def.hit_end_norm * dur;
        const float cancel_at      = def.cancel_start_norm * dur;
        const float dodge_at        = def.dodge_cancel_start_norm * dur;
        const float recov_tail        = dur - active_end;          // hitbox close -> clip end
        const float chain_window        = dur - cancel_at;            // time you may chain in

        // Effective on-screen attack length: the trimmed playback region
        // [clip_start, clip_end] divided by the playback speed multiplier.
        const float trim_start = def.clip_start_norm * dur;
        const float trim_end   = def.clip_end_norm   * dur;
        const float effective  = def.time_scale > 1e-5f
            ? (trim_end - trim_start) / def.time_scale
            : (trim_end - trim_start);

        std::printf("%-18s %-16s %7.3f | "
                    "0.00-%4.2f(%2df) %4.2f-%4.2f(%2df) %4.2f(%2df) | "
                    "%4.2f(%2df) %4.2f(%2df) | %5.2fs(%2df) | %5.2fs(%2df)\n",
                    id.c_str(), def.clip.c_str(), dur,
                    startup_end, frames(startup_end),
                    startup_end, active_end, frames(active_end - startup_end),
                    recov_tail, frames(recov_tail),
                    cancel_at, frames(cancel_at),
                    dodge_at, frames(dodge_at),
                    chain_window, frames(chain_window),
                    effective, frames(effective));
    }

    std::printf("\nNote: 'eff' = trimmed clip_window divided by time_scale = the actual "
                "on-screen attack length. Recovery now ends after recovery_seconds "
                "(timer-driven in combat_tick), independent of the raw clip tail.\n");

    // --- assets/Fight compatibility + duration scan ---------------------------
    // Tries to load each Fight GLB as an animation clip onto the PLAYER base
    // skeleton. Compatible clips print their duration; incompatible ones print
    // the rejection reason (skeleton mismatch). This decides whether Fight clips
    // can replace the over-long finishers without offline retargeting.
    const std::string player_base =
        std::string(ENGINE_SOURCE_DIR) +
        "/assets/Meshy_AI_Voxel_Dungeon_Explore_biped/"
        "Meshy_AI_Voxel_Dungeon_Explore_biped_Character_output.glb";
    const std::filesystem::path fight_dir =
        std::filesystem::path(ENGINE_SOURCE_DIR) / "assets" / "Fight";

    std::printf("\n=== assets/Fight scan (loaded onto player skeleton) ===\n");
    if (!std::filesystem::exists(fight_dir)) {
        std::printf("(no assets/Fight directory)\n\n");
        return 0;
    }

    std::vector<std::filesystem::path> fight_files;
    for (const auto& entry : std::filesystem::directory_iterator(fight_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".glb") {
            fight_files.push_back(entry.path());
        }
    }
    std::sort(fight_files.begin(), fight_files.end());

    int compatible = 0;
    for (const auto& path : fight_files) {
        try {
            CharacterAsset tmp = GltfIngest::load_base(player_base);
            GltfIngest::load_animation_clip(tmp, path.string(), "Probe");
            const float dur = tmp.clips.empty() ? 0.f : tmp.clips.back().duration_seconds;
            std::printf("  OK    %7.3fs  %s\n", dur, path.filename().string().c_str());
            ++compatible;
        } catch (const std::exception& ex) {
            std::printf("  SKIP            %s  (%s)\n",
                        path.filename().string().c_str(), ex.what());
        }
    }
    std::printf("\n%d / %zu Fight clips are skeleton-compatible with the player.\n\n",
                compatible, fight_files.size());
    return 0;
}
