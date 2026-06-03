# Combat System Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redesign the combat system with input-gated combos, 10-frame input buffering, TRS-level animation crossfades, hitstop overlay, screenshake, and dodge-cancel windows — eliminating the double-advance bug and enabling 4 independent attack chains.

**Architecture:** A new `InputBuffer` (ring buffer, 10-frame TTL) replaces the 1-frame `attack_pressed` latch. `CombatPhase` gains `Startup/Active/Recovery/DodgeCancel` states. Hitstop is an overlay (`hitstop_active` flag) not a new phase. `AnimationController` blends at TRS level. `AnimationController::tick` is the sole owner of `anim.time_seconds`. Data flow per sim step: `InputBuffer::tick → AnimationController::tick → combat_tick → player_tick → try_hit_in_window → hit_react_tick`.

**Tech Stack:** C++20, GLM (glm::slerp for rotation blend), Catch2 (tests), GLFW (input), Visual Studio / CMake build

---

## File Map

| File | Status | Responsibility |
|---|---|---|
| `engine/movement/core/InputSnapshot.hpp` | Modify | Replace `attack_pressed` with 4 attack bools + `dodge_pressed` |
| `engine/character/core/InputBuffer.hpp` | **Create** | `BufferedInput::Kind` enum + `InputBuffer` ring-buffer class |
| `engine/character/core/InputBuffer.cpp` | **Create** | `InputBuffer` implementation |
| `engine/character/core/CharacterComponents.hpp` | Modify | New `CombatPhase`; blend fields on `AnimationState`; hitstop/chain fields on `CombatController` |
| `engine/character/core/AttackData.hpp` | Modify | Rename fields with `_norm`/`_seconds` suffixes; add `cancel_start_norm`, `dodge_cancel_start_norm` |
| `engine/character/core/AttackData.cpp` | Modify | Parse new fields; assert invariants |
| `engine/character/core/HitDetection.cpp` | Modify | Update field references; simplify `norm_t` calc |
| `engine/character/core/AnimationController.hpp` | Modify | Declare `crossfade_to`; updated `sample_bone_matrices` signature |
| `engine/character/core/AnimationController.cpp` | Modify | `crossfade_to`; blend decay in `tick`; TRS blend in `sample_bone_matrices` |
| `engine/character/core/CombatController.hpp` | Modify | New `combat_tick` signature; `ChainTable` type alias |
| `engine/character/core/CombatController.cpp` | Modify | Full FSM rewrite |
| `engine/character/core/HitReactSystem.hpp` | Modify | `trigger_hit_react` takes `hitstop_frames` out-param; screenshake struct |
| `engine/character/core/HitReactSystem.cpp` | Modify | Hitstop-aware knockback |
| `engine/character/core/CharacterCatalog.cpp` | Modify | Load all 8 confirmed combat clips + attempt Fight GLBs |
| `movement/MovementApp.hpp` | Modify | New fields: `InputBuffer`, `ScreenShake`, `ChainTable` |
| `movement/MovementApp.cpp` | Modify | New data flow order; 4 chains; screenshake; updated overlay |
| `assets/character/combat_attacks.txt` | Modify | Expand to 8 attacks with `cancel_window` and `dodge_cancel_window` fields |
| `engine/CMakeLists.txt` | Modify | Add `InputBuffer.cpp` to `ENGINE_CHARACTER_CORE_SOURCES` |
| `tests/CMakeLists.txt` | Modify | Add `character/test_input_buffer.cpp` to `character_tests` |
| `tests/character/test_input_buffer.cpp` | **Create** | InputBuffer unit tests |
| `tests/character/test_combo_fsm.cpp` | Modify | Update for new FSM signature and phases |
| `tests/character/test_attack_data.cpp` | Modify | Update for renamed fields |
| `tests/character/test_hit_window.cpp` | Modify | Update field references |

---

## Task 1: InputSnapshot — 4 attack bools + dodge

**Files:**
- Modify: `engine/movement/core/InputSnapshot.hpp`
- Modify: `movement/MovementApp.cpp` (poll_input only)

- [ ] **Step 1.1: Update InputSnapshot**

Replace the single `attack_pressed` with four attack booleans and a dodge bool:

```cpp
// engine/movement/core/InputSnapshot.hpp
#pragma once
#include <glm/glm.hpp>

namespace engine::movement {

struct InputSnapshot {
    bool move_forward = false;
    bool move_back    = false;
    bool move_left    = false;
    bool move_right   = false;
    bool sprint       = false;

    bool jump_pressed    = false;  // edge-triggered, Space
    bool dodge_pressed   = false;  // edge-triggered, Space (same key — jump cleared first, dodge used in combat)

    // Edge-triggered attack buttons (cleared by InputBuffer push)
    bool attack_light   = false;  // LMB
    bool attack_heavy   = false;  // RMB
    bool attack_kick    = false;  // Q
    bool attack_special = false;  // E

    glm::vec2 mouse_delta{0.f};
    float     scroll_delta = 0.f;

    [[nodiscard]] bool any_move() const {
        return move_forward || move_back || move_left || move_right;
    }
    [[nodiscard]] bool any_attack() const {
        return attack_light || attack_heavy || attack_kick || attack_special;
    }
};

} // namespace engine::movement
```

- [ ] **Step 1.2: Update poll_input in MovementApp.cpp**

Replace the LMB latch block with four separate latches. Find the existing block (around line 198) and replace:

```cpp
// In MovementApp.hpp, add these private fields alongside jump_down_/jump_latch_:
bool attack_light_down_   = false; bool attack_light_latch_   = false;
bool attack_heavy_down_   = false; bool attack_heavy_latch_   = false;
bool attack_kick_down_    = false; bool attack_kick_latch_    = false;
bool attack_special_down_ = false; bool attack_special_latch_ = false;
// Remove: bool attack_down_ = false; bool attack_latch_ = false;
```

```cpp
// In MovementApp.cpp poll_input(), replace attack latch block with:

auto edge_latch = [](bool now, bool& down, bool& latch) {
    if (now && !down) latch = true;
    if (!now)         latch = false;
    down = now;
};

edge_latch(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS,
           attack_light_down_,   attack_light_latch_);
edge_latch(glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS,
           attack_heavy_down_,   attack_heavy_latch_);
edge_latch(glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS,
           attack_kick_down_,    attack_kick_latch_);
edge_latch(glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS,
           attack_special_down_, attack_special_latch_);

snap.attack_light   = attack_light_latch_;
snap.attack_heavy   = attack_heavy_latch_;
snap.attack_kick    = attack_kick_latch_;
snap.attack_special = attack_special_latch_;
snap.dodge_pressed  = snap.jump_pressed; // Space doubles as dodge during combat
```

- [ ] **Step 1.3: Build and fix any remaining references to `attack_pressed`**

```
cmake --build build --config Debug --target character_tests 2>&1 | grep "error:"
cmake --build build --config Debug --target movement 2>&1 | grep "error:"
```

Fix any remaining `snap.attack_pressed` references — they should not exist after this step.

- [ ] **Step 1.4: Commit**

```
git add engine/movement/core/InputSnapshot.hpp movement/MovementApp.cpp movement/MovementApp.hpp
git commit -m "refactor: replace attack_pressed with 4 attack bools + dodge_pressed"
```

---

## Task 2: InputBuffer — 10-frame ring buffer

**Files:**
- Create: `engine/character/core/InputBuffer.hpp`
- Create: `engine/character/core/InputBuffer.cpp`
- Create: `tests/character/test_input_buffer.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 2.1: Write the failing tests**

```cpp
// tests/character/test_input_buffer.cpp
#include <catch2/catch_test_macros.hpp>
#include "engine/character/core/InputBuffer.hpp"

using namespace engine::character;
using Kind = BufferedInput::Kind;

TEST_CASE("empty buffer returns None", "[input_buffer]") {
    InputBuffer buf;
    REQUIRE(buf.peek() == Kind::None);
}

TEST_CASE("pushed input is visible via peek", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Light);
    REQUIRE(buf.peek() == Kind::Light);
}

TEST_CASE("consume removes oldest entry", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Light);
    buf.push(Kind::Heavy);
    buf.consume();
    REQUIRE(buf.peek() == Kind::Heavy);
}

TEST_CASE("tick decrements TTL; expired entries removed", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Kick, 2); // TTL = 2
    buf.tick(); // TTL = 1
    REQUIRE(buf.peek() == Kind::Kick);
    buf.tick(); // TTL = 0 -> expired
    REQUIRE(buf.peek() == Kind::None);
}

TEST_CASE("clear empties buffer", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Light);
    buf.push(Kind::Heavy);
    buf.clear();
    REQUIRE(buf.peek() == Kind::None);
}

