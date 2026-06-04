# Combat Core + MovementWorld Integration - Design Spec
**Date:** 2026-06-04
**Status:** Approved for implementation planning

## Problem Statement

The combat core is implemented in `engine/character/core` and is already used by
`movement/MovementApp`, but the connection is still app-local. Player combat
state, player animation state, player input buffer, and dummy hit reaction live
as one-off fields on `MovementApp`.

That works for the current prototype, but it prevents combat from becoming a
normal entity capability. Future enemies, NPCs, multiple dummies, and combat
debug tools would need more app-local special cases instead of sharing the same
core data path.

## Chosen Approach

Add optional character/combat component stores to `engine::movement::MovementWorld`.
This makes combat part of the shared entity model without creating a separate
registry or large game-core rewrite.

`MovementWorld` remains the owner of entity lifetime and component stores.
Character/combat state becomes attached to entities in the same style as
`Transform`, `Collider`, `PlayerController`, and `CameraRig`.

This deliberately makes `engine_movement_core` depend on `engine_character_core`.
That dependency is an architecture decision, not accidental coupling: movement
owns entity lifetime and collision state, while character core owns animation,
combat, input-buffer, and hit-reaction state. The implementation must update the
CMake target links explicitly, including `movement_tests`.

## Scope

In scope:
- Add `AnimationState`, `CombatController`, `InputBuffer`, and `HitReact`
  component stores to `MovementWorld`.
- Ensure `MovementWorld::destroy()` removes the new components.
- Move player and dummy runtime state in `MovementApp` from app-local fields to
  entity components.
- Keep the current combat tick order and hit detection behavior.
- Add focused tests for component lifecycle.
- Add a small behavior smoke test to protect the existing combat tick order.
- Verify movement and character targets still build.

Out of scope:
- Save/load persistence for transient combat state.
- A separate `CombatWorld` registry.
- Enemy AI, new attacks, balance changes, or combat feel tuning.
- Moving `CharacterAsset`, `CharacterPass` handles, attack tables, or chain
  tables into `MovementWorld`.

## Architecture

`MovementWorld` gains four new stores:

```cpp
using AnimationStateStore = ComponentStore<engine::character::AnimationState>;
using CombatControllerStore = ComponentStore<engine::character::CombatController>;
using InputBufferStore = ComponentStore<engine::character::InputBuffer>;
using HitReactStore = ComponentStore<engine::character::HitReact>;
```

And accessors matching the existing component style:

```cpp
animations()
combats()
input_buffers()
hit_reacts()
```

Entity composition after the change:

Player:
`Transform + Collider + PlayerController + CameraRig + AnimationState + CombatController + InputBuffer`

Training dummy:
`Transform + Collider + AnimationState + HitReact`

`HitReact` is a generic character reaction component. The training dummy is only
the first user; the implementation should not name helpers or code paths as if
hit reaction were dummy-only.

`MovementApp` keeps app-level resources that are not per-entity simulation
state:
- `CharacterAsset` for player and dummy.
- `CharacterPass` render handles.
- `AttackTable`.
- Player `ChainTable`.
- Camera `ScreenShake`.

## Data Flow

The current fixed-step order remains intact:

1. Tick the player's `InputBuffer`.
2. Push edge-triggered attack and dodge inputs into the player's buffer.
3. Tick the player's `AnimationState`.
4. Select locomotion animation when combat is idle.
5. Tick `CombatController`.
6. Tick movement, with movement input locked while combat is active.
7. Run hit detection using entity `Transform` and `Collider`.
8. Trigger the dummy `HitReact`, dummy `AnimationState`, hitstop, and screenshake.
9. Tick dummy hit reaction and dummy animation.

The change is where state is stored, not what the simulation does.

This ordering is an invariant. The implementation should include a focused
integration smoke test that proves an attack edge reaches the player's
`InputBuffer`, `CombatController` consumes it, movement is locked while combat is
active, and a successful hit sets the target's `HitReact`.

## Error Handling

`MovementApp` should guard every component lookup that can fail. If the player
is missing required movement components, the app keeps the existing behavior and
exits the run loop.

Combat simulation state must be independent of render asset loading. On normal
spawn, `AnimationState`, `CombatController`, `InputBuffer`, and `HitReact`
components are created even if character render assets fail to load.

If character rendering assets are missing, character rendering is skipped. If
combat components are missing, combat ticking is skipped. These are separate
failure modes.

If the dummy entity is missing `HitReact` or `AnimationState`, hit reaction and
dummy animation are skipped. This preserves the current tolerant behavior around
asset load failures.

To keep `MovementApp` readable, component lookup guards should be grouped in
small helper structs rather than nested directly through the tick body:

```cpp
struct PlayerCombatRefs { ... };
std::optional<PlayerCombatRefs> try_get_player_combat_refs();

struct TargetReactionRefs { ... };
std::optional<TargetReactionRefs> try_get_target_reaction_refs(EntityId target);
```

## Testing

Add or extend movement tests to verify:
- A spawned entity can receive `AnimationState`, `CombatController`,
  `InputBuffer`, and `HitReact` components.
- `MovementWorld::destroy()` removes those components.
- `MovementWorld::clear()` removes those components.
- Entity index reuse cannot expose stale character/combat components from a
  destroyed entity.
- Combat tick order remains behaviorally intact for the player attack path.

Verification targets:
- Build `movement`.
- Build and run movement tests.
- Build and run character tests if impacted by includes or linkage.

## Implementation Notes

The new stores require `MovementWorld.hpp` to include character component
headers. The implementation must update `engine_movement_core` to link
`engine_character_core` directly, and update `movement_tests` if needed by the
target graph.

Recommended implementation order:
1. Add `MovementWorld` stores and accessors.
2. Extend `MovementWorld::destroy()` and `MovementWorld::clear()`.
3. Add lifecycle tests for all four stores, including entity reuse.
4. Wire player and target reaction components during `MovementApp::startup()`.
5. Replace app-local player/dummy combat fields with component lookups.
6. Move lookup guards into small helper structs/functions.
7. Add the combat tick-order smoke test.
8. Build `movement_test`, `movement_tests`, and `character_tests`.

Avoid refactoring unrelated systems while doing this. This is an ownership and
wiring change, not a combat redesign.
