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

`CombatPhase` has four values: `Idle`, `Startup`, `Active`, `Recovery`, `DodgeCancel`.  
Hitstop is **not** a `CombatPhase` value — it overlays the current phase via three fields on `CombatController`:

```cpp
bool        hitstop_active       = false;
CombatPhase phase_before_hitstop = CombatPhase::Idle;
int         hitstop_frames       = 0;
```

When hitstop triggers: `phase_before_hitstop = phase; hitstop_active = true; hitstop_frames = 5`.  
Each sim step: if `hitstop_active`, skip all FSM transitions and `clip_remaining` countdown, decrement `hitstop_frames`. When `hitstop_frames == 0`: `hitstop_active = false`, resume in `phase_before_hitstop`.

```
Idle → Startup → Active → Recovery → Idle
                              │
             (past dodge_cancel_start_norm + Space)
                              ↓
                         DodgeCancel → Idle

    ── hit lands anywhere in Active ──►  hitstop overlay (no phase change)
```

| State | Description | Exit condition |
|---|---|---|
| `Idle` | No action | Buffer contains attack → `Startup` |
| `Startup` | Pre-hit frames (animation wind-up) | `norm_time ≥ hit_start_norm` → `Active` |
| `Active` | Hit window open; damage possible | `norm_time > hit_end_norm` → `Recovery` |
| `Recovery` | End-lag; character committed | `clip_remaining ≤ 0` AND buffer has same-chain attack → next combo step; else → `Idle` |
| `DodgeCancel` | Early escape from `Recovery` | 1 frame → `Idle` + dodge impulse applied |

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

**`AnimationController::sample_bone_matrices`** — when `blend_weight > 0`, samples TRS channels from both clips separately, then blends at the pose level before building matrices:
- Translation: `glm::mix(trans_a, trans_b, blend_weight)`
- Rotation: `glm::slerp(rot_a, rot_b, blend_weight)`
- Scale: `glm::mix(scale_a, scale_b, blend_weight)`
- Then builds the bone matrix via `trs_matrix(t, r, s)` as usual.

This avoids the visual artifacts (shearing, incorrect interpolation) that result from `glm::mix`-ing final 4×4 matrices directly, which is only correct for pure translation and breaks for rotation and scale.

**Bug fix:** `combat_tick` no longer advances `anim.time_seconds`. The single call to `AnimationController::tick` in `MovementApp` is the sole owner of time advancement. During `Hitstun`, `anim.speed = 0` is set so `tick` advances nothing.

---

## Attack Table Extensions

All normalized fields use the `_norm` suffix (range [0, 1] relative to clip duration). The `recovery_seconds` field is in wall-clock seconds. This naming is enforced in both the struct and the `.txt` parser to prevent unit confusion at the implementation stage.

```cpp
struct AttackDef {
    std::string id;
    std::string clip;
    float hit_start_norm          = 0.f;   // normalized [0,1]: hit window opens
    float hit_end_norm            = 0.f;   // normalized [0,1]: hit window closes
    float range                   = 0.f;   // metres, hit capsule forward offset
    float radius                  = 0.f;   // metres, hit capsule radius
    float recovery_seconds        = 0.2f;  // seconds of end-lag after last combo hit
    float cancel_start_norm       = 0.7f;  // normalized [0,1]: buffer accepts next input from here
    float dodge_cancel_start_norm = 0.6f;  // normalized [0,1]: Space cancels into dodge from here
};
```

`cancel_start_norm` must be ≥ `hit_end_norm`. `dodge_cancel_start_norm` must be ≤ `cancel_start_norm`. The parser asserts these invariants on load.

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

**Variant pool** (deferred — stabilise fixed chains first):  
`Left_Short_Hook_from_Guard`, `Right_Upper_Hook_from_Guard`, `Left_Uppercut_from_Guard`, `Punch_Combo_1–5`, `Kung_Fu_Punch`, `Flying_Fist_Kick`, `Sweeping_Kick`, `Boxing_Guard_Prep_Straight_Punch`, `Boxing_Guard_Right_Straight_Kick`, `Boxing_Guard_Step_Knee_Strike`, `Lunge_Roundhouse_Kick`.  
Random substitution within a chain is deferred. If added later, variant selection must use a seeded deterministic RNG and must not affect gameplay properties (hit windows, range, damage) — visual variation only.

**Weapon slots** (deferred, assets already present):  
`Right_Hand_Sword_Slash`, `Weapon_Combo_1`, `Weapon_Combo`, `Shield_Push_Left`

Total: ~34 of 38 animations mapped immediately; 4 are exact duplicates (`(1)` suffix files).

---

## Hitstop

Hitstop is an overlay, not a `CombatPhase`. `CombatController` carries:

```cpp
bool        hitstop_active       = false;
CombatPhase phase_before_hitstop = CombatPhase::Idle;
int         hitstop_frames       = 0;
```

On a successful hit:
1. `phase_before_hitstop = combat.phase; hitstop_active = true; hitstop_frames = 5`.
2. `anim.speed = 0` for both attacker and target (time advancement stops because `AnimationController::tick` multiplies by `speed`).
3. `clip_remaining` countdown is paused (not decremented while `hitstop_active`).
4. Each sim step while `hitstop_active`: decrement `hitstop_frames`. When `hitstop_frames == 0`: `hitstop_active = false; combat.phase = phase_before_hitstop; anim.speed = 1`.
5. The target's `HitReact` knockback begins only after hitstop ends (`hit_react_tick` checks `!hitstop_active` on the target).

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

The ordering rule is: **advance time first, then read normalized time for FSM and hit detection within the same step.** This ensures `combat_tick` and `try_hit_in_window` always agree on the current clip position and never drift by one frame.

```
poll_input()
    → InputSnapshot (4 attack bools, movement, sprint, space)
    → push rising edges into InputBuffer

sim step:
    1. InputBuffer::tick()           // decrement TTLs, expire stale inputs
    2. AnimationController::tick()   // SOLE owner of time advancement; respects anim.speed=0 during hitstop
    3. combat_tick()                 // reads current norm_time, drives FSM transitions, crossfade_to on clip change
    4. player_tick()                 // movement (input frozen if phase != Idle)
    5. try_hit_in_window()           // hit detection — uses same norm_time as step 3
       → on hit: set hitstop_active, trigger_hit_react(), apply_screenshake()
    6. hit_react_tick()              // knockback translation (skipped if hitstop_active)
    7. AnimationController::tick() for dummy
```

`combat_tick` must not touch `anim.time_seconds` or `anim.speed` directly except for setting `speed = 0` when activating hitstop and restoring `speed = 1` when hitstop ends.

---

## Testing

Existing tests in `tests/character/test_combo_fsm.cpp` remain valid after renaming `attack_pressed` → `attack_light` in fixtures. New test cases to add:

- Buffer input during `Active` phase → next combo step fires on `Recovery` transition
- No buffer input → combo ends at `Idle` after `Recovery`
- Dodge-cancel fires when Space pressed past `dodge_cancel_start`
- Hitstop: `anim.speed == 0` for N frames after hit, then restores
- Crossfade: `blend_weight` decays from 1.0 to 0.0 over `blend_duration`