TEST_CASE("buffer capacity: oldest entry dropped when full", "[input_buffer]") {
    InputBuffer buf;
    for (int i = 0; i < InputBuffer::kCapacity + 2; ++i) {
        buf.push(Kind::Light, InputBuffer::kDefaultTTL);
    }
    // Should not crash; peek still valid
    REQUIRE(buf.peek() == Kind::Light);
}
```

- [ ] **Step 2.2: Run tests — expect compile failure**

```
cmake --build build --config Debug --target character_tests 2>&1 | grep "error:"
```

Expected: `InputBuffer.hpp not found`

- [ ] **Step 2.3: Create InputBuffer.hpp**

```cpp
// engine/character/core/InputBuffer.hpp
#pragma once
#include <array>
#include <cstddef>

namespace engine::character {

struct BufferedInput {
    enum class Kind { None, Light, Heavy, Kick, Special, Dodge };
    Kind kind       = Kind::None;
    int  ttl_frames = 0;
};

class InputBuffer {
public:
    static constexpr int kCapacity   = 10;
    static constexpr int kDefaultTTL = 10;  // ~167 ms at 60 Hz

    void push(BufferedInput::Kind kind, int ttl = kDefaultTTL);
    [[nodiscard]] BufferedInput::Kind peek() const;
    void consume();
    void tick();
    void clear();

private:
    std::array<BufferedInput, kCapacity> entries_{};
    int head_ = 0;  // index of oldest entry
    int size_ = 0;
};

} // namespace engine::character
```

- [ ] **Step 2.4: Create InputBuffer.cpp**

```cpp
// engine/character/core/InputBuffer.cpp
#include "engine/character/core/InputBuffer.hpp"

namespace engine::character {

void InputBuffer::push(BufferedInput::Kind kind, int ttl) {
    if (size_ == kCapacity) {
        head_ = (head_ + 1) % kCapacity; // drop oldest
        --size_;
    }
    const int tail = (head_ + size_) % kCapacity;
    entries_[tail] = {kind, ttl};
    ++size_;
}

BufferedInput::Kind InputBuffer::peek() const {
    if (size_ == 0) return BufferedInput::Kind::None;
    return entries_[head_].kind;
}

void InputBuffer::consume() {
    if (size_ == 0) return;
    head_ = (head_ + 1) % kCapacity;
    --size_;
}

void InputBuffer::tick() {
    int i = 0;
    while (i < size_) {
        const int idx = (head_ + i) % kCapacity;
        --entries_[idx].ttl_frames;
        if (entries_[idx].ttl_frames <= 0) {
            // Remove by shifting remaining entries forward
            for (int j = i; j < size_ - 1; ++j) {
                const int cur  = (head_ + j)     % kCapacity;
                const int next = (head_ + j + 1) % kCapacity;
                entries_[cur] = entries_[next];
            }
            --size_;
            // Don't increment i — check same slot again
        } else {
            ++i;
        }
    }
}

void InputBuffer::clear() {
    head_ = 0;
    size_ = 0;
}

} // namespace engine::character
```

- [ ] **Step 2.5: Register InputBuffer in CMakeLists**

In `engine/CMakeLists.txt`, add to `ENGINE_CHARACTER_CORE_SOURCES`:
```cmake
${CMAKE_SOURCE_DIR}/engine/character/core/InputBuffer.cpp
```

In `tests/CMakeLists.txt`, add to `character_tests` sources:
```cmake
character/test_input_buffer.cpp
```

- [ ] **Step 2.6: Run tests — expect pass**

```
cmake --build build --config Debug --target character_tests
ctest --test-dir build -C Debug -R "input_buffer" -V
```

Expected: 6 tests pass.

- [ ] **Step 2.7: Commit**

```
git add engine/character/core/InputBuffer.hpp engine/character/core/InputBuffer.cpp
git add tests/character/test_input_buffer.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add InputBuffer ring buffer with 10-frame TTL"
```

---

## Task 3: CharacterComponents — extend structs

**Files:**
- Modify: `engine/character/core/CharacterComponents.hpp`

- [ ] **Step 3.1: Update CharacterComponents.hpp**

Replace the entire file:

```cpp
// engine/character/core/CharacterComponents.hpp
#pragma once

#include "engine/character/core/InputBuffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::character {

struct SkinnedModel {
    std::string glb_path;
    std::uint32_t catalog_id = 0;
};

struct AnimationState {
    std::string active_clip;
    float       time_seconds   = 0.f;
    float       speed          = 1.f;
    bool        looping        = true;

    // Blend-out slot: previous clip fading to 0
    std::string blend_clip;
    float       blend_time     = 0.f;   // playback position in blend_clip
    float       blend_weight   = 0.f;   // 1.0 = fully blend_clip, decays to 0
    float       blend_duration = 0.12f;
};

// Startup: pre-hit wind-up frames (norm_time < hit_start_norm)
// Active:  hit window open (hit_start_norm <= norm_time <= hit_end_norm)
// Recovery: committed end-lag (norm_time > hit_end_norm, until clip ends)
// DodgeCancel: one-frame state, applies dodge impulse then → Idle
enum class CombatPhase { Idle, Startup, Active, Recovery, DodgeCancel };

struct CombatController {
    CombatPhase phase       = CombatPhase::Idle;
    int         combo_index = 0;
    std::vector<std::string> combo_ids;   // set from ChainTable at combo start
    BufferedInput::Kind      active_kind  = BufferedInput::Kind::None;

    float attack_yaw   = 0.f;
    bool  hit_consumed = false;

    // Hitstop overlay (not a CombatPhase value)
    bool        hitstop_active       = false;
    CombatPhase phase_before_hitstop = CombatPhase::Idle;
    int         hitstop_frames       = 0;

    // Set true for one frame by DodgeCancel; MovementApp reads + clears it
    bool dodge_requested = false;
};

struct HitReact {
    float       knockback_distance = 0.3f;
    float       knockback_duration = 0.25f;
    std::string hit_clip           = "Hit_Reaction_1";
    float       timer              = 0.f;
    glm::vec3   knockback_delta{0.f};
    bool        playing_hit        = false;
};

} // namespace engine::character
```

- [ ] **Step 3.2: Build character_tests to check struct changes compile**

```
cmake --build build --config Debug --target character_tests 2>&1 | grep "error:" | head -20
```

Expected: errors about `CombatPhase::Attacking` (we'll fix those in Task 6).

- [ ] **Step 3.3: Commit**

```
git add engine/character/core/CharacterComponents.hpp
git commit -m "refactor: extend CharacterComponents — blend slot, hitstop overlay, new CombatPhase values"
```

---

## Task 4: AttackData — rename fields, add cancel windows, fix HitDetection

**Files:**
- Modify: `engine/character/core/AttackData.hpp`
- Modify: `engine/character/core/AttackData.cpp`
- Modify: `engine/character/core/HitDetection.cpp`
- Modify: `tests/character/test_attack_data.cpp`
- Modify: `tests/character/test_hit_window.cpp`
- Modify: `assets/character/combat_attacks.txt`

- [ ] **Step 4.1: Write failing tests for new AttackDef fields**

Open `tests/character/test_attack_data.cpp`. Add these test cases alongside existing ones:

```cpp
TEST_CASE("AttackData parses cancel_window fields", "[attack_data]") {
    // Write to a temp file and load
    const std::string tmp = std::string(ENGINE_SOURCE_DIR) +
        "/assets/character/combat_attacks_test_cancel.txt";
    {
        std::ofstream f(tmp);
        f << "attack test_cancel {\n"
          << "    clip TestClip\n"
          << "    hit_window 0.30 0.50\n"
          << "    range 1.0\n"
          << "    radius 0.3\n"
          << "    recovery 0.2\n"
          << "    cancel_window 0.70\n"
          << "    dodge_cancel_window 0.60\n"
          << "}\n";
    }
    const auto table = engine::character::AttackData::load(tmp);
    REQUIRE(table.count("test_cancel"));
    const auto& def = table.at("test_cancel");
    REQUIRE(def.cancel_start_norm       == Catch::Approx(0.70f));
    REQUIRE(def.dodge_cancel_start_norm == Catch::Approx(0.60f));
    std::filesystem::remove(tmp);
}

TEST_CASE("AttackData asserts cancel >= hit_end", "[attack_data]") {
    const std::string tmp = std::string(ENGINE_SOURCE_DIR) +
        "/assets/character/combat_attacks_test_bad.txt";
    {
        std::ofstream f(tmp);
        f << "attack bad {\n"
          << "    clip X\n"
          << "    hit_window 0.30 0.50\n"
          << "    range 1.0\n"
          << "    radius 0.3\n"
          << "    recovery 0.2\n"
          << "    cancel_window 0.40\n"   // < hit_end 0.50 -> invalid
          << "}\n";
    }
    REQUIRE_THROWS(engine::character::AttackData::load(tmp));
    std::filesystem::remove(tmp);
}
```

- [ ] **Step 4.2: Update AttackData.hpp**

```cpp
// engine/character/core/AttackData.hpp
#pragma once
#include <string>
#include <unordered_map>

