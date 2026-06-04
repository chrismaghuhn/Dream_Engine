# Combat Feel Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing C++ combo system feel snappy by adding data-driven clip trimming + playback speed, wiring the unused `recovery_seconds`, replacing the 6.5s/9.4s finishers with short `assets/Fight` boxing clips, and adding per-attack hitstop.

**Architecture:** Additive changes to the data-driven combat stack. `AttackDef` gains a trimmed playback region (`clip_start_norm`/`clip_end_norm`), a `time_scale`, and `hitstop_frames`. A shared header helper `attack_norm_time()` re-bases all windows onto the trimmed region. `combat_tick` seeds playback time/speed on attack start, clamps playback to the trimmed end, and drives recovery from an accumulated `recovery_timer` instead of waiting for clip end.

**Tech Stack:** C++20, flecs, GLM, Catch2, CMake (MSVC). Headless tests link `engine_character_core` (no Vulkan).

**Spec:** `docs/superpowers/specs/2026-06-04-combat-feel-tuning-design.md`

> Field-order note: the spec lists the new `AttackDef` fields next to `clip`. The
> implementation **appends** them after `dodge_cancel_start_norm` so existing
> 9-field aggregate initializers in `tests/character/test_combo_fsm.cpp` keep
> compiling. Behaviour is identical.

**Build/test commands used throughout:**
```bash
cmake -S . -B build
cmake --build build --config Debug --target character_tests
ctest --test-dir build -C Debug -R <name> --output-on-failure
```

---

## Task 1: AttackDef fields, helpers, parser, invariants

**Files:**
- Modify: `engine/character/core/AttackData.hpp`
- Modify: `engine/character/core/AttackData.cpp`
- Test: `tests/character/test_attack_data.cpp`

- [ ] **Step 1: Write the failing test (new fields + invariants)**

Add this temp-file helper near the top of `tests/character/test_attack_data.cpp`
(after the includes), so the new tests never mutate the source tree:

```cpp
#include <filesystem>

// Writes an attack-table snippet to a unique file under the OS temp dir and
// returns its path. Avoids polluting assets/character and survives crashes
// (the OS temp dir is disposable). Caller removes it when done.
static std::string write_temp_attack(const std::string& stem, const std::string& body) {
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() /
        ("combat_attacks_" + stem + "_" +
         std::to_string(::Catch::getSeed()) + ".txt");
    std::ofstream f(p);
    f << body;
    f.close();
    return p.string();
}
```

> If `Catch::getSeed()` is unavailable in this Catch2 version, use a static counter
> (`static int n = 0; ++n;`) for the unique suffix instead.

Append these test cases:

```cpp
TEST_CASE("AttackData parses clip_window, time_scale, hitstop", "[attack_data]") {
    const std::string tmp = write_temp_attack("trim",
        "attack trimmed {\n"
        "    clip TestClip\n"
        "    clip_window 0.15 0.65\n"
        "    time_scale 1.4\n"
        "    hit_window 0.30 0.50\n"
        "    range 1.0\n    radius 0.3\n    recovery 0.18\n"
        "    cancel_window 0.55\n    dodge_cancel_window 0.45\n"
        "    hitstop 6\n}\n");
    const auto table = engine::character::AttackData::load(tmp);
    REQUIRE(table.count("trimmed"));
    const auto& d = table.at("trimmed");
    REQUIRE(d.clip_start_norm == Catch::Approx(0.15f));
    REQUIRE(d.clip_end_norm   == Catch::Approx(0.65f));
    REQUIRE(d.time_scale      == Catch::Approx(1.4f));
    REQUIRE(d.hitstop_frames  == 6);
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData defaults trim/scale/hitstop when omitted", "[attack_data]") {
    const std::string tmp = write_temp_attack("defaults",
        "attack plain {\n    clip C\n    hit_window 0.30 0.50\n    range 1.0\n"
        "    radius 0.3\n    recovery 0.2\n    cancel_window 0.60\n}\n");
    const auto table = engine::character::AttackData::load(tmp);
    const auto& d = table.at("plain");
    REQUIRE(d.clip_start_norm == Catch::Approx(0.0f));
    REQUIRE(d.clip_end_norm   == Catch::Approx(1.0f));
    REQUIRE(d.time_scale      == Catch::Approx(1.0f));
    REQUIRE(d.hitstop_frames  == 4);
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData rejects clip_window with start >= end", "[attack_data]") {
    const std::string tmp = write_temp_attack("badtrim",
        "attack bad {\n    clip C\n    clip_window 0.70 0.30\n"
        "    hit_window 0.30 0.50\n    range 1.0\n    radius 0.3\n"
        "    recovery 0.2\n    cancel_window 0.60\n}\n");
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData rejects non-positive time_scale", "[attack_data]") {
    const std::string tmp = write_temp_attack("badscale",
        "attack bad {\n    clip C\n    time_scale 0\n"
        "    hit_window 0.30 0.50\n    range 1.0\n    radius 0.3\n"
        "    recovery 0.2\n    cancel_window 0.60\n}\n");
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData rejects fractional hitstop", "[attack_data]") {
    const std::string tmp = write_temp_attack("fracstop",
        "attack bad {\n    clip C\n    hitstop 3.9\n"
        "    hit_window 0.30 0.50\n    range 1.0\n    radius 0.3\n"
        "    recovery 0.2\n    cancel_window 0.60\n}\n");
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}

TEST_CASE("attack_norm_time maps onto trimmed region", "[attack_data]") {
    engine::character::AttackDef d;
    d.clip_start_norm = 0.2f;
    d.clip_end_norm   = 0.6f;            // region = [0.4s, 1.2s] for a 2s clip
    const float dur = 2.0f;
    REQUIRE(engine::character::attack_norm_time(0.4f, d, dur) == Catch::Approx(0.0f));
    REQUIRE(engine::character::attack_norm_time(1.2f, d, dur) == Catch::Approx(1.0f));
    REQUIRE(engine::character::attack_norm_time(0.8f, d, dur) == Catch::Approx(0.5f));
    REQUIRE(engine::character::attack_norm_time(0.0f, d, dur) == Catch::Approx(0.0f)); // clamp
    REQUIRE(engine::character::attack_norm_time(5.0f, d, dur) == Catch::Approx(1.0f)); // clamp
    REQUIRE(engine::character::attack_clip_start_seconds(d, dur) == Catch::Approx(0.4f));
    REQUIRE(engine::character::attack_clip_end_seconds(d, dur)   == Catch::Approx(1.2f));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target character_tests`
