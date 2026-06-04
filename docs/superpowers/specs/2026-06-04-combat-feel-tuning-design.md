# Combat Feel Tuning — Design

Date: 2026-06-04
Status: Approved for planning
Scope: Refine the existing C++ combat system (`engine/character/core`) for snappy,
flowing combos. No rewrite — this builds on the already-shipped Combat Redesign.

> Language note: This engine is C++20 / Vulkan (flecs, GLM, Catch2). The original
> task framed it as a "JavaScript/TypeScript engine"; that was a mistake. All code
> here is C++ following existing engine conventions.

---

## 1. Problem (measured, not guessed)

The combo, input-buffer (10-frame TTL), cancel-window, crossfade, hitstop and
Startup/Active/Recovery frame-data systems already exist and work. The combat
nonetheless feels sluggish. A diagnostic tool (`tools/anim_inspect`) measured the
real clip durations and resolved the normalized frame data into seconds:

| attack | clip | duration | startup→hit | recovery tail | cancel opens |
|---|---|---|---|---|---|
| counterstrike | Counterstrike | 6.53 s | 2.48 s | 2.94 s | 4.70 s |
| dodge_and_counter | Dodge_and_Counter | 9.37 s | 3.93 s | 3.75 s | 7.12 s |
| elbow_strike | Elbow_Strike | 2.50 s | 0.80 s | 1.40 s | 1.60 s |
| shield_push | Shield_Push_Left | 2.43 s | 0.68 s | 1.31 s | 1.61 s |
| sweeping_kick | Sweeping_Kick | 2.37 s | 0.71 s | 1.14 s | 1.66 s |
| high_kick | High_Kick | 2.10 s | 0.73 s | 1.09 s | 1.43 s |
| lunge_spin_kick | Lunge_Spin_Kick | 1.67 s | 0.60 s | 0.63 s | 1.30 s |
| spartan_kick | Spartan_Kick | 1.47 s | 0.50 s | 0.73 s | 1.06 s |

**Root cause:** normalized frame data applied to 2–9 second mocap clips. The two
"finishers" (6.5 s / 9.4 s) are unplayable; the others carry 1–1.4 s of dead
recovery tail. Because windows are normalized to the bloated duration, cancel
windows open far too late for the 10-frame buffer to reach. Additionally,
`recovery_seconds` is parsed and unit-tested but **never used** by `combat_tick`
(`engine/character/core/CombatController.cpp`) — the reset only happens at clip
end, so there is no data lever to shorten recovery today.

All 38 clips in `assets/Fight` were verified skeleton-compatible with the player
rig (24 bones), so short boxing clips can replace the over-long finishers without
offline retargeting.

---

## 2. Approach

Data-driven clip trimming + playback speed, plus actually wiring `recovery_seconds`.
No GLB re-export, no absolute-time refactor. Three new per-attack data fields turn
any mocap clip into a snappy attack, and recovery becomes a fixed short duration
independent of the clip's dead tail.

---

## 3. Data model — `AttackDef` (`engine/character/core/AttackData.hpp`)

New fields (additive; existing fields keep their meaning but are re-based onto the
trimmed region):

```cpp
struct AttackDef {
    std::string id;
    std::string clip;

    // --- NEW: trimmed playback region of the raw clip ---
    float clip_start_norm = 0.f;   // [0,1) of raw clip; skip dead intro
    float clip_end_norm   = 1.f;   // (0,1] of raw clip; cut dead outro
    float time_scale      = 1.f;   // playback multiplier (>0); 1.5 => 1.5x faster

    // Windows below are normalized within the TRIMMED region [clip_start, clip_end]:
    //   0 == clip_start_norm * duration, 1 == clip_end_norm * duration
    float hit_start_norm  = 0.f;
    float hit_end_norm     = 0.f;
    float range            = 0.f;
    float radius           = 0.f;
    float recovery_seconds = 0.2f; // NOW USED: fixed end-lag after Active before auto-return
    float cancel_start_norm = 0.7f;
    float dodge_cancel_start_norm = 0.6f;

    int   hitstop_frames   = 4;    // NEW: per-attack hitstop on a confirmed hit
};
```

**Invariants asserted on load** (extend `parse_attack_block`):
- `0 <= clip_start_norm < clip_end_norm <= 1`
- `time_scale > 0`
- existing: `0 <= hit_start_norm <= hit_end_norm <= 1`
- existing: `cancel_start_norm >= hit_end_norm`
- existing: `dodge_cancel_start_norm <= cancel_start_norm`
- `hitstop_frames >= 0`