namespace engine::character {

struct AttackDef {
    std::string id;
    std::string clip;
    float hit_start_norm          = 0.f;   // [0,1] normalized: hit window opens
    float hit_end_norm            = 0.f;   // [0,1] normalized: hit window closes
    float range                   = 0.f;   // metres: hit capsule forward offset
    float radius                  = 0.f;   // metres: hit capsule radius
    float recovery_seconds        = 0.2f;  // seconds of end-lag after last combo hit
    float cancel_start_norm       = 0.7f;  // [0,1]: buffer accepts combo continuation from here
    float dodge_cancel_start_norm = 0.6f;  // [0,1]: Space cancels into dodge from here
    // Invariants (asserted on load):
    //   cancel_start_norm >= hit_end_norm
    //   dodge_cancel_start_norm <= cancel_start_norm
};

using AttackTable = std::unordered_map<std::string, AttackDef>;

class AttackData {
public:
    [[nodiscard]] static AttackTable load(const std::string& path);
};

} // namespace engine::character
```

- [ ] **Step 4.3: Update AttackData.cpp parser**

In `AttackData.cpp`, update `parse_attack_block`:

Replace the field name handling block. Find the `if (field == "hit_window")` block and update ALL field names, add new fields, and add invariant assertions:

```cpp
// In parse_attack_block, update the field handling:

        if (field == "clip") {
            check_dup("clip");
            def.clip = lex.read_token();
        } else if (field == "hit_window") {
            check_dup("hit_window");
            def.hit_start_norm = lex.read_float("hit_window.start");
            def.hit_end_norm   = lex.read_float("hit_window.end");
        } else if (field == "range") {
            check_dup("range");
            def.range = lex.read_float("range");
        } else if (field == "radius") {
            check_dup("radius");
            def.radius = lex.read_float("radius");
        } else if (field == "recovery") {
            check_dup("recovery");
            def.recovery_seconds = lex.read_float("recovery");
        } else if (field == "cancel_window") {
            check_dup("cancel_window");
            def.cancel_start_norm = lex.read_float("cancel_window");
        } else if (field == "dodge_cancel_window") {
            check_dup("dodge_cancel_window");
            def.dodge_cancel_start_norm = lex.read_float("dodge_cancel_window");
        } else {
            throw std::runtime_error(
                lex.source + ":" + std::to_string(lex.line) + ":" +
                std::to_string(lex.col) + ": unknown field '" + field +
                "' in attack '" + id + "'");
        }
```

After the closing `}` of `parse_attack_block`, before the `return def;`, add:

```cpp
    // Invariant assertions
    if (def.cancel_start_norm < def.hit_end_norm) {
        throw std::runtime_error(
            lex.source + ": attack '" + id +
            "': cancel_window must be >= hit_end (hit_window end)");
    }
    if (def.dodge_cancel_start_norm > def.cancel_start_norm) {
        throw std::runtime_error(
            lex.source + ": attack '" + id +
            "': dodge_cancel_window must be <= cancel_window");
    }
```

- [ ] **Step 4.4: Update HitDetection.cpp**

In `HitDetection.cpp`, find references to `def.hit_start`, `def.hit_end`, `def.recovery` and update:
- `def.hit_start` → `def.hit_start_norm`
- `def.hit_end` → `def.hit_end_norm`

Also simplify the `norm_t` calculation. Find the current calculation (which uses `clip_remaining`) and replace with:

```cpp
// HitDetection.cpp — try_hit_in_window
// Old: complex clip_remaining calculation
// New: use anim.time_seconds directly
[[nodiscard]] bool try_hit_in_window(CombatController& combat,
                                     const AnimationState& anim,
                                     const AttackDef& def,
                                     const engine::movement::Transform& attacker,
                                     const engine::movement::Transform& target,
                                     const engine::movement::Collider& target_collider) {
    if (combat.hit_consumed) return false;
    if (combat.phase != CombatPhase::Active) return false;

    // norm_t: use anim.time_seconds; caller must pass clip duration via anim or def
    // We compute it from anim.time_seconds and the clip duration looked up externally.
    // For simplicity, keep the existing approach but use the renamed fields:
    // norm_t is passed via anim.time_seconds relative to total clip duration.
    // See MovementApp for how clip duration is passed.
    // The hit window check uses the same time that AnimationController::tick advanced.
    
    // (Full implementation: norm_t = anim.time_seconds / clip_duration,
    //  where clip_duration must be known. The simplest fix: pass clip_duration as parameter.)
    return false; // placeholder — see Step 4.5
}
```

- [ ] **Step 4.5: Update HitDetection.hpp and .cpp with clip_duration parameter**

```cpp
// HitDetection.hpp — updated signature
[[nodiscard]] bool try_hit_in_window(CombatController& combat,
                                     const AnimationState& anim,
                                     const AttackDef& def,
                                     float clip_duration,
                                     const engine::movement::Transform& attacker,
                                     const engine::movement::Transform& target,
                                     const engine::movement::Collider& target_collider);
```

```cpp
// HitDetection.cpp — full implementation
[[nodiscard]] bool try_hit_in_window(CombatController& combat,
                                     const AnimationState& anim,
                                     const AttackDef& def,
                                     float clip_duration,
                                     const engine::movement::Transform& attacker,
                                     const engine::movement::Transform& target,
                                     const engine::movement::Collider& target_collider) {
    if (combat.hit_consumed) return false;
    if (combat.phase != CombatPhase::Active) return false;

    const float norm_t = (clip_duration > 1e-5f)
        ? anim.time_seconds / clip_duration
        : 1.f;
    if (norm_t < def.hit_start_norm || norm_t > def.hit_end_norm) return false;

    const float yaw = combat.attack_yaw;
    const glm::vec3 fwd(std::sin(yaw), 0.f, std::cos(yaw));
    const glm::vec3 hit_center = attacker.position + fwd * def.range;

    const bool hit = capsule_intersects_box(
        hit_center, def.radius, def.radius * 0.5f,
        target.position, target_collider.half_extents);

    if (hit) {
        combat.hit_consumed = true;
    }
    return hit;
}
```

- [ ] **Step 4.6: Update combat_attacks.txt with cancel windows**

```
# combat_attacks.txt — v2 attack table (Combat Redesign)
# Fields: clip, hit_window [start_norm end_norm], range, radius,
#         recovery (seconds), cancel_window (norm), dodge_cancel_window (norm)

attack high_kick {
    clip High_Kick
    hit_window 0.35 0.48
    range 1.25
    radius 0.35
    recovery 0.25
    cancel_window 0.72
    dodge_cancel_window 0.62
}

attack elbow_strike {
    clip Elbow_Strike
    hit_window 0.32 0.44
    range 0.85
    radius 0.30
    recovery 0.20
    cancel_window 0.70
    dodge_cancel_window 0.60
}

attack counterstrike {
    clip Counterstrike
    hit_window 0.38 0.55
    range 1.05
    radius 0.35
    recovery 0.35
    cancel_window 0.75
    dodge_cancel_window 0.65
}

attack spartan_kick {
    clip Spartan_Kick
    hit_window 0.33 0.50
    range 1.40
    radius 0.40
    recovery 0.30
    cancel_window 0.72
    dodge_cancel_window 0.62
}

attack sweeping_kick {
    clip Sweeping_Kick
    hit_window 0.30 0.55
    range 1.10
    radius 0.45
    recovery 0.20
    cancel_window 0.70
    dodge_cancel_window 0.60
}

attack lunge_spin_kick {
    clip Lunge_Spin_Kick
    hit_window 0.40 0.62
    range 1.30
    radius 0.40
    recovery 0.40
    cancel_window 0.78
    dodge_cancel_window 0.65
}

attack dodge_and_counter {
    clip Dodge_and_Counter
    hit_window 0.55 0.75
    range 0.90
    radius 0.30
    recovery 0.20
    cancel_window 0.80
    dodge_cancel_window 0.70
}

attack shield_push {
    clip Shield_Push_Left
    hit_window 0.25 0.45
    range 0.70
    radius 0.50
    recovery 0.25
    cancel_window 0.70
    dodge_cancel_window 0.60
}
```

- [ ] **Step 4.7: Fix test_attack_data.cpp and test_hit_window.cpp**

In `tests/character/test_attack_data.cpp`, update any direct field references:
- `def.hit_start` → `def.hit_start_norm`
- `def.hit_end` → `def.hit_end_norm`
- `def.recovery` → `def.recovery_seconds`

In `tests/character/test_hit_window.cpp`, update field references and add `clip_duration` parameter to `try_hit_in_window` calls.

- [ ] **Step 4.8: Build and run tests**

```
cmake --build build --config Debug --target character_tests
ctest --test-dir build -C Debug -R "attack_data|hit_window|hit_overlap" -V
```

Expected: all attack_data and hit tests pass.

- [ ] **Step 4.9: Commit**

```
git add engine/character/core/AttackData.hpp engine/character/core/AttackData.cpp
git add engine/character/core/HitDetection.hpp engine/character/core/HitDetection.cpp
git add assets/character/combat_attacks.txt
git add tests/character/test_attack_data.cpp tests/character/test_hit_window.cpp
git commit -m "refactor: AttackData renamed fields (_norm/_seconds), cancel windows, update HitDetection"
```

---

## Task 5: AnimationController — crossfade_to and TRS blend

**Files:**
- Modify: `engine/character/core/AnimationController.hpp`
- Modify: `engine/character/core/AnimationController.cpp`

- [ ] **Step 5.1: Write failing tests for crossfade_to and blend decay**

Add to the existing `test_combo_fsm.cpp` or create `tests/character/test_animation_controller.cpp` if it doesn't exist:

```cpp
// Add these test cases to tests/character/test_combo_fsm.cpp
// (or a new test_animation_controller.cpp — add it to tests/CMakeLists.txt if new)