Expected: COMPILE FAILURE — `clip_start_norm`, `time_scale`, `hitstop_frames`, `attack_norm_time`, `attack_clip_start_seconds`, `attack_clip_end_seconds` do not exist.

- [ ] **Step 3: Add fields + helpers to `AttackData.hpp`**

Replace the whole file with:

```cpp
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
    // Appended (see plan field-order note): trimmed playback region of the raw
    // clip, playback speed multiplier, and per-attack hitstop on a confirmed hit.
    float clip_start_norm = 0.f;   // [0,1) of raw clip
    float clip_end_norm   = 1.f;   // (0,1] of raw clip
    float time_scale      = 1.f;   // >0; playback multiplier
    int   hitstop_frames  = 4;
};

using AttackTable = std::unordered_map<std::string, AttackDef>;

// Seconds at which the trimmed playback region starts/ends within the raw clip.
[[nodiscard]] inline float attack_clip_start_seconds(const AttackDef& def,
                                                     float clip_duration) {
    return def.clip_start_norm * clip_duration;
}
[[nodiscard]] inline float attack_clip_end_seconds(const AttackDef& def,
                                                   float clip_duration) {
    return def.clip_end_norm * clip_duration;
}

// Normalized [0,1] time within the trimmed region. time_seconds is
// AnimationState::time_seconds; clip_duration is the raw clip length.
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
```

- [ ] **Step 4: Parse new keys + invariants in `AttackData.cpp`**

In `parse_attack_block`, add these branches inside the field `while` loop, after the existing `dodge_cancel_window` branch and before the `else { unknown field }`:

Add `#include <cmath>` to the top of `AttackData.cpp` (for `std::floor`). Then add
these branches inside the field `while` loop:

```cpp
        } else if (field == "clip_window") {
            check_dup("clip_window");
            def.clip_start_norm = lex.read_float("clip_window.start");
            def.clip_end_norm   = lex.read_float("clip_window.end");
        } else if (field == "time_scale") {
            check_dup("time_scale");
            def.time_scale = lex.read_float("time_scale");
        } else if (field == "hitstop") {
            check_dup("hitstop");
            // Frame counts are integers: reject fractional values rather than
            // silently truncating 3.9 -> 3.
            const float hs = lex.read_float("hitstop");
            if (hs < 0.f || std::floor(hs) != hs) {
                throw std::runtime_error(
                    lex.source + ":" + std::to_string(lex.line) + ":" +
                    std::to_string(lex.col) +
                    ": hitstop must be a non-negative integer in attack '" + id + "'");
            }
            def.hitstop_frames = static_cast<int>(hs);
```

Then, after the existing `dodge_cancel_window` invariant check (the `if (def.dodge_cancel_start_norm > def.cancel_start_norm)` block), add:

```cpp
    if (def.clip_start_norm < 0.f || def.clip_end_norm > 1.f ||
        def.clip_start_norm >= def.clip_end_norm) {
        throw std::runtime_error(
            lex.source + ": invalid clip_window in attack '" + id + "'");
    }
    if (def.time_scale <= 0.f) {
        throw std::runtime_error(
            lex.source + ": time_scale must be > 0 in attack '" + id + "'");
    }
```

(`hitstop` integer/sign validation happens inline in the parse branch above, so no
separate invariant is needed here.)

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R attack_data --output-on-failure`
Expected: PASS (all `[attack_data]` cases, including the pre-existing size==8 test which still parses the unchanged `combat_attacks.txt`).

- [ ] **Step 6: Commit**

```bash
git add engine/character/core/AttackData.hpp engine/character/core/AttackData.cpp tests/character/test_attack_data.cpp
git commit -m "feat: add clip trim, time_scale, hitstop fields to AttackDef"
```

---

## Task 2: Add `recovery_timer` to CombatController

**Files:**
- Modify: `engine/character/core/CharacterComponents.hpp`

- [ ] **Step 1: Add the field**

In `struct CombatController`, after `bool hit_consumed = false;`, add:

```cpp
    // Accumulates dt while in Recovery; drives the fixed recovery duration.
    float recovery_timer = 0.f;
```

- [ ] **Step 2: Build to verify it compiles**

Run: `cmake --build build --config Debug --target character_tests`
Expected: PASS (compiles; no behaviour change yet).

- [ ] **Step 3: Commit**

```bash
git add engine/character/core/CharacterComponents.hpp
git commit -m "feat: add recovery_timer to CombatController"
```

---

## Task 3: Rewrite combat_tick (trim, seed, recovery timer, clamp, fail-fast)

**Files:**
- Modify: `engine/character/core/CombatController.cpp`
- Test: `tests/character/test_combo_fsm.cpp`

- [ ] **Step 1: Replace the clip-end recovery test + add new tests**

In `tests/character/test_combo_fsm.cpp`, **replace** the test case
`"no buffer input in Recovery returns to Idle after clip ends"` with:

```cpp
TEST_CASE("Recovery returns to Idle after recovery_seconds", "[combo_fsm]") {
    auto attacks = make_table();
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.26f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);

    // punch.recovery_seconds = 0.2 -> needs ~12 frames of dt. Keep time fixed.
    for (int i = 0; i < 13; ++i) {
        combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    }
    REQUIRE(combat.phase == CombatPhase::Idle);
}
```

Then **append** these new test cases:

```cpp
TEST_CASE("start_attack seeds trimmed start time and time_scale", "[combo_fsm]") {
    auto attacks = make_table();
    attacks["punch"].clip_start_norm = 0.2f;   // 0.2 * 0.5s = 0.10s
    attacks["punch"].clip_end_norm   = 0.8f;
    attacks["punch"].time_scale      = 1.5f;
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);

    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(anim.time_seconds == Catch::Approx(0.10f));
    REQUIRE(anim.speed == Catch::Approx(1.5f));
}