**Text format** (`combat_attacks.txt`) new keys: `clip_window <start> <end>`,
`time_scale <s>`, `hitstop <frames>`. All optional with the defaults above.

---

## 4. Playback semantics

Effective attack duration = `(clip_end_norm − clip_start_norm) * raw_duration / time_scale`.

- `start_attack` (`CombatController.cpp`): after `crossfade_to`, set
  `anim.time_seconds = clip_start_norm * raw_duration` and `anim.speed = time_scale`.
- `norm_time` helper: map `anim.time_seconds` to `[0,1]` over the trimmed region:
  `nt = (time_seconds − clip_start_s) / (clip_end_s − clip_start_s)`, clamped.
- **Clamp playback to the trimmed end:** in `combat_tick`, once
  `anim.time_seconds >= clip_end_s`, hold it at `clip_end_s`. This prevents the dead
  outro from ever showing during recovery; the character holds the final strike pose
  for the short recovery window, then crossfades out.
- `AnimationController::tick` is unchanged (it still advances and clamps to the raw
  clip duration, which is `>= clip_end_s`, so it never fights the combat clamp).
- **Fail fast on bad clip data:** `start_attack` resolves the referenced clip's raw
  duration up front. If the clip is missing or its `duration_seconds <= 0`, the
  attack fails to start and an error is logged — it does NOT fall back to untrimmed
  playback. Silent fallback would reintroduce the mushy long-clip behaviour with no
  visible cause, so this case is surfaced loudly rather than degraded.

## 5. Recovery + cancel model (`combat_tick`)

`dt` is **already a parameter** of `combat_tick`
(`void combat_tick(CombatController&, Transform&, AnimationState&, InputBuffer&,
const AttackTable&, const std::vector<AnimClip>&, const ChainTable&, float dt)`); it
is currently discarded via `(void)dt;`. We simply start using it. No tick-ownership
or call-path signature changes elsewhere — `AnimationController::tick` remains the
sole owner of `anim.time_seconds`.

- Add `float recovery_timer = 0.f;` to `CombatController`.
- On `Active → Recovery` transition, reset `recovery_timer = 0`.
- In `Recovery`: accumulate `recovery_timer += dt`. Cancel windows are gated by
  normalized trimmed time (Option B — consistent with the existing window system):
  - the attack can be **chain-canceled once `nt >= cancel_start_norm`** with a
    buffered same-chain input → start next combo step;
  - it can be **dodge-canceled once `nt >= dodge_cancel_start_norm`** with a buffered
    `Dodge`;
  - otherwise, when `recovery_timer >= recovery_seconds`, `reset_to_idle`.
  Because `cancel_start_norm` defaults to (and in the roster equals) `hit_end_norm`,
  cancels are typically available from the very start of Recovery; an attack may set
  `cancel_start_norm > hit_end_norm` to delay cancels into late Active/early Recovery.
- The old "reset at clip end" path is removed — recovery length is now purely
  `recovery_seconds`, decoupled from the clip tail.

This is the primary sluggishness fix: a Light attack's total lock becomes
`startup + active + recovery_seconds` (~0.5–0.7 s) instead of the full clip.

## 6. Transition + hitstop tuning

- Attack→attack crossfade: `0.08 s → 0.06 s` (snappier chaining).
- Reset→Walk crossfade: `0.15 s → 0.10 s`.
- Hitstop becomes per-attack via `AttackDef::hitstop_frames`. `trigger_hit_react`
  (`HitReactSystem`) takes the attacker's hitstop frame count as a parameter from its
  caller instead of the hardcoded `5`. Suggested: Lights 3, mid 4–5, finishers 6.
- **Single hitstop owner:** `MovementApp.cpp` is already the system that confirms a
  hit (`try_hit_in_window` → `trigger_hit_react`). It is the *only* place that reads
  the active `AttackDef::hitstop_frames` and passes it into `trigger_hit_react`.
  `CombatController` does NOT independently trigger hitstop — it only drains the
  `hitstop_active`/`hitstop_frames` overlay that `HitReactSystem` set. This keeps
  hitstop logic from splitting across two files.

## 7. New clip roster + chains

Replace the over-long finishers. All clips loaded from `assets/Fight` (skeleton-
compatible). Logical clip name → source GLB (raw duration):