#include "engine/character/core/AnimationController.hpp"
#include "engine/character/core/CharacterComponents.hpp"

TEST_CASE("crossfade_to sets blend fields correctly", "[animation]") {
    using namespace engine::character;
    AnimationState anim;
    anim.active_clip   = "Walk";
    anim.time_seconds  = 0.3f;

    AnimationController::crossfade_to(anim, "High_Kick");

    REQUIRE(anim.blend_clip    == "Walk");
    REQUIRE(anim.blend_time    == Catch::Approx(0.3f));
    REQUIRE(anim.blend_weight  == Catch::Approx(1.f));
    REQUIRE(anim.active_clip   == "High_Kick");
    REQUIRE(anim.time_seconds  == Catch::Approx(0.f));
    REQUIRE(anim.looping       == false);
}

TEST_CASE("tick decays blend_weight to zero", "[animation]") {
    using namespace engine::character;
    AnimationState anim;
    anim.active_clip    = "High_Kick";
    anim.blend_clip     = "Walk";
    anim.blend_weight   = 1.f;
    anim.blend_duration = 0.1f;
    anim.speed          = 1.f;

    AnimClip clip;
    clip.name = "High_Kick";
    clip.duration_seconds = 1.f;

    AnimationController::tick(anim, &clip, 0.05f);
    REQUIRE(anim.blend_weight == Catch::Approx(0.5f));

    AnimationController::tick(anim, &clip, 0.05f);
    REQUIRE(anim.blend_weight == Catch::Approx(0.f));
    REQUIRE(anim.blend_clip.empty());
}

TEST_CASE("crossfade_to no-op when clip unchanged", "[animation]") {
    using namespace engine::character;
    AnimationState anim;
    anim.active_clip  = "Walk";
    anim.time_seconds = 0.5f;

    AnimationController::crossfade_to(anim, "Walk");

    REQUIRE(anim.blend_clip.empty());
    REQUIRE(anim.time_seconds == Catch::Approx(0.5f)); // unchanged
}
```

- [ ] **Step 5.2: Run tests — expect compile failure**

```
cmake --build build --config Debug --target character_tests 2>&1 | grep "error:"
```

Expected: `crossfade_to` not declared.

- [ ] **Step 5.3: Update AnimationController.hpp**

```cpp
// engine/character/core/AnimationController.hpp
#pragma once

#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::character {

class AnimationController {
public:
    [[nodiscard]] static std::string select_locomotion(float speed, bool grounded);

    // Advances anim.time_seconds and decays anim.blend_weight.
    // anim.speed = 0 during hitstop — this function still runs but advances nothing.
    static void tick(AnimationState& anim, const AnimClip* clip, float dt);

    // Switch to new_clip with a crossfade. Previous clip fades out over blend_duration.
    // No-op if new_clip == anim.active_clip.
    static void crossfade_to(AnimationState& anim,
                              const std::string& new_clip,
                              float blend_duration = 0.12f,
                              bool looping = false);

    [[nodiscard]] static float normalized_time(const AnimationState& anim,
                                               const AnimClip* clip);

    // Samples bone matrices, blending blend_clip → active_clip when blend_weight > 0.
    // Blending is done at TRS level (slerp rotation, lerp translation/scale).
    // blend_clip_ptr may be null — if so, no blending occurs.
    [[nodiscard]] static std::vector<glm::mat4> sample_bone_matrices(
        const AnimationState& anim,
        const AnimClip* clip,
        const SkinnedMeshData& mesh,
        const AnimClip* blend_clip_ptr = nullptr);
};

} // namespace engine::character
```

- [ ] **Step 5.4: Update AnimationController.cpp**

Replace the entire file:

```cpp
// engine/character/core/AnimationController.cpp
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