TEST_CASE("Recovery_DoesNotWaitForRawClipEnd_WhenRawClipHasLongTail", "[combo_fsm]") {
    // Synthetic attack on a clip with a multi-second dead tail.
    AttackTable attacks;
    attacks["longtail"] =
        AttackDef{"longtail", "Long", 0.30f, 0.50f, 1.0f, 0.4f, 0.16f, 0.55f, 0.45f};
    std::vector<AnimClip> clips;
    {
        AnimClip c; c.name = "Long"; c.duration_seconds = 5.0f; clips.push_back(c);
    }
    ChainTable chains{{kLightChain, {"longtail"}}};
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt); // Startup
    anim.time_seconds = 1.6f;   // nt = 1.6/5.0 = 0.32 >= hit_start 0.30 -> Active
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Active);
    anim.time_seconds = 2.6f;   // nt = 0.52 > hit_end 0.50 -> Recovery
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);

    // recovery_seconds = 0.16 -> ~10 frames. anim.time stays at 2.6 (far from 5.0).
    for (int i = 0; i < 11; ++i) {
        combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    }
    REQUIRE(combat.phase == CombatPhase::Idle);   // returned WITHOUT reaching clip end
}

TEST_CASE("start_attack rejects missing clip (no silent fallback)", "[combo_fsm]") {
    AttackTable attacks;
    attacks["ghost"] =
        AttackDef{"ghost", "DoesNotExist", 0.30f, 0.50f, 1.0f, 0.4f, 0.2f, 0.60f, 0.50f};
    std::vector<AnimClip> clips;   // empty -> clip duration resolves to 0
    ChainTable chains{{kLightChain, {"ghost"}}};
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, 1.f / 60.f);

    REQUIRE(combat.phase == CombatPhase::Idle);
    REQUIRE(combat.combo_ids.empty());
}

TEST_CASE("chain-cancel only fires at cancel_start_norm", "[combo_fsm]") {
    auto attacks = make_table();   // punch.cancel_start_norm = 0.70, clip 0.5s
    auto clips = make_clips();
    auto chains = make_chains();
    auto tf = make_transform();
    CombatController combat;
    AnimationState anim;
    InputBuffer buffer;
    const float dt = 1.f / 60.f;

    buffer.push(Kind::Light);
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;  // Active
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    anim.time_seconds = 0.26f;  // Recovery (nt 0.52 > 0.50)
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);

    // Buffer next BEFORE cancel window (nt 0.66 < 0.70): must NOT chain yet.
    buffer.push(Kind::Light);
    anim.time_seconds = 0.33f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);
    REQUIRE(combat.combo_index == 0);

    // Reach cancel window (nt 0.72 >= 0.70): chains to index 1.
    anim.time_seconds = 0.36f;
    combat_tick(combat, tf, anim, buffer, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(combat.combo_index == 1);
}
```

Add `#include "engine/character/core/CharacterAsset.hpp"` is already present (for `AnimClip`). Keep existing includes.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --config Debug --target character_tests`
Expected: build succeeds but new/updated `[combo_fsm]` tests FAIL (old `combat_tick` resets at clip end, does not seed trimmed time/speed, does not fail-fast, has no recovery timer).

- [ ] **Step 3: Patch `CombatController.cpp` (do NOT blind-replace)**

First, **diff the current file against the target below** and write down any behavior
present in the current file but absent from the target (e.g. extra logging, fields,
or branches added since this plan). Preserve anything the diff reveals that is not an
intentional change of this task. The intentional changes are exactly:
1. `clip_duration` returns `0.f` (not `1.f`) for a missing clip.
2. `start_attack` takes `const std::vector<AnimClip>& clips`, validates the clip
   (rejects missing / `duration <= 0`), seeds `anim.time_seconds = clip_start_s` and
   `anim.speed = def.time_scale`, resets `recovery_timer`, and uses a `0.06f` crossfade.
3. `reset_to_idle` crossfades to `"Idle"` (not `"Walk"`) over `0.10f` and resets
   `recovery_timer`.
4. `combat_tick` computes `def`/`duration` before the hitstop block; on hitstop end
   restores `anim.speed` to `def->time_scale` (not hardcoded `1.f`).
5. `nt` uses `attack_norm_time`; playback is clamped to `clip_end_s`.
6. Idle clears `combo_ids` when `start_attack` rejects (bad data).
7. Active resets `recovery_timer` on entering Recovery.
8. Recovery is timer-driven (`recovery_timer >= recovery_seconds`) instead of
   clip-end-driven, with Option B cancel gating.

If the current file matches the pre-plan version exactly, the target below IS the
full file. Apply it as:

```cpp
#include "engine/character/core/CombatController.hpp"

#include "engine/character/core/AnimationController.hpp"
#include "engine/movement/core/Components.hpp"

#include <spdlog/spdlog.h>

namespace engine::character {

namespace {

// Returns 0 when the clip is not found, so callers can treat 0 as invalid.
float clip_duration(const std::string& name, const std::vector<AnimClip>& clips) {
    for (const AnimClip& clip : clips) {
        if (clip.name == name) {
            return clip.duration_seconds;
        }
    }
    return 0.f;
}

const AttackDef* current_def(const CombatController& combat, const AttackTable& attacks) {
    if (combat.combo_index < 0 ||
        combat.combo_index >= static_cast<int>(combat.combo_ids.size())) {
        return nullptr;
    }
    const std::string& id = combat.combo_ids[static_cast<std::size_t>(combat.combo_index)];
    auto it = attacks.find(id);
    return it != attacks.end() ? &it->second : nullptr;
}

std::string kind_to_chain(BufferedInput::Kind kind) {
    switch (kind) {
    case BufferedInput::Kind::Light:   return kLightChain;
    case BufferedInput::Kind::Heavy:   return kHeavyChain;
    case BufferedInput::Kind::Kick:    return kKickChain;
    case BufferedInput::Kind::Special: return kSpecialChain;
    default:                           return {};
    }
}

// Validates the clip, then commits the attack state. Returns false (FSM untouched)
// when the clip is missing or zero-length — no silent fallback to untrimmed play.
bool start_attack(CombatController& combat,
                  engine::movement::Transform& transform,
                  AnimationState& anim,
                  const AttackTable& attacks,
                  const std::vector<AnimClip>& clips,
                  int combo_index,
                  BufferedInput::Kind kind) {
    if (combo_index < 0 ||
        combo_index >= static_cast<int>(combat.combo_ids.size())) {
        return false;
    }
    const std::string& id = combat.combo_ids[static_cast<std::size_t>(combo_index)];
    auto it = attacks.find(id);
    if (it == attacks.end()) {
        return false;
    }
    const AttackDef& def = it->second;
    const float duration = clip_duration(def.clip, clips);
    if (duration <= 0.f) {
        SPDLOG_ERROR("CombatController: attack '{}' clip '{}' missing or zero-length; "
                     "not starting", id, def.clip);
        return false;
    }

    combat.combo_index    = combo_index;
    combat.hit_consumed   = false;
    combat.attack_yaw     = transform.yaw;
    combat.active_kind    = kind;
    combat.phase          = CombatPhase::Startup;
    combat.recovery_timer = 0.f;

    AnimationController::crossfade_to(anim, def.clip, 0.06f, false);
    anim.time_seconds = attack_clip_start_seconds(def, duration);
    anim.speed        = def.time_scale;
    return true;
}

void reset_to_idle(CombatController& combat, AnimationState& anim) {
    AnimationController::crossfade_to(anim, "Idle", 0.10f, true);
    combat.phase          = CombatPhase::Idle;
    combat.combo_index    = 0;
    combat.active_kind    = BufferedInput::Kind::None;
    combat.combo_ids.clear();
    combat.hit_consumed   = false;
    combat.recovery_timer = 0.f;
}

} // namespace

void combat_tick(CombatController& combat,
                 engine::movement::Transform& transform,
                 AnimationState& anim,
                 InputBuffer& buffer,
                 const AttackTable& attacks,
                 const std::vector<AnimClip>& clips,
                 const ChainTable& chains,
                 float dt) {
    const AttackDef* def = current_def(combat, attacks);
    const float duration = def ? clip_duration(def->clip, clips) : 0.f;

    // Hitstop overlay: freeze FSM, drain counter, then restore the attack's speed.
    if (combat.hitstop_active) {
        --combat.hitstop_frames;
        if (combat.hitstop_frames <= 0) {
            combat.hitstop_active = false;
            combat.phase = combat.phase_before_hitstop;
            anim.speed = def ? def->time_scale : 1.f;
        }
        return;
    }

    const float nt = (def && duration > 0.f)
        ? attack_norm_time(anim.time_seconds, *def, duration)
        : 0.f;

    // Hold playback at the trimmed end so the dead clip outro never shows.
    if (def && duration > 0.f) {
        const float clip_end_s = attack_clip_end_seconds(*def, duration);
        if (anim.time_seconds > clip_end_s) {
            anim.time_seconds = clip_end_s;
        }
    }

    switch (combat.phase) {
    case CombatPhase::Idle: {
        const BufferedInput::Kind kind = buffer.peek();
        if (kind == BufferedInput::Kind::None || kind == BufferedInput::Kind::Dodge) {
            break;
        }
        const std::string chain = kind_to_chain(kind);
        auto it = chains.find(chain);
        if (it == chains.end() || it->second.empty()) {
            break;
        }
        buffer.consume();
        combat.combo_ids = it->second;
        if (!start_attack(combat, transform, anim, attacks, clips, 0, kind)) {
            combat.combo_ids.clear(); // bad data -> stay Idle
        }
        break;
    }

    case CombatPhase::Startup:
        if (def && nt >= def->hit_start_norm) {
            combat.phase = CombatPhase::Active;
        }
        break;

    case CombatPhase::Active:
        if (def && nt > def->hit_end_norm) {
            combat.phase = CombatPhase::Recovery;
            combat.recovery_timer = 0.f;
        }
        break;

    case CombatPhase::Recovery: {
        combat.recovery_timer += dt;

        const int next_index = combat.combo_index + 1;
        const bool has_next = next_index < static_cast<int>(combat.combo_ids.size());

        // Dodge-cancel once normalized trimmed time reaches dodge_cancel_start_norm.
        if (def && nt >= def->dodge_cancel_start_norm &&
            buffer.peek() == BufferedInput::Kind::Dodge) {
            buffer.consume();
            combat.dodge_requested = true;
            reset_to_idle(combat, anim);
            combat.phase = CombatPhase::DodgeCancel;
            break;
        }

        // Chain-cancel once normalized trimmed time reaches cancel_start_norm.
        if (has_next && def && nt >= def->cancel_start_norm &&
            buffer.peek() == combat.active_kind) {
            buffer.consume();
            (void)start_attack(combat, transform, anim, attacks, clips, next_index,
                               combat.active_kind);
            break;
        }

        // Fixed recovery: return to Idle after recovery_seconds, independent of the
        // clip's (possibly multi-second) dead tail.
        if (def && combat.recovery_timer >= def->recovery_seconds) {
            reset_to_idle(combat, anim);
        }
        break;
    }

    case CombatPhase::DodgeCancel:
        combat.dodge_requested = false;
        combat.phase = CombatPhase::Idle;
        break;
    }
}

} // namespace engine::character
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R combo_fsm --output-on-failure`
Expected: PASS — all `[combo_fsm]` cases including `Recovery_DoesNotWaitForRawClipEnd_WhenRawClipHasLongTail`, seed, fail-fast, and cancel-gating. The pre-existing `hitstop freezes FSM` test still passes (default `time_scale=1` restores `speed==1`).

- [ ] **Step 5: Commit**

```bash
git add engine/character/core/CombatController.cpp tests/character/test_combo_fsm.cpp
git commit -m "feat: trimmed playback, timer-driven recovery, fail-fast in combat_tick"
```

---

## Task 4: HitDetection uses trimmed normalization

**Files:**
- Modify: `engine/character/core/HitDetection.cpp:65-67`
- Test: `tests/character/test_hit_window.cpp`

- [ ] **Step 1: Write the failing regression test**

Append to `tests/character/test_hit_window.cpp` (reuses the file's existing
`make_transform`/`box_col` helpers):