| Logical name | Fight GLB | raw |
|---|---|---|
| Jab_Left | Left_Jab_from_Guard | 1.80 s |
| Jab_Right | Right_Jab_from_Guard | 2.03 s |
| Hook_Left | Left_Hook_from_Guard | 1.03 s |
| Uppercut_Right | Right_Uppercut_from_Guard | 1.07 s |
| Uppercut_Left | Left_Uppercut_from_Guard | 1.40 s |
| Upper_Hook_Right | Right_Upper_Hook_from_Guard | 1.80 s |
| Knee_Strike | Boxing_Guard_Step_Knee_Strike | 2.53 s |
| Kick_High_Step | Step_in_High_Kick | 1.33 s |
| Roundhouse | Roundhouse_Kick | 2.73 s |
| Spin_Kick | Lunge_Spin_Kick | 1.67 s |
| Shield_Push | Shield_Push_Left | 2.43 s |

Locomotion `Walk` / `Run` stay loaded from the player directory.

**Chains** (`movement/MovementApp.cpp`):
- **Light** (`kLightChain`): `jab_left → jab_right → hook_left → uppercut_right` (finisher)
- **Heavy** (`kHeavyChain`): `uppercut_left → upper_hook_right → knee_strike` (finisher)
- **Kick** (`kKickChain`): `high_kick_step → roundhouse → spin_kick` (finisher)
- **Special** (`kSpecialChain`): `shield_push`

**Initial frame data** (`combat_attacks.txt`). Values are measured starting points
and must be treated as first-pass tuning, not final balance — combat feel is
playtest-dependent, and these will be refined live via `anim_inspect` + playtest.
Effective durations target ~0.55–0.70 s for openers and ~0.75–0.85 s for finishers:

```
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

## 8. Clip loading (`engine/character/core/CharacterCatalog.cpp`)

`load_player_set` keeps base + `Walk` + `Run` from the player directory and loads
the 11 combat clips above from `assets/Fight` (full path, skeleton validated on
ingest, cached per `base|anim|name` key). Old player-dir combat clips
(Counterstrike, Dodge_and_Counter, Elbow_Strike, etc.) are no longer loaded.

## 9. Tests

- `tests/character/test_attack_data.cpp`: parse `clip_window`, `time_scale`,
  `hitstop`; assert new invariants (`clip_start < clip_end`, `time_scale > 0`).
- `tests/character/test_combo_fsm.cpp`: trimmed-window `norm_time` mapping;
  `start_attack` seeds `time_seconds = clip_start_s` and `speed = time_scale`;
  chain-cancel only fires once `nt >= cancel_start_norm`; dodge-cancel only once
  `nt >= dodge_cancel_start_norm`; playback clamps at `clip_end_s`.
- **Explicit anti-regression test** `Recovery_DoesNotWaitForRawClipEnd_WhenRawClipHasLongTail`:
  build a synthetic attack whose raw clip has a multi-second tail, set
  `recovery_seconds = 0.16`, drive past Active into Recovery, and assert the FSM
  returns to `Idle` after ~`recovery_seconds` of accumulated `dt` — NOT at the raw
  clip end. This pins the core bug (recovery formerly waited for the full clip).
- `start_attack` fail-fast: an attack whose clip is missing or has
  `duration_seconds <= 0` does not enter `Startup` (stays `Idle`, logs error).
- Keep `tools/anim_inspect` as a permanent tuning tool; extend it to print the
  *effective* (trimmed + scaled) duration per attack.

## 10. Out of scope (YAGNI)

No block-system rework, no variant pools, no weapon slots, no GLB re-exports, no new
render features. Movement/dodge integration stays as-is (dodge-cancel already wired).

## 11. File map

| File | Change |
|---|---|
| `engine/character/core/AttackData.hpp` | Add `clip_start_norm`, `clip_end_norm`, `time_scale`, `hitstop_frames` |
| `engine/character/core/AttackData.cpp` | Parse new keys; assert invariants |
| `engine/character/core/CharacterComponents.hpp` | Add `recovery_timer` to `CombatController` |
| `engine/character/core/CombatController.cpp` | Trimmed `norm_time`; seed time/speed; recovery timer; clamp to `clip_end_s`; per-attack hitstop hand-off |
| `engine/character/core/HitReactSystem.hpp/.cpp` | `trigger_hit_react` takes hitstop frames from caller |
| `engine/character/core/CharacterCatalog.cpp` | Load combat clips from `assets/Fight` |
| `assets/character/combat_attacks.txt` | New roster + trimmed frame data |
| `movement/MovementApp.cpp` | New chain definitions; pass `AttackDef::hitstop_frames` on hit |
| `tools/anim_inspect/main.cpp` | Print effective trimmed/scaled durations |
| `tests/character/test_attack_data.cpp` | New field + invariant tests |
| `tests/character/test_combo_fsm.cpp` | Recovery/trim/clamp tests |