template <typename T>
T lerp_channel(const std::vector<float>& times, const std::vector<T>& values, float t, T identity) {
    if (values.empty()) return identity;
    if (values.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();
    auto it = std::lower_bound(times.begin(), times.end(), t);
    const std::size_t hi = static_cast<std::size_t>(it - times.begin());
    const std::size_t lo = hi - 1;
    const float alpha = (t - times[lo]) / (times[hi] - times[lo]);
    return glm::mix(values[lo], values[hi], alpha);
}

glm::quat slerp_channel(const std::vector<float>& times, const std::vector<glm::quat>& values, float t) {
    const glm::quat identity(1.f, 0.f, 0.f, 0.f);
    if (values.empty()) return identity;
    if (values.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();
    auto it = std::lower_bound(times.begin(), times.end(), t);
    const std::size_t hi = static_cast<std::size_t>(it - times.begin());
    const std::size_t lo = hi - 1;
    const float alpha = (t - times[lo]) / (times[hi] - times[lo]);
    return glm::slerp(values[lo], values[hi], alpha);
}

glm::mat4 trs_matrix(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
    return glm::translate(glm::mat4(1.f), t) * glm::toMat4(r) * glm::scale(glm::mat4(1.f), s);
}

struct BonePose { glm::vec3 t{0.f}; glm::quat r{1.f, 0.f, 0.f, 0.f}; glm::vec3 s{1.f}; };

// Sample local TRS for every bone from a single clip.
std::vector<BonePose> sample_pose(const AnimClip* clip, float time_seconds,
                                   const SkinnedMeshData& mesh) {
    const std::size_t bone_count = mesh.bones.size();
    std::vector<BonePose> poses(bone_count);

    if (!clip || clip->channels.empty()) return poses;

    std::unordered_map<std::string, const AnimChannel*> by_bone;
    by_bone.reserve(clip->channels.size());
    for (const AnimChannel& ch : clip->channels) {
        by_bone[ch.target_joint] = &ch;
    }

    const float t = time_seconds;
    for (std::size_t i = 0; i < bone_count; ++i) {
        auto it = by_bone.find(mesh.bones[i].name);
        if (it == by_bone.end()) continue;
        const AnimChannel& ch = *it->second;
        poses[i].t = lerp_channel(ch.translation_times, ch.translations, t, glm::vec3(0.f));
        poses[i].r = slerp_channel(ch.rotation_times, ch.rotations, t);
        poses[i].s = lerp_channel(ch.scale_times, ch.scales, t, glm::vec3(1.f));
    }
    return poses;
}

std::vector<glm::mat4> poses_to_bone_matrices(const std::vector<BonePose>& poses,
                                               const SkinnedMeshData& mesh) {
    const std::size_t bone_count = mesh.bones.size();
    std::vector<glm::mat4> local(bone_count, glm::mat4(1.f));
    std::vector<glm::mat4> global(bone_count, glm::mat4(1.f));

    for (std::size_t i = 0; i < bone_count; ++i) {
        local[i] = trs_matrix(poses[i].t, poses[i].r, poses[i].s);
    }
    for (std::size_t i = 0; i < bone_count; ++i) {
        const int parent = mesh.bones[i].parent;
        global[i] = (parent < 0) ? local[i]
                                  : global[static_cast<std::size_t>(parent)] * local[i];
    }

    std::vector<glm::mat4> result(bone_count, glm::mat4(1.f));
    for (std::size_t i = 0; i < bone_count; ++i) {
        result[i] = (i < mesh.inverse_bind_matrices.size())
            ? global[i] * mesh.inverse_bind_matrices[i]
            : global[i];
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------

std::string AnimationController::select_locomotion(float speed, bool grounded) {
    if (!grounded || speed < 0.1f) return "Walk";
    if (speed < 3.0f)              return "Walk";
    return "Run";
}

void AnimationController::tick(AnimationState& anim, const AnimClip* clip, float dt) {
    if (!clip || clip->duration_seconds <= 0.f) return;

    anim.time_seconds += dt * anim.speed;
    if (anim.looping) {
        anim.time_seconds = std::fmod(anim.time_seconds, clip->duration_seconds);
        if (anim.time_seconds < 0.f) anim.time_seconds += clip->duration_seconds;
    } else {
        anim.time_seconds = std::min(anim.time_seconds, clip->duration_seconds);
    }

    // Decay blend weight
    if (anim.blend_weight > 0.f && anim.blend_duration > 1e-5f) {
        anim.blend_time   += dt;
        anim.blend_weight -= dt / anim.blend_duration;
        if (anim.blend_weight <= 0.f) {
            anim.blend_weight = 0.f;
            anim.blend_clip.clear();
        }
    }
}

void AnimationController::crossfade_to(AnimationState& anim,
                                        const std::string& new_clip,
                                        float blend_duration,
                                        bool looping) {
    if (anim.active_clip == new_clip) return;
    anim.blend_clip     = anim.active_clip;
    anim.blend_time     = anim.time_seconds;
    anim.blend_weight   = 1.f;
    anim.blend_duration = blend_duration;
    anim.active_clip    = new_clip;
    anim.time_seconds   = 0.f;
    anim.looping        = looping;
    anim.speed          = 1.f;
}

float AnimationController::normalized_time(const AnimationState& anim, const AnimClip* clip) {
    if (!clip || clip->duration_seconds <= 0.f) return 0.f;
    return anim.time_seconds / clip->duration_seconds;
}

std::vector<glm::mat4> AnimationController::sample_bone_matrices(
    const AnimationState& anim,
    const AnimClip* clip,
    const SkinnedMeshData& mesh,
    const AnimClip* blend_clip_ptr) {

    const std::vector<BonePose> primary = sample_pose(clip, anim.time_seconds, mesh);

    if (blend_clip_ptr && anim.blend_weight > 1e-5f) {
        const std::vector<BonePose> secondary = sample_pose(blend_clip_ptr, anim.blend_time, mesh);
        const float w = anim.blend_weight; // 0 = fully primary, 1 = fully secondary
        const std::size_t n = mesh.bones.size();
        std::vector<BonePose> blended(n);
        for (std::size_t i = 0; i < n; ++i) {
            blended[i].t = glm::mix(primary[i].t, secondary[i].t, w);
            blended[i].r = glm::slerp(primary[i].r, secondary[i].r, w);
            blended[i].s = glm::mix(primary[i].s, secondary[i].s, w);
        }
        return poses_to_bone_matrices(blended, mesh);
    }

    return poses_to_bone_matrices(primary, mesh);
}

} // namespace engine::character
```

- [ ] **Step 5.5: Run animation tests**

```
cmake --build build --config Debug --target character_tests
ctest --test-dir build -C Debug -R "animation" -V
```

Expected: crossfade and blend decay tests pass.

- [ ] **Step 5.6: Commit**

```
git add engine/character/core/AnimationController.hpp engine/character/core/AnimationController.cpp
git add tests/character/test_combo_fsm.cpp
git commit -m "feat: AnimationController crossfade_to + TRS-level pose blending"
```

---

## Task 6: CombatController — full FSM rewrite

**Files:**
- Modify: `engine/character/core/CombatController.hpp`
- Modify: `engine/character/core/CombatController.cpp`
- Modify: `tests/character/test_combo_fsm.cpp`

- [ ] **Step 6.1: Rewrite test_combo_fsm.cpp for new API**

```cpp
// tests/character/test_combo_fsm.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/character/core/CombatController.hpp"
#include "engine/character/core/InputBuffer.hpp"
#include "engine/movement/core/Components.hpp"

using namespace engine::character;
using namespace engine::movement;
using Kind = BufferedInput::Kind;

static AttackTable make_table() {
    AttackTable t;
    t["punch"]  = AttackDef{"punch",  "Punch",  0.30f, 0.50f, 1.0f, 0.4f, 0.2f, 0.70f, 0.60f};
    t["kick"]   = AttackDef{"kick",   "Kick",   0.40f, 0.60f, 1.2f, 0.4f, 0.2f, 0.72f, 0.62f};
    t["finish"] = AttackDef{"finish", "Finish", 0.30f, 0.50f, 1.0f, 0.4f, 0.3f, 0.75f, 0.65f};
    return t;
}

static std::vector<AnimClip> make_clips() {
    AnimClip a, b, c;
    a.name = "Punch";  a.duration_seconds = 0.5f;
    b.name = "Kick";   b.duration_seconds = 0.6f;
    c.name = "Finish"; c.duration_seconds = 0.5f;
    return {a, b, c};
}

static ChainTable make_chains() {
    return {{kLightChain, {"punch", "kick", "finish"}}};
}

static Transform make_transform() {
    Transform tf; tf.position = {0.f, 0.f, 0.f}; tf.yaw = 0.f; return tf;
}

TEST_CASE("idle + buffered Light starts first combo hit", "[combo_fsm]") {
    auto attacks = make_table(); auto clips = make_clips();
    auto chains  = make_chains(); auto tf = make_transform();
    CombatController combat; AnimationState anim; InputBuffer buf;

    buf.push(Kind::Light);
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, 1.f/60.f);

    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(combat.combo_index == 0);
    REQUIRE(anim.active_clip == "Punch");
    REQUIRE(combat.hit_consumed == false);
}

TEST_CASE("Startup transitions to Active at hit_start_norm", "[combo_fsm]") {
    auto attacks = make_table(); auto clips = make_clips();
    auto chains  = make_chains(); auto tf = make_transform();
    CombatController combat; AnimationState anim; InputBuffer buf;

    buf.push(Kind::Light);
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, 1.f/60.f);
    REQUIRE(combat.phase == CombatPhase::Startup);

    // Advance anim past hit_start_norm (0.30) for Punch (duration 0.5s)
    // hit_start_norm=0.30 → time >= 0.15s
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, 1.f/60.f);
    REQUIRE(combat.phase == CombatPhase::Active);
}

TEST_CASE("no buffer input in Recovery → Idle after clip ends", "[combo_fsm]") {
    auto attacks = make_table(); auto clips = make_clips();
    auto chains  = make_chains(); auto tf = make_transform();
    CombatController combat; AnimationState anim; InputBuffer buf;

    buf.push(Kind::Light);
    const float dt = 1.f/60.f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Startup);

    // Drive to Active
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Active);

    // Drive to Recovery (past hit_end_norm=0.50 → time >= 0.25s)
    anim.time_seconds = 0.26f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Recovery);

    // Drive past clip end (Punch = 0.5s)
    anim.time_seconds = 0.51f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);
    REQUIRE(combat.phase == CombatPhase::Idle);
}

TEST_CASE("buffered input in Recovery chains to next attack", "[combo_fsm]") {
    auto attacks = make_table(); auto clips = make_clips();
    auto chains  = make_chains(); auto tf = make_transform();
    CombatController combat; AnimationState anim; InputBuffer buf;

    buf.push(Kind::Light);
    const float dt = 1.f/60.f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt); // → Active
    anim.time_seconds = 0.26f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt); // → Recovery

    // Buffer second press during recovery
    buf.push(Kind::Light);
    // Advance to cancel window (cancel_start_norm=0.70 → time >= 0.35s for 0.5s clip)
    anim.time_seconds = 0.36f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);

    REQUIRE(combat.phase == CombatPhase::Startup);
    REQUIRE(combat.combo_index == 1);
    REQUIRE(anim.active_clip == "Kick");
}

TEST_CASE("attack_yaw locked at combo start", "[combo_fsm]") {
    auto attacks = make_table(); auto clips = make_clips();
    auto chains  = make_chains(); auto tf = make_transform();
    tf.yaw = 1.5f;
    CombatController combat; AnimationState anim; InputBuffer buf;

    buf.push(Kind::Light);
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, 1.f/60.f);
    REQUIRE(combat.attack_yaw == Catch::Approx(1.5f));
}

TEST_CASE("hitstop freezes FSM for N frames", "[combo_fsm]") {
    auto attacks = make_table(); auto clips = make_clips();
    auto chains  = make_chains(); auto tf = make_transform();
    CombatController combat; AnimationState anim; InputBuffer buf;

    buf.push(Kind::Light);
    const float dt = 1.f/60.f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);
    anim.time_seconds = 0.16f;
    combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt); // → Active

    // Trigger hitstop manually
    combat.hitstop_active       = true;
    combat.phase_before_hitstop = CombatPhase::Active;
    combat.hitstop_frames       = 3;
    anim.speed = 0.f;

    // 3 ticks — should stay frozen
    for (int i = 0; i < 3; ++i) {
        const float t_before = anim.time_seconds;
        combat_tick(combat, tf, anim, buf, attacks, clips, chains, dt);
        REQUIRE(combat.hitstop_active == (i < 2));  // false after last tick
        if (i < 2) REQUIRE(combat.phase == CombatPhase::Active); // phase unchanged during hitstop
    }
    REQUIRE(combat.hitstop_active == false);
    REQUIRE(combat.phase == CombatPhase::Active);
}
```

- [ ] **Step 6.2: Run tests — expect compile failure**

```
cmake --build build --config Debug --target character_tests 2>&1 | grep "error:"
```

Expected: `combat_tick` signature mismatch, `ChainTable` undefined.

- [ ] **Step 6.3: Update CombatController.hpp**

```cpp
// engine/character/core/CombatController.hpp
#pragma once

#include "engine/character/core/AttackData.hpp"
#include "engine/character/core/CharacterAsset.hpp"
#include "engine/character/core/CharacterComponents.hpp"
#include "engine/character/core/InputBuffer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine::movement {
struct Transform;
} // namespace engine::movement