```cpp
TEST_CASE("HitDetection_UsesTrimmedNormTime_ForHitWindow", "[hit_window]") {
    // 2.0s clip, trimmed region [1.0s, 2.0s], hit window [0.4, 0.6] of that region.
    AttackDef def{"trim_hit", "Clip", 0.4f, 0.6f, 1.25f, 0.35f, 0.2f, 0.70f, 0.60f};
    def.clip_start_norm = 0.5f;
    def.clip_end_norm   = 1.0f;

    auto attacker = make_transform({0.f, 1.f, 0.f}, 0.f);
    auto target   = make_transform({0.f, 1.f, 1.25f});
    auto col      = box_col();

    // Raw time 1.5s -> trimmed nt 0.5 (inside [0.4,0.6]) -> HIT.
    // Old raw normalization gives 1.5/2.0 = 0.75 (outside) -> would MISS.
    CombatController inside;
    inside.phase = CombatPhase::Active;
    AnimationState a_in; a_in.active_clip = "Clip"; a_in.time_seconds = 1.5f;
    REQUIRE(try_hit_in_window(inside, a_in, def, 2.0f, attacker, target, col));

    // Raw time 0.9s -> trimmed nt clamps to 0 (outside [0.4,0.6]) -> MISS.
    // Old raw normalization gives 0.9/2.0 = 0.45 (inside) -> would wrongly HIT.
    CombatController outside;
    outside.phase = CombatPhase::Active;
    AnimationState a_out; a_out.active_clip = "Clip"; a_out.time_seconds = 0.9f;
    REQUIRE_FALSE(try_hit_in_window(outside, a_out, def, 2.0f, attacker, target, col));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R HitDetection_UsesTrimmedNormTime --output-on-failure`
Expected: FAIL — current code normalizes against raw clip duration, so the 1.5s case
misses and the 0.9s case hits (both assertions wrong).

- [ ] **Step 3: Replace the normalization**

In `try_hit_in_window`, replace:

```cpp
    const float normalized = clip_duration_seconds > 1e-5f
        ? std::clamp(anim.time_seconds / clip_duration_seconds, 0.f, 1.f)
        : 1.f;
```

with:

```cpp
    const float normalized = clip_duration_seconds > 1e-5f
        ? attack_norm_time(anim.time_seconds, def, clip_duration_seconds)
        : 1.f;
```

