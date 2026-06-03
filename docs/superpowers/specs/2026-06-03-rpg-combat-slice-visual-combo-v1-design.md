# RPG Combat Slice v1 — Visual Combo vs. Dummy

Status: Draft for review  
Date: 2026-06-03  
Scope: First playable combat proof after Movement Foundation v1  
Prerequisite: [Movement Foundation v1](2026-06-03-rpg-combat-slice-movement-foundation-design.md) (implemented)

## 1. Purpose

This milestone proves that the Meshy Dungeon Explorer player character can be
seen in the movement arena, play locomotion clips, execute a committed 3-hit
unarmed combo, and produce visible hit feedback on a user-supplied training
dummy — without HP, death, loot, or enemy AI.

Long-term direction remains a Gothic-like action RPG (see Movement Foundation
spec). This slice narrows the next step to **character presentation + visual
combat feedback** only.

### Success criterion

The player can move in third person, trigger a 3-attack combo (Kick → Elbow →
Counterstrike), and when the attack **hit window** overlaps the dummy collider,
the dummy plays a hit reaction or knockback. No health UI, no death, no dummy
attacks.

### Explicit non-goals (v1)

- Sword or weapon mesh / sword combo
- Forest Adventurer GLB as placeholder dummy
- HP, damage numbers, death, loot, respawn
- Enemy attacks, counter AI, lock-on
- Foot IK, root motion extraction, animation retargeting across different skeletons
- Production HUD (debug overlay only)

## 2. Product decisions (confirmed)

| Topic | Decision |
|--------|----------|
| Player mesh | Meshy `Meshy_AI_Voxel_Dungeon_Explore_biped` (Character_output + animation GLBs) |
| Dummy mesh | `assets/Meshy_AI_Voxel_Straw_Fantasy_D_biped/` (Straw Fantasy biped, Hit_Reaction_1 base) |
| Dummy behavior | Static + simple hit reaction (animation if present, else knockback); no attack |
| Player combat | 3-hit committed combo: High Kick → Elbow Strike → Counterstrike |
| Hit outcome | Visual only (hit anim / knockback); no HP or death |
| Placeholder | No Forest Adventurer; no combat slice until dummy GLB exists (M4 gate) |
| Combat style | Unarmed martial arts (Meshy clips), not sword — updates parent spec wording |

## 3. Assets

### 3.1 Player (Meshy Dungeon Explorer)

Base mesh:

- `assets/Meshy_AI_Voxel_Dungeon_Explore_biped/Meshy_AI_Voxel_Dungeon_Explore_biped_Character_output.glb`

Locomotion clips (separate `*_withSkin.glb` files, ~24 MB each):

- `..._Animation_Walking_withSkin.glb`
- `..._Animation_Running_withSkin.glb`
- Idle: bind pose or first frame of walk from Character_output

Combo clips (v1, fixed set):

- `..._Animation_High_Kick_withSkin.glb`
- `..._Animation_Elbow_Strike_withSkin.glb`
- `..._Animation_Counterstrike_withSkin.glb`

Other Meshy clips (crouch, dodge, other kicks) are out of scope for v1 but
remain available for later milestones.

### 3.2 Training dummy (Straw Fantasy biped)

**Delivered:** `assets/Meshy_AI_Voxel_Straw_Fantasy_D_biped/`

| File | Role in v1 |
|------|------------|
| `..._Animation_Hit_Reaction_1_withSkin.glb` | **Base mesh** (no separate `Character_output.glb` in folder — ingest mesh+skeleton from this clip) + default hit-reaction clip |
| `..._Animation_Electrocution_Reaction_withSkin.glb` | Optional alternate hit react (not required for M4) |
| `..._Animation_Walking_withSkin.glb` | Out of scope (dummy does not move in v1) |
| `..._Animation_Running_withSkin.glb` | Out of scope |

M1 validation runs on the dummy skeleton when loading these clips (same rules as player).

**Arena placement (initial, tune in playtest):**

```
entity "training_dummy" {
    transform position 3 0 0 yaw 180
    collider box half_extents 0.4 0.9 0.4
    skinned_model path "assets/Meshy_AI_Voxel_Straw_Fantasy_D_biped/..._Hit_Reaction_1_withSkin.glb"
    hit_react clip Hit_Reaction_1 knockback 0.3 duration 0.25
    debug_name "Training Dummy"
}
```

Exact `half_extents` and position depend on mesh bounds after ingest (adjust in M4).

The M4 gate is **unblocked** — dummy GLB path is known.

### 3.3 Forest Adventurer