namespace engine::character {

// Maps chain name → ordered list of attack IDs.
using ChainTable = std::unordered_map<std::string, std::vector<std::string>>;

// Fixed chain name constants — used by both combat_tick and MovementApp.
inline constexpr const char* kLightChain   = "light_chain";
inline constexpr const char* kHeavyChain   = "heavy_chain";
inline constexpr const char* kKickChain    = "kick_chain";
inline constexpr const char* kSpecialChain = "special_chain";

// Advance the combat FSM by dt seconds.
// buffer: input ring buffer (tick() already called by caller this frame).
// chains: maps chain name → combo attack id list.
// AnimationController::tick must be called BEFORE this function each sim step.
// combat_tick does NOT advance anim.time_seconds — that is AnimationController::tick's job.
void combat_tick(CombatController& combat,
                 engine::movement::Transform& transform,
                 AnimationState& anim,
                 InputBuffer& buffer,
                 const AttackTable& attacks,
                 const std::vector<AnimClip>& clips,
                 const ChainTable& chains,
                 float dt);

} // namespace engine::character
```

- [ ] **Step 6.4: Rewrite CombatController.cpp**

```cpp
// engine/character/core/CombatController.cpp
#include "engine/character/core/CombatController.hpp"
#include "engine/character/core/AnimationController.hpp"
#include "engine/movement/core/Components.hpp"

