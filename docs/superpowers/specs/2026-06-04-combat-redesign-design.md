# Combat System Redesign — Design Spec
**Date:** 2026-06-04  
**Status:** Approved for implementation

---

## Problem Statement

The existing combat system has several critical issues that make it feel unresponsive and janky:

1. **Double-advance bug** — `combat_tick` advances `anim.time_seconds` internally during `Attacking`, then `MovementApp` calls `AnimationController::tick` unconditionally, advancing it again. Attack animations play at 2× speed and hit windows trigger too early.
2. **Hard animation cuts** — All clip transitions (Idle→Attack, Attack→Attack, Attack→Recovery) are instant hard cuts at `time_seconds = 0`. No crossfading.
3. **No input buffer** — `attack_pressed` is a 1-frame edge latch. Inputs just outside the exact frame window are lost.
4. **Auto-chained combo** — The full 3-hit combo runs automatically on a single press with no player timing agency.
5. **No hitstop** — Hits lack the brief freeze that communicates impact.
6. **No screenshake** — No camera feedback on hit.
7. **No cancel windows** — Player is fully committed for the entire attack + recovery; no dodge escape.
8. **Only 3 of 38 animations used** — `assets/Fight` contains 38 GLB files; only `high_kick`, `elbow_strike`, `counterstrike` are mapped.

---

## Approach

**Option B — Clean FSM Expansion.** New states added to `CombatPhase`; `InputBuffer` struct replaces the 1-frame latch; `AnimationState` gets a blend slot for crossfades; `HitReactSystem` gets hitstop; `CameraSystem` gets a screenshake channel. All changes are in clearly separated, focused files. Existing tests remain compilable.

---

## Architecture

### Files Changed

| File | Change |
|---|---|
| `engine/character/core/CharacterComponents.hpp` | New `CombatPhase` states; `AnimationState` blend fields; `CombatController` cancel/hitstop fields |
| `engine/character/core/CombatController.cpp/.hpp` | Full rewrite of FSM logic; multi-chain support |
| `engine/character/core/AnimationController.cpp/.hpp` | `sample_bone_matrices` blends two clips; new `crossfade_to` helper |
| `engine/character/core/HitReactSystem.cpp/.hpp` | Hitstop trigger added |
| `engine/character/core/AttackData.hpp` | `AttackDef` gets `cancel_start`, `dodge_cancel_start` |
| `engine/movement/core/InputSnapshot.hpp` | `attack_pressed` replaced by `attack_light`, `attack_heavy`, `attack_kick`, `attack_special` |
| `movement/MovementApp.cpp/.hpp` | Screenshake forwarded to camera; new button polling (LMB/RMB/Q/E); combo chains wired up |
| `assets/character/combat_attacks.txt` | Expanded to ~20 attack definitions with cancel windows |

### Files Added

| File | Content |
|---|---|
| `engine/character/core/InputBuffer.hpp/.cpp` | 10-frame ring buffer for buffered inputs |

---

## FSM States

```
Idle → Startup → Active → Recovery → Idle
                   │
            (hit lands)
                   ↓
               Hitstun  ──→ (back to Active or Recovery)
                   
Recovery (past dodge_cancel_start) + Space → DodgeCancel → Idle
```

| State | Description | Exit condition |
|---|---|---|
| `Idle` | No action | Buffer contains attack → `Startup` |
| `Startup` | Pre-hit frames (animation wind-up) | Normalized clip time ≥ `hit_start` → `Active` |
| `Active` | Hit window open; damage possible | Normalized clip time > `hit_end` → `Recovery` |
| `Recovery` | End-lag; character committed | `clip_remaining ≤ 0` AND buffer has attack → next combo step; else → `Idle` |
| `Hitstun` | Attacker freezes for N frames on successful hit | `hitstop_frames` countdown reaches 0 → previous state |
| `DodgeCancel` | Early escape from `Recovery` via Space | 1 frame → `Idle` + dodge impulse applied |

**Key rule:** The combo does NOT auto-chain. Each step requires a fresh button press buffered during the previous `Active` or early `Recovery` window (`cancel_start` normalized time). No press in buffer → combo ends at `Recovery` → `Idle`.

---

## InputBuffer

**File:** `engine/character/core/InputBuffer.hpp/.cpp`

```cpp
struct BufferedInput {
    enum class Kind { None, Light, Heavy, Kick, Special };
    Kind kind      = Kind::None;
    int  ttl_frames = 0;   // decremented each sim step; 0 = expired
};

class InputBuffer {
public:
    static constexpr int kCapacity   = 10;
    static constexpr int kDefaultTTL = 10;  // ~167 ms at 60 Hz

    void push(BufferedInput::Kind kind, int ttl = kDefaultTTL);
    [[nodiscard]] BufferedInput::Kind peek() const;  // oldest valid entry
    void consume();                                   // remove oldest entry
    void tick();                                      // decrement all TTLs, remove expired
    void clear();
};
```

`CombatController` calls `buffer.tick()` each sim step, reads `buffer.peek()` to decide whether to start/continue a combo, and calls `buffer.consume()` when an input is used.

`MovementApp::poll_input` still produces `InputSnapshot` (now with four attack booleans). At the start of each sim step, rising-edge transitions are pushed into `InputBuffer`.

---

## Animation Crossfade

`AnimationState` gains a blend slot:

```cpp
struct AnimationState {
    std::string active_clip;
    float       time_seconds  = 0.f;
    float       speed         = 1.f;
    bool        looping       = true;

    std::string blend_clip;           // previous clip fading out
    float       blend_time    = 0.f;  // current playback position in blend_clip
    float       blend_weight  = 0.f;  // 1.0 = fully blend_clip, fades to 0
    float       blend_duration = 0.12f;
};
```