`assets/Meshy_AI_Voxel_Forest_Adventur_0603211455_image-to-3d-texture.glb` is
**not** used in this milestone (image-to-3D, likely non-rigged).

## 4. Architecture

### 4.1 Module layering

```
engine/character/core     glTF ingest, animation, combo FSM, hit math (no Vulkan)
engine/character/render   CharacterPass (Vulkan skinned mesh)
movement_test             App: MovementWorld, input, sim, renderer registration
```

Dependencies:

- `engine_character_core` → GLM, optional cgltf (or equivalent), no `engine_render`
- `engine_character_render` → `engine_render`, `engine_character_core`
- `movement_test` → movement core, character render, platform, renderer

Movement Foundation code stays unchanged except optional `.arena` schema
extensions and `MovementApp` wiring.

### 4.2 glTF ingest strategy

Meshy exports each animation as a full skinned GLB. v1 pipeline:

1. Load `Character_output.glb` → mesh, skeleton, bind pose, texture.
2. For each animation GLB → extract animation channels only; verify skeleton
   compatibility (joint count / names); on mismatch log warning and skip clip.
3. On first load, write cooked cache under **`build/character_cache/`** only
   (e.g. `<hash>.charbin`). Never under `assets/` — source assets and generated
   cache must stay separate.
4. v1 does **not** implement cross-skeleton retargeting.

### 4.2.1 M1 asset validation (strict, fail-fast on player set)

Before accepting any Meshy animation GLB for the player skeleton, ingest must
verify against `Character_output.glb`:

| Check | On failure |
|--------|------------|
| Same joint count | Reject clip, error log |
| Same joint names (order may differ only if remapped by stable name map) | Reject clip |
| Same skeleton hierarchy (parent indices / node tree) | Reject clip |
| Bone count ≤ 128 | Reject asset |
| Scale and up-axis consistent with base (document expected: Y-up, unit meters) | Reject or warn+reject |
| Skinned mesh has vertex bone weights | Reject base mesh |
| Base color texture present, or apply documented fallback material | Warn if fallback used |

Goal: catch Meshy export mismatches in M1, not as subtle in-game animation bugs.

### 4.3 ECS extensions (MovementWorld)

New pure-data components in `engine/movement/core` or `engine/character/core`
(keep movement collider/player types shared):

| Component | Role |
|-----------|------|
| `SkinnedModel` | Asset path, cooked cache key, default texture |
| `AnimationState` | Active clip id, time, speed, looping flag |
| `CombatController` | Combo index, phase (Idle / Attacking / Recovery), committed flag, `attack_yaw` (locked at attack start) |
| `HitReact` | Dummy-only: phase, knockback vector, timer |

`Transform`, `Collider`, `PlayerController`, `CameraRig` unchanged.

Arena schema additions (strict parser, duplicate-field rules unchanged):

```
skinned_model path "<gltf path>"
combat_controller combo "high_kick,elbow_strike,counterstrike"
hit_react knockback <meters> duration <seconds>
```

### 4.4 Simulation rules

- Locomotion clips selected from player velocity / grounded state (walk / run / idle).
- `attack_pressed` is edge-triggered in `InputSnapshot` (same pattern as jump).
- During `CombatController::Attacking`: player movement frozen (committed attacks).
- On entering each attack: `attack_yaw = transform.yaw` at that instant; hit
  capsule and facing use **`attack_yaw`**, not live yaw (camera/movement may
  still update `transform.yaw` for render root, but hit math does not).
- Combo advances on clip end: hit 1 → hit 2 → hit 3 → Recovery → Idle.
- Input during Attacking/Recovery ignored for new combo until Idle.
- Animation time advances on **fixed sim step**; bone poses sampled at sim time.
- Render mesh root uses **interpolated** `Transform` (position/yaw); bones use sim pose.

### 4.5 Attack definitions (data table, not hardcoded)

v1 loads attack parameters from a strict text data file (same parser discipline as
`.arena`), e.g. `assets/character/combat_attacks.txt`. Example content:

```
attack high_kick {
    clip High_Kick
    hit_window 0.35 0.48
    range 1.25
    radius 0.35
    recovery 0.25
}
attack elbow_strike {
    clip Elbow_Strike
    hit_window 0.32 0.44
    range 0.85
    radius 0.30
    recovery 0.20
}
attack counterstrike {
    clip Counterstrike
    hit_window 0.38 0.55
    range 1.05
    radius 0.35
    recovery 0.35
}
```

`combat_controller` in `.arena` references attack ids:
`combo "high_kick,elbow_strike,counterstrike"`.

Fields:

| Field | Meaning |
|--------|---------|
| `clip` | Logical clip name → cooked animation id |
| `hit_window` | Normalized clip time `[start, end]` inclusive |
| `range` | Hit capsule center offset forward from player (meters) |
| `radius` | Hit capsule radius (meters) |
| `recovery` | Seconds in Recovery phase after clip ends before Idle |

Balancing changes only touch this file, not C++.

### 4.6 Hit detection (visual only)

- Each sim step while attacking: if normalized clip time `t` is inside
  `[hit_window_start, hit_window_end]` **and** this swing has not hit yet → run
  overlap test. Fixed-step may skip exact frames; the window avoids missed hits.
- **At most one hit per attack swing** (`hit_consumed` flag reset when next attack starts).
- Active hit volume: capsule at `attack_yaw`, offset `range`, radius from table.
- Target: static box collider on dummy entity.
- On overlap: trigger `HitReact` (knockback + optional hit clip).
- No `Health` component, no damage accumulation.

### 4.7 Rendering

- New `CharacterPass` registered at `pass_insertion::kBeforeImgui`.
- Shaders: `character_skinned.vert` / `character_skinned.frag`.
- GPU: skinned vertices, bone matrix UBO (max 128 bones), single diffuse texture per mesh.
- One draw per skinned entity per frame.
- DebugDrawPass remains for capsule / hit debug; optional hit capsule draw in debug color.

### 4.8 Data flow

```
Input (frame) → InputSnapshot
Fixed-step loop:
  PlayerMovement → Transform (frozen while Attacking)
  CombatController → clip, attack_yaw, recovery timers
  AnimationController → advance time, bone matrices
  HitTest (if t in hit_window and !hit_consumed) → HitReact on dummy
Render:
  CharacterPass ← interpolated transform + sim bone matrices
  DebugDrawPass ← optional overlays (hit capsule uses attack_yaw)
```

### 4.9 Executable

v1 stays in **`movement_test`** only. No separate `combat_test` target — Combat
Slice extends the existing movement app and arena workflow. Split out later if needed.

## 5. Error handling

| Condition | Behavior |
|-----------|----------|
| Missing / invalid player GLB | Fail arena load with clear error |
| Animation GLB skeleton mismatch | Reject clip (M1 validation), fail ingest |
| Dummy GLB invalid / skeleton fail validation | Fail ingest; block M4 until fixed |
| Attack misses | Combo continues, no dummy feedback |
| Dummy without hit animation | Knockback only |
| GPU / validation errors | Same as engine baseline; no special case in v1 |

Performance target: one player + one dummy at 60 FPS on developer machine; no LOD.

## 6. Testing

| Test | Scope |
|------|--------|
| `test_gltf_ingest` | Load fixture; mesh, bone count, validation rules |
| `test_skeleton_compat` | Matching vs mismatched animation GLB |
| `test_attack_data` | Parse `combat_attacks.txt`, duplicate-field errors |
| `test_animation_controller` | Time advance, end event, loop |
| `test_combo_fsm` | 3-step chain, ignore input while attacking |
| `test_hit_window` | Hit fires inside window, once per swing, not outside window |
| `test_hit_overlap` | Capsule vs AABB at `attack_yaw` |
| Manual | `movement_test` + `combat_test.arena`: combo visible, dummy reacts |

GPU / window tests are manual only (same as movement_test).

## 7. Milestones

| Id | Deliverable | Blocked by |
|----|-------------|------------|
| M0 | This spec + implementation plan | — |
| M1 | glTF ingest + strict skeleton validation + `build/character_cache/` + tests | — |
| M2 | CharacterPass; player visible with idle/walk/run | M1 |
| M3 | Attack data table + combo FSM + three clips on player | M2 |
| M4 | Hit window + Straw Fantasy dummy visual feedback | M3 |
| M5 | `combat_test.arena` in `movement_test`, debug overlay (clip, combo, hit window) | M4 |

M1–M3 can proceed without the dummy visible; M4 integrates
`Meshy_AI_Voxel_Straw_Fantasy_D_biped`. No Forest Adventurer placeholder.

## 8. Deviations from parent Combat Slice spec

The parent document describes sword combo, Training Dummy Plus with HP/death/loot,
and full combat loop. This v1 document intentionally defers those in favor of
visual combo proof and user-timed dummy delivery. Update the parent spec
reference when v2 adds HP and enemy behavior.

## 9. Open items (until implementation)

- Fine-tune `hit_window` / `range` / `radius` in `combat_attacks.txt` during M3/M4 playtest.
- Dummy collider `half_extents` and arena position after first ingest (mesh bounds).
- Confirm whether `Character_output.glb` for Straw Fantasy will be added later (optional; v1 uses Hit_Reaction_1 as base).