namespace engine::character {

namespace {

float clip_duration(const std::string& name, const std::vector<AnimClip>& clips) {
    for (const AnimClip& c : clips) {
        if (c.name == name) return c.duration_seconds;
    }
    return 1.f;
}

const AttackDef* current_def(const CombatController& combat, const AttackTable& attacks) {
    if (combat.combo_index < 0 ||
        combat.combo_index >= static_cast<int>(combat.combo_ids.size())) return nullptr;
    auto it = attacks.find(combat.combo_ids[static_cast<std::size_t>(combat.combo_index)]);
    return it != attacks.end() ? &it->second : nullptr;
}

float norm_time(const AnimationState& anim, const AttackDef* def,
                const std::vector<AnimClip>& clips) {
    if (!def) return 0.f;
    const float dur = clip_duration(def->clip, clips);
    return dur > 1e-5f ? anim.time_seconds / dur : 1.f;
}

std::string kind_to_chain(BufferedInput::Kind k) {
    switch (k) {
        case BufferedInput::Kind::Light:   return kLightChain;
        case BufferedInput::Kind::Heavy:   return kHeavyChain;
        case BufferedInput::Kind::Kick:    return kKickChain;
        case BufferedInput::Kind::Special: return kSpecialChain;
        default:                           return {};
    }
}

bool start_attack(CombatController& combat,
                  engine::movement::Transform& transform,
                  AnimationState& anim,
                  const AttackTable& attacks,
                  const std::vector<AnimClip>& clips,
                  int idx,
                  BufferedInput::Kind kind) {
    if (idx < 0 || idx >= static_cast<int>(combat.combo_ids.size())) return false;
    auto it = attacks.find(combat.combo_ids[static_cast<std::size_t>(idx)]);
    if (it == attacks.end()) return false;

    combat.combo_index = idx;
    combat.hit_consumed = false;
    combat.attack_yaw = transform.yaw;
    combat.active_kind = kind;
    combat.phase = CombatPhase::Startup;

    AnimationController::crossfade_to(anim, it->second.clip, 0.08f, false);
    return true;
}

void reset_to_idle(CombatController& combat, AnimationState& anim) {
    AnimationController::crossfade_to(anim, "Walk", 0.15f, true);
    combat.phase       = CombatPhase::Idle;
    combat.combo_index = 0;
    combat.active_kind = BufferedInput::Kind::None;
    combat.combo_ids.clear();
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
    (void)dt; // time advancement is AnimationController::tick's responsibility

    // Hitstop overlay: freeze FSM, drain counter
    if (combat.hitstop_active) {
        --combat.hitstop_frames;
        if (combat.hitstop_frames <= 0) {
            combat.hitstop_active = false;
            anim.speed = 1.f;
        }
        return;
    }

    const AttackDef* def  = current_def(combat, attacks);
    const float nt        = norm_time(anim, def, clips);

    switch (combat.phase) {

    case CombatPhase::Idle: {
        const auto kind = buffer.peek();
        if (kind == BufferedInput::Kind::None ||
            kind == BufferedInput::Kind::Dodge) break;
        buffer.consume();
        const std::string chain = kind_to_chain(kind);
        auto it = chains.find(chain);
        if (it == chains.end() || it->second.empty()) break;
        combat.combo_ids = it->second;
        start_attack(combat, transform, anim, attacks, clips, 0, kind);
        break;
    }

    case CombatPhase::Startup: {
        if (def && nt >= def->hit_start_norm) {
            combat.phase = CombatPhase::Active;
        }
        break;
    }

    case CombatPhase::Active: {
        if (def && nt > def->hit_end_norm) {
            combat.phase = CombatPhase::Recovery;
        }
        break;
    }

    case CombatPhase::Recovery: {
        const float clip_dur = def ? clip_duration(def->clip, clips) : 0.f;

        // Dodge cancel window
        if (def && nt >= def->dodge_cancel_start_norm) {
            if (buffer.peek() == BufferedInput::Kind::Dodge) {
                buffer.consume();
                combat.dodge_requested = true;
                reset_to_idle(combat, anim);
                combat.phase = CombatPhase::DodgeCancel;
                break;
            }
        }

        // Early combo cancel window: player pressed same-chain button
        const int next_idx = combat.combo_index + 1;
        const bool has_next = next_idx < static_cast<int>(combat.combo_ids.size());
        if (has_next && def && nt >= def->cancel_start_norm) {
            const auto kind = buffer.peek();
            if (kind == combat.active_kind) {
                buffer.consume();
                start_attack(combat, transform, anim, attacks, clips, next_idx, kind);
                break;
            }
        }

        // Clip ended: last-chance chain check or return to Idle
        if (clip_dur > 1e-5f && anim.time_seconds >= clip_dur) {
            if (has_next) {
                const auto kind = buffer.peek();
                if (kind == combat.active_kind) {
                    buffer.consume();
                    start_attack(combat, transform, anim, attacks, clips, next_idx, kind);
                } else {
                    reset_to_idle(combat, anim);
                }
            } else {
                reset_to_idle(combat, anim);
            }
        }
        break;
    }

    case CombatPhase::DodgeCancel: {
        // One-frame state: MovementApp reads combat.dodge_requested this frame
        combat.dodge_requested = false;
        combat.phase = CombatPhase::Idle;
        break;
    }

    } // switch
}

} // namespace engine::character
```

- [ ] **Step 6.5: Run all combo_fsm tests**

```
cmake --build build --config Debug --target character_tests
ctest --test-dir build -C Debug -R "combo_fsm" -V
```

Expected: all combo_fsm tests pass.

- [ ] **Step 6.6: Commit**

```
git add engine/character/core/CombatController.hpp engine/character/core/CombatController.cpp
git add tests/character/test_combo_fsm.cpp
git commit -m "feat: CombatController FSM rewrite — Startup/Active/Recovery, InputBuffer, hitstop overlay, chains"
```

---

## Task 7: HitReactSystem — hitstop trigger + ScreenShake struct

**Files:**
- Modify: `engine/character/core/HitReactSystem.hpp`
- Modify: `engine/character/core/HitReactSystem.cpp`

- [ ] **Step 7.1: Update HitReactSystem.hpp**

```cpp
// engine/character/core/HitReactSystem.hpp
#pragma once

#include "engine/character/core/CharacterComponents.hpp"
#include "engine/movement/core/Components.hpp"

namespace engine::character {

struct ScreenShake {
    float magnitude = 0.f;
    float duration  = 0.f;
    float timer     = 0.f;
};

void hit_react_tick(HitReact& react,
                    engine::movement::Transform& transform,
                    float dt,
                    bool hitstop_active = false);

// Trigger hit reaction. Also sets attacker hitstop and screenshake.
// attacker_combat: may be null (e.g., no attacker hitstop needed).
// shake: may be null.
void trigger_hit_react(HitReact& react,
                       engine::movement::Transform& transform,
                       const glm::vec3& direction,
                       AnimationState& anim,
                       CombatController* attacker_combat = nullptr,
                       ScreenShake* shake = nullptr);

void apply_screenshake(ScreenShake& shake, float magnitude, float duration);
void tick_screenshake(ScreenShake& shake, float dt);
// Returns a camera position offset for the current frame.
[[nodiscard]] glm::vec3 screenshake_offset(const ScreenShake& shake);

} // namespace engine::character
```

- [ ] **Step 7.2: Update HitReactSystem.cpp**

```cpp
// engine/character/core/HitReactSystem.cpp
#include "engine/character/core/HitReactSystem.hpp"

#include <algorithm>
#include <cmath>

namespace engine::character {

void hit_react_tick(HitReact& react,
                    engine::movement::Transform& transform,
                    float dt,
                    bool hitstop_active) {
    if (!react.playing_hit || react.timer <= 0.f) {
        react.playing_hit = false;
        return;
    }
    if (hitstop_active) return; // knockback paused during hitstop

    const float t     = react.timer / react.knockback_duration;
    const float speed = t;
    const float move  = glm::length(react.knockback_delta) * speed * dt /
                        (react.knockback_duration > 1e-5f ? react.knockback_duration : 1.f);

    if (glm::length(react.knockback_delta) > 1e-5f) {
        transform.position += glm::normalize(react.knockback_delta) * move;
    }

    react.timer -= dt;
    if (react.timer <= 0.f) {
        react.timer       = 0.f;
        react.playing_hit = false;
    }
}

void trigger_hit_react(HitReact& react,
                       engine::movement::Transform& /*transform*/,
                       const glm::vec3& direction,
                       AnimationState& anim,
                       CombatController* attacker_combat,
                       ScreenShake* shake) {
    react.playing_hit     = true;
    react.timer           = react.knockback_duration;
    react.knockback_delta = glm::vec3(direction.x, 0.f, direction.z) *
                            react.knockback_distance;

    anim.active_clip  = react.hit_clip;
    anim.time_seconds = 0.f;
    anim.looping      = false;
    anim.speed        = 1.f;

    // Trigger hitstop on attacker
    if (attacker_combat) {
        attacker_combat->hitstop_active       = true;
        attacker_combat->phase_before_hitstop = attacker_combat->phase;
        attacker_combat->hitstop_frames       = 5;
        // anim.speed for attacker set to 0 by combat_tick next frame
        // (speed is on the attacker's anim, not available here — MovementApp handles it)
    }

    if (shake) {
        apply_screenshake(*shake, 0.04f, 0.18f);
    }
}

void apply_screenshake(ScreenShake& shake, float magnitude, float duration) {
    shake.magnitude = magnitude;
    shake.duration  = duration;
    shake.timer     = duration;
}

void tick_screenshake(ScreenShake& shake, float dt) {
    if (shake.timer <= 0.f) return;
    shake.timer -= dt;
    if (shake.timer < 0.f) shake.timer = 0.f;
}

glm::vec3 screenshake_offset(const ScreenShake& shake) {
    if (shake.timer <= 0.f || shake.duration <= 1e-5f) return glm::vec3(0.f);
    const float decay = shake.timer / shake.duration;
    const float freq  = 40.f;
    const float ox    = std::sin(shake.timer * freq * 1.3f) * shake.magnitude * decay;
    const float oy    = std::sin(shake.timer * freq * 0.9f) * shake.magnitude * decay * 0.5f;
    return glm::vec3(ox, oy, 0.f);
}

} // namespace engine::character
```

- [ ] **Step 7.3: Note on attacker anim.speed**

`trigger_hit_react` sets `attacker_combat->hitstop_frames = 5` but cannot set `attacker_anim.speed = 0` because it doesn't have a reference to the attacker's `AnimationState`. MovementApp must do this immediately after calling `trigger_hit_react`:

```cpp
// In MovementApp, after trigger_hit_react call:
if (hit) {
    player_anim_.speed = 0.f; // freeze attacker animation for hitstop duration
}
```

This is intentional: `HitReactSystem` is about the target reacting, not about the attacker state.

- [ ] **Step 7.4: Build and run full character test suite**

```
cmake --build build --config Debug --target character_tests
ctest --test-dir build -C Debug -R "character_tests" -V
```

Expected: all tests pass.

- [ ] **Step 7.5: Commit**

```
git add engine/character/core/HitReactSystem.hpp engine/character/core/HitReactSystem.cpp
git commit -m "feat: HitReactSystem hitstop trigger + ScreenShake"
```

---

## Task 8: CharacterCatalog — load all available combat clips

**Files:**
- Modify: `engine/character/core/CharacterCatalog.cpp`

- [ ] **Step 8.1: Update load_player_set to load all 8 confirmed combat clips**

Replace the `clips[]` array in `load_player_set`:

```cpp
const std::pair<std::string, std::string> clips[] = {
    {"Animation_Walking_withSkin.glb",          "Walk"},
    {"Animation_Running_withSkin.glb",           "Run"},
    {"Animation_High_Kick_withSkin.glb",         "High_Kick"},
    {"Animation_Elbow_Strike_withSkin.glb",      "Elbow_Strike"},
    {"Animation_Counterstrike_withSkin.glb",     "Counterstrike"},
    {"Animation_Spartan_Kick_withSkin.glb",      "Spartan_Kick"},
    {"Animation_Sweeping_Kick_withSkin.glb",     "Sweeping_Kick"},
    {"Animation_Lunge_Spin_Kick_withSkin.glb",   "Lunge_Spin_Kick"},
    {"Animation_Dodge_and_Counter_withSkin.glb", "Dodge_and_Counter"},
    {"Animation_Shield_Push_Left_withSkin.glb",  "Shield_Push_Left"},
};
```

- [ ] **Step 8.2: Verify all clip files exist before building**

```
ls /c/Users/chris/Documents/Engine/assets/Meshy_AI_Voxel_Dungeon_Explore_biped/ | grep -E "(Spartan|Sweeping|Lunge|Dodge|Shield)"
```

Expected: all 5 new files present.

- [ ] **Step 8.3: Build and run character catalog test**

```
cmake --build build --config Debug --target character_tests
ctest --test-dir build -C Debug -R "character_catalog" -V
```

Expected: player set loaded with 10 clips.

- [ ] **Step 8.4: Commit**

```
git add engine/character/core/CharacterCatalog.cpp
git commit -m "feat: CharacterCatalog loads 10 player combat clips"
```

---

## Task 9: MovementApp — new data flow, 4 chains, screenshake, blend lookup

**Files:**
- Modify: `movement/MovementApp.hpp`
- Modify: `movement/MovementApp.cpp`

- [ ] **Step 9.1: Update MovementApp.hpp**

Add new private fields. Find the existing private section and add/replace:

```cpp
// Add these private fields (remove old attack_down_/attack_latch_ if present):
bool attack_light_down_   = false; bool attack_light_latch_   = false;
bool attack_heavy_down_   = false; bool attack_heavy_latch_   = false;
bool attack_kick_down_    = false; bool attack_kick_latch_    = false;
bool attack_special_down_ = false; bool attack_special_latch_ = false;

engine::character::InputBuffer  player_input_buffer_;
engine::character::ChainTable   player_chains_;
engine::character::ScreenShake  camera_shake_;
```

- [ ] **Step 9.2: Initialize player_chains_ in startup()**

After the line that sets up `player_combat_.combo_ids`, replace that setup with:

```cpp
// Setup 4 combat chains (replaces the old single combo_ids assignment)
player_chains_[engine::character::kLightChain]   = {"elbow_strike", "counterstrike"};
player_chains_[engine::character::kHeavyChain]   = {"spartan_kick", "dodge_and_counter"};
player_chains_[engine::character::kKickChain]    = {"high_kick", "sweeping_kick", "lunge_spin_kick"};
player_chains_[engine::character::kSpecialChain] = {"shield_push"};
```

Make sure `attack_table_` has entries matching these IDs (they map to the clips defined in Task 4).

- [ ] **Step 9.3: Update the sim step data flow in run()**

Replace the entire sim step lambda body with the new order. Find `sim_clock_.step([&] {` and replace its contents:

```cpp
sim_clock_.step([&] {
    tf->sync_previous();

    // 1. Tick input buffer (decrement TTLs, expire stale inputs)
    player_input_buffer_.tick();

    // 2. Push new rising-edge inputs into buffer
    if (input.attack_light)   player_input_buffer_.push(engine::character::BufferedInput::Kind::Light);
    if (input.attack_heavy)   player_input_buffer_.push(engine::character::BufferedInput::Kind::Heavy);
    if (input.attack_kick)    player_input_buffer_.push(engine::character::BufferedInput::Kind::Kick);
    if (input.attack_special) player_input_buffer_.push(engine::character::BufferedInput::Kind::Special);
    if (input.dodge_pressed &&
        player_combat_.phase != engine::character::CombatPhase::Idle)
        player_input_buffer_.push(engine::character::BufferedInput::Kind::Dodge);

    // Clear latches that were consumed by the buffer push
    if (input.attack_light)   { attack_light_latch_   = false; input.attack_light   = false; }
    if (input.attack_heavy)   { attack_heavy_latch_   = false; input.attack_heavy   = false; }
    if (input.attack_kick)    { attack_kick_latch_    = false; input.attack_kick    = false; }
    if (input.attack_special) { attack_special_latch_ = false; input.attack_special = false; }

    const bool is_combat_locked =
        player_combat_.phase != engine::character::CombatPhase::Idle;

    // 3. Advance animation time (SOLE owner of anim.time_seconds)
    if (player_char_handle_ >= 0) {
        const engine::character::AnimClip* clip = nullptr;
        for (const auto& c : player_asset_.clips)
            if (c.name == player_anim_.active_clip) { clip = &c; break; }
        engine::character::AnimationController::tick(
            player_anim_, clip, static_cast<float>(SimClock::fixed_dt));
    }

    // 4. Locomotion clip selection (only when Idle)
    if (player_char_handle_ >= 0 &&
        player_combat_.phase == engine::character::CombatPhase::Idle) {
        const float horiz_speed = glm::length(glm::vec2(pc->velocity.x, pc->velocity.z));
        const std::string desired =
            engine::character::AnimationController::select_locomotion(horiz_speed, pc->grounded);
        if (desired != player_anim_.active_clip) {
            engine::character::AnimationController::crossfade_to(
                player_anim_, desired, 0.1f, true);
        }
    }

    // 5. Combat FSM tick (reads current norm_time from already-advanced anim)
    if (player_char_handle_ >= 0 && !attack_table_.empty()) {
        engine::character::combat_tick(
            player_combat_, *tf, player_anim_,
            player_input_buffer_, attack_table_, player_asset_.clips,
            player_chains_, static_cast<float>(SimClock::fixed_dt));
    }

    // 6. Movement (frozen if in combat)
    InputSnapshot move_input = input;
    if (is_combat_locked) {
        move_input.move_forward = false;
        move_input.move_back    = false;
        move_input.move_left    = false;
        move_input.move_right   = false;
        move_input.jump_pressed = false;
    }
    debug_ = player_tick(*tf, *pc, player_capsule_, collision_, move_input,
                         rig->yaw, static_cast<float>(SimClock::fixed_dt), tuning_);

    // 7. Hit detection: uses the same norm_time that combat_tick just used
    if (player_combat_.phase == engine::character::CombatPhase::Active &&
        dummy_char_handle_ >= 0 && world_.is_alive(dummy_entity_) &&
        !attack_table_.empty()) {
        const int ci = player_combat_.combo_index;
        if (ci >= 0 && ci < static_cast<int>(player_combat_.combo_ids.size())) {
            const std::string& atk_id = player_combat_.combo_ids[ci];
            auto ait = attack_table_.find(atk_id);
            if (ait != attack_table_.end()) {
                engine::movement::Transform* dummy_tf  = world_.transforms().get(dummy_entity_);
                engine::movement::Collider*  dummy_col = world_.colliders().get(dummy_entity_);
                if (dummy_tf && dummy_col) {
                    // Get clip duration for norm_t calculation
                    float clip_dur = 1.f;
                    for (const auto& c : player_asset_.clips)
                        if (c.name == ait->second.clip) { clip_dur = c.duration_seconds; break; }

                    const bool hit = engine::character::try_hit_in_window(
                        player_combat_, player_anim_, ait->second, clip_dur,
                        *tf, *dummy_tf, *dummy_col);
                    if (hit) {
                        const glm::vec3 dir = dummy_tf->position - tf->position;
                        const glm::vec3 dir_norm =
                            glm::length(dir) > 1e-5f ? glm::normalize(dir)
                                                     : glm::vec3(0.f, 0.f, 1.f);
                        // Set attacker anim.speed = 0 for hitstop duration
                        player_anim_.speed = 0.f;
                        engine::character::trigger_hit_react(
                            dummy_react_, *dummy_tf, dir_norm, dummy_anim_,
                            &player_combat_, &camera_shake_);
                        SPDLOG_INFO("Hit! chain[{}] combo[{}]",
                                    static_cast<int>(player_combat_.active_kind), ci);
                    }
                }
            }
        }
    }

    // 8. HitReact tick on dummy (knockback paused during hitstop)
    if (dummy_char_handle_ >= 0 && world_.is_alive(dummy_entity_)) {
        engine::movement::Transform* dummy_tf = world_.transforms().get(dummy_entity_);
        if (dummy_tf) {
            engine::character::hit_react_tick(
                dummy_react_, *dummy_tf,
                static_cast<float>(SimClock::fixed_dt),
                player_combat_.hitstop_active);

            // Advance dummy animation time
            const engine::character::AnimClip* dclip = nullptr;
            for (const auto& c : dummy_asset_.clips)
                if (c.name == dummy_anim_.active_clip) { dclip = &c; break; }
            engine::character::AnimationController::tick(
                dummy_anim_, dclip, static_cast<float>(SimClock::fixed_dt));

            if (!dummy_anim_.looping && dclip &&
                dummy_anim_.time_seconds >= dclip->duration_seconds) {
                dummy_anim_.time_seconds = dclip->duration_seconds;
                dummy_anim_.speed = 0.f;
            }
        }
    }

    ++sim_steps_last_frame_;
});
```

- [ ] **Step 9.4: Tick screenshake per display frame and apply to view**

After the sim loop, add screenshake tick and application to view matrix. Find the line `snap.view = camera::view_matrix(...)` and update:

```cpp
// Tick screenshake (display-rate, before view matrix)
engine::character::tick_screenshake(camera_shake_, static_cast<float>(frame_dt));
const glm::vec3 shake_offset = engine::character::screenshake_offset(camera_shake_);

snap.view = camera::view_matrix(*rig, render_pos + shake_offset);
```

- [ ] **Step 9.5: Update sample_bone_matrices calls to pass blend_clip**

In the player bone matrix sampling block, find the clip and blend_clip:

```cpp
// Player bone matrices with crossfade blend
if (player_char_handle_ >= 0) {
    const engine::character::AnimClip* clip       = nullptr;
    const engine::character::AnimClip* blend_clip = nullptr;
    for (const auto& c : player_asset_.clips) {
        if (c.name == player_anim_.active_clip) clip       = &c;
        if (c.name == player_anim_.blend_clip)  blend_clip = &c;
    }
    const std::vector<glm::mat4> bone_mats =
        engine::character::AnimationController::sample_bone_matrices(
            player_anim_, clip, player_asset_.mesh, blend_clip);
    // ... rest of model matrix setup unchanged
}
```

Do the same for the dummy character.

- [ ] **Step 9.6: Update overlay state for new CombatPhase values**

Find the combat_phase overlay string:

```cpp
using CP = engine::character::CombatPhase;
overlay_state.combat_phase =
    player_combat_.phase == CP::Startup     ? "Startup"     :
    player_combat_.phase == CP::Active      ? "Active"      :
    player_combat_.phase == CP::Recovery    ? "Recovery"    :
    player_combat_.phase == CP::DodgeCancel ? "DodgeCancel" : "Idle";
overlay_state.active_clip  = player_anim_.active_clip.c_str();
overlay_state.combo_index  = player_combat_.combo_index;
```

- [ ] **Step 9.7: Build the movement executable**

```
cmake --build build --config Debug --target movement 2>&1 | grep "error:"
```

Expected: clean build.

- [ ] **Step 9.8: Commit**

```
git add movement/MovementApp.cpp movement/MovementApp.hpp
git commit -m "feat: MovementApp — new data flow, 4 chains, screenshake, TRS blend"
```

---

## Task 10: Run all tests + full build verification

- [ ] **Step 10.1: Build all targets**

```
cmake --build build --config Debug
```

Expected: zero errors.

- [ ] **Step 10.2: Run all test suites**

```
ctest --test-dir build -C Debug -V 2>&1 | tail -30
```

Expected: all tests pass. Note any failures and fix them before proceeding.

- [ ] **Step 10.3: Verify attack_table loads clean**

The `combat_attacks.txt` now has 8 entries. Verify via the log output when running the app:

```
cmake --build build --config Debug --target movement
# Run movement.exe, check spdlog output for:
# MovementApp: loaded 8 attack definitions
```

- [ ] **Step 10.4: Smoke test the combat feel**

Launch the app and verify:
1. LMB starts elbow_strike → can press LMB again during recovery to chain to counterstrike
2. LMB without second press → combo ends and returns to Walk
3. Q starts high_kick → chain through sweeping_kick → lunge_spin_kick (3-hit)
4. Getting hit: training dummy shows knockback + brief camera shake
5. Attack animations have smooth crossfades (no hard pop to Walk)
6. No animation running at 2x speed (the double-advance bug is gone)

- [ ] **Step 10.5: Final commit**

```
git add -A
git commit -m "feat: combat redesign complete — input-gated combos, crossfade, hitstop, screenshake"
```

---

## Self-Review Against Spec

| Spec Requirement | Task |
|---|---|
| Fix double-advance bug | Task 5 (AnimationController::tick sole owner), Task 6 (combat_tick no longer advances time) |
| InputBuffer 10-frame TTL | Task 2 |
| Input-gated combo (press per hit) | Task 6 — Recovery checks buffer for continuation |
| Startup/Active/Recovery/DodgeCancel phases | Task 3, Task 6 |
| Hitstop overlay (bool + phase_before + frames) | Task 3, Task 7 |
| TRS-level crossfade (not matrix mix) | Task 5 |
| `_norm`/`_seconds` field naming | Task 4 |
| `cancel_start_norm` ≥ `hit_end_norm` assertion | Task 4 |
| 4 attack buttons (LMB/RMB/Q/E) | Task 1 |
| 4 combo chains | Task 6 (ChainTable), Task 9 |
| Screenshake | Task 7 (struct + logic), Task 9 (wiring) |
| Dodge-cancel window | Task 6 (DodgeCancel state) |
| Variant pool deferred | Not implemented (correct per spec) |
| Weapon slots deferred | Not implemented (correct per spec) |
| Data flow order: tick→combat→hit | Task 9 |
| `blend_clip` blend_weight decay in tick | Task 5 |
| Parser invariant assertions | Task 4 |