(`attack_norm_time` is declared in `AttackData.hpp`, already included via `HitDetection.hpp`. `std::clamp`'s `<algorithm>` include stays — still used elsewhere in the file.)

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R hit_window --output-on-failure`
Expected: PASS — the new regression test and all pre-existing `[hit_window]` cases
(unchanged for default-trim attacks, since `start=0,end=1` makes
`attack_norm_time == time/duration`).

- [ ] **Step 5: Commit**

```bash
git add engine/character/core/HitDetection.cpp tests/character/test_hit_window.cpp
git commit -m "fix: resolve hit window against trimmed attack region"
```

---

## Task 5: Per-attack hitstop frames

**Files:**
- Modify: `engine/character/core/HitReactSystem.hpp:23-28`
- Modify: `engine/character/core/HitReactSystem.cpp:38-59`

- [ ] **Step 1: Add the parameter to the declaration**

In `HitReactSystem.hpp`, change the `trigger_hit_react` declaration to:

```cpp
void trigger_hit_react(HitReact& react,
                       engine::movement::Transform& transform,
                       const glm::vec3& direction,
                       AnimationState& anim,
                       CombatController* attacker_combat = nullptr,
                       ScreenShake* shake = nullptr,
                       int attacker_hitstop_frames = 5);
```

- [ ] **Step 2: Use it in the definition**

In `HitReactSystem.cpp`, change the signature to match and replace the hardcoded `5`:

```cpp
void trigger_hit_react(HitReact& react,
                       engine::movement::Transform& /*transform*/,
                       const glm::vec3& direction,
                       AnimationState& anim,
                       CombatController* attacker_combat,
                       ScreenShake* shake,
                       int attacker_hitstop_frames) {
    react.playing_hit       = true;
    react.timer             = react.knockback_duration;
    react.knockback_delta   = glm::vec3(direction.x, 0.f, direction.z) *
                              react.knockback_distance;

    anim.active_clip  = react.hit_clip;
    anim.time_seconds = 0.f;
    anim.looping      = false;
    anim.speed        = 1.f;

    if (attacker_combat != nullptr) {
        attacker_combat->hitstop_active = true;
        attacker_combat->phase_before_hitstop = attacker_combat->phase;
        attacker_combat->hitstop_frames = attacker_hitstop_frames;
    }

    if (shake != nullptr) {
        apply_screenshake(*shake, 0.04f, 0.12f);
    }
}
```

- [ ] **Step 3: Build to verify it compiles (default keeps old behaviour)**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug --output-on-failure`
Expected: PASS (existing callers use the default `5`).

- [ ] **Step 4: Commit**

```bash
git add engine/character/core/HitReactSystem.hpp engine/character/core/HitReactSystem.cpp
git commit -m "feat: per-attack hitstop frame count in trigger_hit_react"
```

---

## Task 6: Load combat clips from assets/Fight

**Files:**
- Modify: `engine/character/core/CharacterCatalog.cpp`
- Test: `tests/character/test_character_catalog.cpp`

- [ ] **Step 1: Update the catalog test for the new clip set**

In `tests/character/test_character_catalog.cpp`, in the first test case, replace the
clip-count assertion and `expected_clips` list with the following. This both asserts
the new clips are present AND that the old over-long player-dir combat clips are gone
(the spec requires they no longer load):

```cpp
    const auto has_clip = [&](const std::string& name) {
        for (const auto& clip : asset.clips) {
            if (clip.name == name) return true;
        }
        return false;
    };

    // Walk, Run, the eleven Fight combat clips, plus the Idle rest pose.
    REQUIRE(asset.clips.size() >= 14);

    const std::vector<std::string> expected_clips = {
        "Walk", "Run", "Idle",
        "Jab_Left", "Jab_Right", "Hook_Left", "Uppercut_Right",
        "Uppercut_Left", "Upper_Hook_Right", "Knee_Strike",
        "Kick_High_Step", "Roundhouse", "Spin_Kick", "Shield_Push",
    };
    for (const std::string& expected : expected_clips) {
        REQUIRE(has_clip(expected));
    }

    // The replaced over-long clips must NOT be loaded anymore.
    REQUIRE_FALSE(has_clip("Counterstrike"));
    REQUIRE_FALSE(has_clip("Dodge_and_Counter"));
    REQUIRE_FALSE(has_clip("Elbow_Strike"));
```

Delete the now-redundant original `for (const std::string& expected : expected_clips)`
loop further down (the one with the inner `found` flag) — the `has_clip` loop above
replaces it.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R catalog --output-on-failure`
Expected: FAIL — the new clip names are not loaded yet.

- [ ] **Step 3: Rewrite `load_player_set` in `CharacterCatalog.cpp`**

Replace the body of `CharacterCatalog::load_player_set()` with:

```cpp
CharacterAsset CharacterCatalog::load_player_set() {
    const std::string base_path = player_glb("Character_output.glb");
    require_exists(base_path);

    CharacterAsset asset = CookedCharacterCache::load_or_cook(base_path, [&] {
        return GltfIngest::load_base(base_path);
    });

    auto append_clip = [&](const std::string& anim_path, const std::string& clip_name) {
        require_exists(anim_path);
        const std::string cache_key = base_path + "|" + anim_path + "|" + clip_name;
        CharacterAsset tmp = CookedCharacterCache::load_or_cook(cache_key, [&] {
            CharacterAsset t = GltfIngest::load_base(base_path);
            GltfIngest::load_animation_clip(t, anim_path, clip_name);
            return t;
        });
        if (!tmp.clips.empty()) {
            asset.clips.push_back(std::move(tmp.clips.back()));
        }
    };

    // Locomotion from the player directory.
    append_clip(player_glb("Animation_Walking_withSkin.glb"), "Walk");
    append_clip(player_glb("Animation_Running_withSkin.glb"), "Run");

    // Combat clips from assets/Fight (verified skeleton-compatible with the rig).
    const std::string fight_dir = std::string(ENGINE_SOURCE_DIR) + "/assets/Fight/";
    const std::pair<std::string, std::string> combat[] = {
        {"Meshy_AI_Animation_Left_Jab_from_Guard_withSkin.glb",          "Jab_Left"},
        {"Meshy_AI_Animation_Right_Jab_from_Guard_withSkin.glb",         "Jab_Right"},
        {"Meshy_AI_Animation_Left_Hook_from_Guard_withSkin.glb",         "Hook_Left"},
        {"Meshy_AI_Animation_Right_Uppercut_from_Guard_withSkin.glb",    "Uppercut_Right"},
        {"Meshy_AI_Animation_Left_Uppercut_from_Guard_withSkin.glb",     "Uppercut_Left"},
        {"Meshy_AI_Animation_Right_Upper_Hook_from_Guard_withSkin.glb",  "Upper_Hook_Right"},
        {"Meshy_AI_Animation_Boxing_Guard_Step_Knee_Strike_withSkin.glb","Knee_Strike"},
        {"Meshy_AI_Animation_Step_in_High_Kick_withSkin.glb",            "Kick_High_Step"},
        {"Meshy_AI_Animation_Roundhouse_Kick_withSkin.glb",              "Roundhouse"},
        {"Meshy_AI_Animation_Lunge_Spin_Kick_withSkin.glb",              "Spin_Kick"},
        {"Meshy_AI_Animation_Shield_Push_Left_withSkin.glb",             "Shield_Push"},
        {"Meshy_AI_Animation_Idle_7_withSkin.glb",                       "Idle"},
    };
    for (const auto& [file, clip_name] : combat) {
        append_clip(fight_dir + file, clip_name);
    }

    SPDLOG_INFO("CharacterCatalog: player set loaded — {} clips", asset.clips.size());
    return asset;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R catalog --output-on-failure`
Expected: PASS (13 clips present; cache directory written).

- [ ] **Step 5: Commit**

```bash
git add engine/character/core/CharacterCatalog.cpp tests/character/test_character_catalog.cpp
git commit -m "feat: load combat clips from assets/Fight in player set"
```

---

## Task 6b: Idle locomotion state

Make `select_locomotion` return `"Idle"` when the player is standing still, so that
after `reset_to_idle` (which crossfades to `"Idle"`) a *standing* player stays in the
idle pose, while a *moving* player is immediately corrected to `"Walk"`/`"Run"` by the
locomotion selector that already runs every tick while `phase == Idle`
(`movement/MovementApp.cpp`). This is how "return to Idle unless moving" is achieved
without coupling `CombatController` to movement velocity.

**Files:**
- Modify: `engine/character/core/AnimationController.cpp:137-145`
- Test: `tests/character/test_animation_controller.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/character/test_animation_controller.cpp`:

```cpp
TEST_CASE("select_locomotion returns Idle when standing", "[anim_controller]") {
    using engine::character::AnimationController;
    REQUIRE(AnimationController::select_locomotion(0.0f, true)  == "Idle");
    REQUIRE(AnimationController::select_locomotion(0.05f, true) == "Idle");
    REQUIRE(AnimationController::select_locomotion(1.0f, true)  == "Walk");
    REQUIRE(AnimationController::select_locomotion(4.0f, true)  == "Run");
    // Airborne falls back to Walk (no idle in the air).
    REQUIRE(AnimationController::select_locomotion(0.0f, false) == "Walk");
}
```

(If `test_animation_controller.cpp` uses a different Catch2 tag convention, match the
existing tag used by other cases in that file instead of `[anim_controller]`.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R "select_locomotion returns Idle" --output-on-failure`
Expected: FAIL — current code returns `"Walk"` for `speed < 0.1`.

- [ ] **Step 3: Update `select_locomotion`**

Replace the body in `AnimationController.cpp`:

```cpp
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R "select_locomotion" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/character/core/AnimationController.cpp tests/character/test_animation_controller.cpp
git commit -m "feat: idle locomotion state when standing still"
```

---

## Task 7: New attack roster in combat_attacks.txt

**Files:**
- Modify: `assets/character/combat_attacks.txt`
- Test: `tests/character/test_attack_data.cpp`

- [ ] **Step 1: Update the roster assertion test**

In `tests/character/test_attack_data.cpp`, replace the first test case
`"parses combat attacks with hit and cancel windows"` with:

```cpp
TEST_CASE("parses the tuned combat roster", "[attack_data]") {
    const engine::character::AttackTable table =
        engine::character::AttackData::load(kAttackFile);

    REQUIRE(table.size() == 11);
    REQUIRE(table.count("jab_left"));
    REQUIRE(table.count("uppercut_right"));
    REQUIRE(table.count("knee_strike"));

    const auto& jab = table.at("jab_left");
    REQUIRE(jab.clip == "Jab_Left");
    REQUIRE(jab.clip_start_norm == Catch::Approx(0.15f));
    REQUIRE(jab.clip_end_norm   == Catch::Approx(0.65f));
    REQUIRE(jab.time_scale      == Catch::Approx(1.4f));
    REQUIRE(jab.hit_start_norm  == Catch::Approx(0.35f));
    REQUIRE(jab.hit_end_norm    == Catch::Approx(0.55f));
    REQUIRE(jab.hitstop_frames  == 3);

    const auto& fin = table.at("uppercut_right");
    REQUIRE(fin.hitstop_frames == 6);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R attack_data --output-on-failure`
Expected: FAIL — old file has 8 attacks with old ids.

- [ ] **Step 3: Replace `assets/character/combat_attacks.txt`**

```
# combat_attacks.txt - v3 tuned roster (Combat Feel Tuning)
# Fields: clip, clip_window [start end] (raw-clip trim, normalized),
#         time_scale, hit_window [start end], range, radius, recovery (s),
#         cancel_window, dodge_cancel_window, hitstop (frames).
# hit/cancel windows are normalized within the TRIMMED region [clip_start, clip_end].

# --- Light chain: jab_left -> jab_right -> hook_left -> uppercut_right ---
attack jab_left {
    clip Jab_Left
    clip_window 0.15 0.65
    time_scale 1.4
    hit_window 0.35 0.55
    range 1.00  radius 0.30
    recovery 0.16
    cancel_window 0.55  dodge_cancel_window 0.45
    hitstop 3
}
attack jab_right {
    clip Jab_Right
    clip_window 0.15 0.62
    time_scale 1.4
    hit_window 0.34 0.54
    range 1.05  radius 0.30
    recovery 0.16
    cancel_window 0.54  dodge_cancel_window 0.45
    hitstop 3
}
attack hook_left {
    clip Hook_Left
    clip_window 0.10 0.78
    time_scale 1.3
    hit_window 0.38 0.56
    range 0.95  radius 0.32
    recovery 0.18
    cancel_window 0.56  dodge_cancel_window 0.46
    hitstop 4
}
attack uppercut_right {
    clip Uppercut_Right
    clip_window 0.10 0.82
    time_scale 1.2
    hit_window 0.40 0.58
    range 0.90  radius 0.34
    recovery 0.22
    cancel_window 0.58  dodge_cancel_window 0.48
    hitstop 6
}

# --- Heavy chain: uppercut_left -> upper_hook_right -> knee_strike ---
attack uppercut_left {
    clip Uppercut_Left
    clip_window 0.12 0.70
    time_scale 1.3
    hit_window 0.38 0.56
    range 0.95  radius 0.34
    recovery 0.20
    cancel_window 0.56  dodge_cancel_window 0.46
    hitstop 4
}
attack upper_hook_right {
    clip Upper_Hook_Right
    clip_window 0.15 0.66
    time_scale 1.35
    hit_window 0.36 0.55
    range 1.00  radius 0.34
    recovery 0.20
    cancel_window 0.55  dodge_cancel_window 0.46
    hitstop 5
}
attack knee_strike {
    clip Knee_Strike
    clip_window 0.15 0.62
    time_scale 1.4
    hit_window 0.40 0.58
    range 1.05  radius 0.38
    recovery 0.26
    cancel_window 0.58  dodge_cancel_window 0.48
    hitstop 6
}

# --- Kick chain: high_kick_step -> roundhouse -> spin_kick ---
attack high_kick_step {
    clip Kick_High_Step
    clip_window 0.10 0.72
    time_scale 1.25
    hit_window 0.40 0.58
    range 1.20  radius 0.36
    recovery 0.18
    cancel_window 0.58  dodge_cancel_window 0.48
    hitstop 4
}
attack roundhouse {
    clip Roundhouse
    clip_window 0.20 0.64
    time_scale 1.5
    hit_window 0.38 0.60
    range 1.30  radius 0.40
    recovery 0.22
    cancel_window 0.60  dodge_cancel_window 0.50
    hitstop 5
}
attack spin_kick {
    clip Spin_Kick
    clip_window 0.12 0.74
    time_scale 1.3
    hit_window 0.40 0.62
    range 1.45  radius 0.40
    recovery 0.26
    cancel_window 0.62  dodge_cancel_window 0.52
    hitstop 6
}

# --- Special: shield_push ---
attack shield_push {
    clip Shield_Push
    clip_window 0.12 0.64
    time_scale 1.4
    hit_window 0.30 0.50
    range 0.95  radius 0.36
    recovery 0.20
    cancel_window 0.50  dodge_cancel_window 0.42
    hitstop 4
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --config Debug --target character_tests && ctest --test-dir build -C Debug -R attack_data --output-on-failure`
Expected: PASS (11 attacks, new ids/values).

- [ ] **Step 5: Commit**

```bash
git add assets/character/combat_attacks.txt tests/character/test_attack_data.cpp
git commit -m "feat: tuned combat roster with trimmed boxing clips"
```

---

## Task 8: Wire chains + per-attack hitstop in MovementApp

**Files:**
- Modify: `movement/MovementApp.cpp:98-105` (chains)
- Modify: `movement/MovementApp.cpp:505-515` (hit resolution call)

- [ ] **Step 1: Update the chain definitions**

Replace the `player_chains_` block (lines ~98-105) with:

```cpp
        player_chains_[engine::character::kLightChain] = {
            "jab_left", "jab_right", "hook_left", "uppercut_right"};
        player_chains_[engine::character::kHeavyChain] = {
            "uppercut_left", "upper_hook_right", "knee_strike"};
        player_chains_[engine::character::kKickChain] = {
            "high_kick_step", "roundhouse", "spin_kick"};
        player_chains_[engine::character::kSpecialChain] = {
            "shield_push"};
```

- [ ] **Step 1b: Default rest pose to Idle**

At `movement/MovementApp.cpp:95`, change the initial player clip:

```cpp
        player_anim_.active_clip = "Idle";
```

(`looping = true` on the next line stays. The `Idle` clip is loaded by Task 6; the
locomotion selector switches to `Walk`/`Run` as soon as the player moves.)

- [ ] **Step 2: Pass the attack's hitstop frames on a confirmed hit**

In the hit-resolution block, the `trigger_hit_react` call currently reads (around
line 512):

```cpp
                                engine::character::trigger_hit_react(
                                    dummy_react_, *dummy_tf, dir_norm, dummy_anim_,
                                    &player_combat_, &camera_shake_);
```

`ait` is the `AttackTable::iterator` for the active attack used a few lines above in
the `try_hit_in_window(... ait->second ...)` call. Pass its hitstop frame count:

```cpp
                                engine::character::trigger_hit_react(
                                    dummy_react_, *dummy_tf, dir_norm, dummy_anim_,
                                    &player_combat_, &camera_shake_,
                                    ait->second.hitstop_frames);
```

(If the local iterator/def is named differently at this site, use whatever variable
already holds the active `AttackDef` in the `try_hit_in_window` call directly above —
do not introduce a second lookup.)

- [ ] **Step 3: Build the movement app + full test suite**

Run: `cmake --build build --config Debug --target MovementApp character_tests && ctest --test-dir build -C Debug --output-on-failure`
Expected: build succeeds; all tests PASS.

- [ ] **Step 4: Commit**

```bash
git add movement/MovementApp.cpp
git commit -m "feat: wire tuned chains and per-attack hitstop in MovementApp"
```

---

## Task 9: Print effective durations in anim_inspect

**Files:**
- Modify: `tools/anim_inspect/main.cpp`

- [ ] **Step 1: Add an effective-duration column**

In the attack frame-data loop, after `const float dur = clip->duration_seconds;`,
add:

```cpp
        const float trim_start = def.clip_start_norm * dur;
        const float trim_end   = def.clip_end_norm   * dur;
        const float effective  = def.time_scale > 1e-5f
            ? (trim_end - trim_start) / def.time_scale
            : (trim_end - trim_start);
```

and extend the per-attack `printf` to also print `effective` (seconds + frames),
e.g. append ` eff=%.2fs(%2df)` with `effective, frames(effective)` as the final
arguments. Update the header `printf` to include an `eff` column label.

- [ ] **Step 2: Build + run**

Run: `cmake --build build --config Debug --target anim_inspect && ./build/tools/Debug/anim_inspect.exe`
Expected: each attack prints a trimmed+scaled effective duration (openers ~0.55–0.70 s, finishers ~0.75–0.85 s). Confirms the tuning targets at a glance.

- [ ] **Step 3: Commit**

```bash
git add tools/anim_inspect/main.cpp
git commit -m "feat: report effective trimmed+scaled durations in anim_inspect"
```

---

## Task 10: Full verification + playtest checklist

**Files:** none (verification only).

- [ ] **Step 1: Clean configure + full build**

Run: `cmake -S . -B build && cmake --build build --config Debug`
Expected: builds with no errors.

- [ ] **Step 2: Full test suite**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: all tests PASS (baseline was 195; new tests added).

- [ ] **Step 3: anim_inspect sanity**

Run: `./build/tools/Debug/anim_inspect.exe`
Confirm effective durations are in the target ranges; adjust `time_scale` /
`clip_window` in `combat_attacks.txt` if any opener exceeds ~0.8 s.

- [ ] **Step 4: Manual playtest of the MovementApp**

Run the MovementApp build. Verify by feel:
- Light chain jab→jab→hook→uppercut flows without waiting on clip ends.
- Buffering the next input ~6–10 frames early registers the chain.
- Recovery is short; mashing a different chain or dodge cancels cleanly.
- Hits land with visible hitstop; finishers feel weightier than jabs.
- No T-pose / frozen dead-outro frames between attacks.
- After a combo ends while standing, the character settles into the looped `Idle`
  pose (not the `Walk` pose); while moving, it blends to `Walk`/`Run`.

Record any clips that still feel off (too slow, hit timing wrong) and tune their
`combat_attacks.txt` values; re-run `anim_inspect` to confirm. No code changes
should be needed for feel adjustments.

- [ ] **Step 5: Final commit (only if tuning values changed)**

```bash
git add assets/character/combat_attacks.txt
git commit -m "chore: post-playtest combat frame-data tuning"
```

---

## Self-Review

**Spec coverage:**
- Trim + `time_scale` + `clip_window` → Task 1 (data), Task 3 (playback).
- `recovery_seconds` wired → Task 3 (recovery_timer).
- Reject attack start on missing/zero-length clip (log + stay Idle, no silent
  untrimmed fallback) → Task 3. Note: this rejects at *attack-start* time, not at
  data-load time; a load-time hard fail would require cross-referencing
  `combat_attacks.txt` clip names against the loaded catalog, which is out of scope.
- Cancel model Option B (gated by `cancel_start_norm`/`dodge_cancel_start_norm`) → Task 3.
- Playback clamp to trimmed end → Task 3.
- Trimmed hit-window normalization → Task 4 (spec gap surfaced: HitDetection needed
  updating; covered) with its own discriminating regression test
  `HitDetection_UsesTrimmedNormTime_ForHitWindow`.
- Catalog removes old over-long clips → Task 6 asserts `REQUIRE_FALSE(has_clip(...))`
  for Counterstrike / Dodge_and_Counter / Elbow_Strike, not just presence of new ones.
- Test hygiene: new parser tests write to `std::filesystem::temp_directory_path()`,
  never the source tree → Task 1.
- Per-attack hitstop, single owner = MovementApp → Task 5 (param) + Task 8 (caller).
- Crossfade tightening (0.06 / 0.10) → Task 3 (`start_attack` / `reset_to_idle`).
- New roster + chains, finishers replaced → Task 6 (clips), Task 7 (data), Task 8 (chains).
- Anti-regression `Recovery_DoesNotWaitForRawClipEnd_WhenRawClipHasLongTail` → Task 3.
- `anim_inspect` effective durations → Task 9.
- Idle rest pose: clip loaded (Task 6), `select_locomotion` returns `"Idle"` standing
  (Task 6b), `reset_to_idle` → `"Idle"` (Task 3), default pose `"Idle"` (Task 8). The
  locomotion selector corrects to `Walk`/`Run` when moving, so combat never ends stuck
  in the Walk pose while standing.

**Placeholder scan:** No TBD/TODO; every code step shows full code.

**Type consistency:** Field names (`clip_start_norm`, `clip_end_norm`, `time_scale`,
`hitstop_frames`, `recovery_timer`) and helper names (`attack_norm_time`,
`attack_clip_start_seconds`, `attack_clip_end_seconds`) are used identically across
Tasks 1, 3, 4, 7, 9. `trigger_hit_react`'s new `attacker_hitstop_frames` parameter
(Task 5) matches the call site (Task 8). Logical clip names in `combat_attacks.txt`
(Task 7) match those loaded by `CharacterCatalog` (Task 6) and asserted by the
catalog test.
```