**`AnimationController::crossfade_to`** — called instead of directly writing `anim.active_clip`:
- Moves `active_clip` / `time_seconds` → `blend_clip` / `blend_time`
- Sets new clip as `active_clip`, `time_seconds = 0`
- Sets `blend_weight = 1.0`

**`AnimationController::tick`** — decrements `blend_weight` by `dt / blend_duration`; clamps to 0.

**`AnimationController::sample_bone_matrices`** — when `blend_weight > 0`, samples both clips and `glm::mix`-interpolates the resulting bone matrices per bone.

**Bug fix:** `combat_tick` no longer advances `anim.time_seconds`. The single call to `AnimationController::tick` in `MovementApp` is the sole owner of time advancement. During `Hitstun`, `anim.speed = 0` is set so `tick` advances nothing.

---

## Attack Table Extensions

`AttackDef` gains two new fields:

```cpp
struct AttackDef {
    std::string id;
    std::string clip;
    float hit_start          = 0.f;  // normalized clip time, hit window open
    float hit_end            = 0.f;  // normalized clip time, hit window close
    float range              = 0.f;
    float radius             = 0.f;
    float recovery           = 0.f;
    float cancel_start       = 0.7f; // normalized time: input buffer accepted from here
    float dodge_cancel_start = 0.6f; // normalized time: Space cancels into dodge from here
};
```

---

## Multi-Chain Attack Mapping

Four combo chains accessed via four buttons. `CombatController` stores `active_chain` (set on first press) and selects the corresponding `combo_ids` list.

| Button | Chain name | Hit 1 | Hit 2 | Hit 3 |
|---|---|---|---|---|
| **LMB** | `quick_strikes` | `Left_Jab_from_Guard` | `Right_Jab_from_Guard` | `Elbow_Strike` |
| **RMB** | `power_strikes` | `Left_Hook_from_Guard` | `Right_Uppercut_from_Guard` | `Counterstrike` |
| **Q** | `kick_chain` | `High_Kick` | `Roundhouse_Kick` | `Spartan_Kick` |
| **E** | `sweep_chain` | `Leg_Sweep` | `Lunge_Spin_Kick` | — |

**Contextual attacks** (triggered by modifier + button):

| Condition | Animation |
|---|---|
| Sprint + LMB | `Lunge_Roundhouse_Kick` |
| Sprint + Q | `Step_in_High_Kick` |
| Dodge-cancel window + LMB | `Dodge_and_Counter` |
| LMB + RMB simultaneously | `Punch_Forward_with_Both_Fists` |
| Hold LMB ≥ 0.4 s | `Charged_Upward_Slash` |

**Variant pool** (random substitution within a chain for variety):  
`Left_Short_Hook_from_Guard`, `Right_Upper_Hook_from_Guard`, `Left_Uppercut_from_Guard`, `Punch_Combo_1–5`, `Kung_Fu_Punch`, `Flying_Fist_Kick`, `Sweeping_Kick`, `Boxing_Guard_Prep_Straight_Punch`, `Boxing_Guard_Right_Straight_Kick`, `Boxing_Guard_Step_Knee_Strike`, `Lunge_Roundhouse_Kick`

**Weapon slots** (deferred, assets already present):  
`Right_Hand_Sword_Slash`, `Weapon_Combo_1`, `Weapon_Combo`, `Shield_Push_Left`

Total: ~34 of 38 animations mapped immediately; 4 are exact duplicates (`(1)` suffix files).

---

## Hitstop

On a successful hit:
1. `player_combat_.hitstop_frames = 5` is set.
2. FSM enters `Hitstun` sub-state (stored as `hitstop_active = true`, no new `CombatPhase` value needed — it overlays the current phase).
3. While `hitstop_active`: `anim.speed = 0` for both attacker and target; `clip_remaining` countdown paused.
4. Each sim step decrements `hitstop_frames`; when it reaches 0, `hitstop_active = false`, `anim.speed = 1`.
5. The target's `HitReact` knockback begins only *after* hitstop ends.

---

## Screenshake

Minimal addition to `CameraSystem`:

```cpp
struct ScreenShake {
    float magnitude = 0.f;
    float duration  = 0.f;
    float timer     = 0.f;
};
```

`trigger_hit_react` calls `apply_screenshake(shake, 0.04f, 0.18f)`.

In `camera::view_matrix`: when `shake.timer > 0`, add a small random offset to the eye position (`sin(timer * freq) * magnitude * decay`). `shake.timer` decremented each frame in `MovementApp`.

---

## Data Flow (per sim step)

```
poll_input()
    → InputSnapshot (4 attack bools, movement, sprint, space)
    → push rising edges into InputBuffer

sim step:
    InputBuffer::tick()          // decrement TTLs
    combat_tick()                // reads buffer, drives FSM, sets anim clip via crossfade_to
    player_tick()                // movement (frozen if not Idle)
    AnimationController::tick()  // SOLE owner of time advancement; respects anim.speed
    try_hit_in_window()          // hit detection
    → on hit: trigger_hit_react(), apply_screenshake(), set hitstop_frames
    hit_react_tick()             // knockback (skipped during hitstop)
    AnimationController::tick() for dummy
```

---

## Testing

Existing tests in `tests/character/test_combo_fsm.cpp` remain valid after renaming `attack_pressed` → `attack_light` in fixtures. New test cases to add:

- Buffer input during `Active` phase → next combo step fires on `Recovery` transition
- No buffer input → combo ends at `Idle` after `Recovery`
- Dodge-cancel fires when Space pressed past `dodge_cancel_start`
- Hitstop: `anim.speed == 0` for N frames after hit, then restores
- Crossfade: `blend_weight` decays from 1.0 to 0.0 over `blend_duration`
