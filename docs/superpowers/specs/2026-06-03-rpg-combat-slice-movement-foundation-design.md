# RPG Combat Slice - Movement Foundation v1 Design

Status: Draft for review  
Date: 2026-06-03  
Scope: Movement Foundation v1 and its immediate role in the first Combat Slice

## 1. Purpose

This document defines the first technical milestone for evolving the engine toward a
classic sword-and-magic action RPG. The long-term target is a third-person fantasy
RPG with committed real-time melee combat, character progression, quests, inventory,
and a seamless chunk-based open world.

The first playable proof is a Combat Slice, but the first implementation milestone is
Movement Foundation v1. The goal is to establish stable simulation timing, minimal
ECS ownership, data/save roundtripping, primitive collision, capsule movement, a
Gothic-style third-person camera, and debug visualization before building combat.

The slice deliberately prioritizes systems that will affect combat feel:

- fixed-step simulation
- stable entity identity
- save/load-ready component state
- kinematic player movement
- capsule collision
- camera behavior
- debug draw for simulation truth

The immediate success criterion is not "fun combat" yet. It is: the player can move
through a small 3D arena in a deterministic, debuggable, save/load-capable way.

## 2. Product Direction

The target combat style is Gothic-like, heavy, and position-based:

- attacks are committed
- timing and reach matter
- wrong positioning is punished
- facing direction matters
- there is no hard lock-on in the first slice
- the player should learn spacing and commitment

The first Combat Slice will eventually include:

- a player character with a 2-3 hit sword combo
- a Training Dummy Plus enemy
- basic counterattack behavior
- hitboxes, hurtboxes, health, death, and loot
- skeletal animation from glTF/GLB assets
- debug UI instead of production HUD

Movement Foundation v1 exists to support that future combat. It does not implement
combat itself.

## 3. Confirmed Decisions

### 3.1 First Playable Slice

The first gameplay proof is:

Combat Slice: third-person movement, free Gothic-like camera, committed melee combo,
one Training Dummy Plus enemy, feedback, death, and loot.

### 3.2 First Implementation Milestone

Movement Foundation v1 comes before combat. It proves:

- fixed-step simulation
- ECS-based movement state
- data definition loading
- text save/load roundtrip
- primitive collision
- capsule movement with gravity and jump
- free third-person camera
- debug draw

### 3.3 Runtime Architecture

The runtime uses a minimal custom ECS:

- self-built
- strongly scoped
- explicit component stores
- no generic reflection in v1
- no archetype optimizer in v1
- no multithreaded ECS in v1
- no editor inspector in v1

Entities use:

- `EntityId`: volatile runtime handle, index + generation
- `PersistentId`: stable save/reference identity derived from definition context

### 3.4 Collision Direction

Collision is owned by the engine long-term:

- no early physics middleware
- no rigid-body simulation in v1
- no general-purpose physics engine in v1
- collision is focused on game queries, movement, hitboxes, and open-world expansion

Movement v1 uses primitive colliders only:

- ground plane or ground box
- static AABB/box walls
- player capsule

### 3.5 Data Direction

The project uses custom text data first:

- human-readable
- strict schema
- block syntax with braces
- `#` comments until end of line
- shared parser for definitions and saves
- no custom binary format in v1

Binary/cooked formats may be introduced later for large world data, mesh caches,
navigation data, or production saves.

### 3.6 Save/Load Direction

Save/load is in scope from Movement Foundation v1.

The early save format is custom text and debug-friendly. It is not required to be
release-stable, but it must be versioned and schema-checked.

Definitions and saves are separated:

- definitions describe default arena state
- saves reference a definition and apply runtime overrides

This mirrors the future open-world model:

- base world/chunk/cell definitions
- runtime deltas for modified state

## 4. Non-Goals

Movement Foundation v1 does not include:

- combat
- attacks
- hitboxes or hurtboxes
- enemies
- AI
- inventory
- loot
- quests
- dialog
- crafting
- character progression
- skeletal animation
- root motion
- animation events
- production HUD
- chunk streaming
- navmesh/pathfinding
- slopes
- stairs
- step-up
- sliding movement
- swept collision
- rigid bodies
- full physics simulation
- binary save format
- migration of old save versions
- editor tooling

These are intentionally deferred to keep the first milestone achievable.

## 5. Architecture Overview

Movement Foundation v1 consists of six core areas:

1. `SimClock`
2. `EntityWorld`
3. Custom text data and save/load
4. `CollisionWorld`
5. Player movement and camera
6. Debug draw and tests

The systems run in a predictable order.

Suggested simulation order:

1. `InputSystem`
2. `PlayerControllerSystem`
3. `CollisionSystem` or collision queries inside player movement
4. `CameraSystem`
5. `DebugDrawSystem`
6. `RenderSubmitSystem`

Gameplay systems operate on fixed-step simulation state. Rendering may interpolate
between previous and current transforms.

## 6. Fixed-Step Simulation

### 6.1 Requirement

Gameplay simulation must not depend directly on variable frame delta.

Movement, gravity, jump, collision, and later combat timing should run on a fixed
simulation step. The initial target is:

- `sim_dt = 1.0 / 60.0`
- accumulator-based stepping
- maximum number of sim steps per frame

The render loop remains variable.

### 6.2 Rationale

Fixed-step simulation gives stable behavior for:

- gravity
- jump arcs
- collision correction
- attack windup/active/recovery windows
- combo input timing
- deterministic save/load tests

This is established engine practice. The exact timestep is a project tuning choice,
but 60 Hz is a practical default for the first RPG prototype.

### 6.3 Transform Interpolation

`Transform` stores both previous and current state:

- `previous_position`
- `current_position`
- `previous_yaw` or previous rotation
- `current_yaw` or current rotation

Before each simulation step:

```text
previous = current
```

Simulation writes `current`.

Rendering computes:

```text
alpha = accumulator / sim_dt
render_position = lerp(previous_position, current_position, alpha)
render_yaw = lerp_angle(previous_yaw, current_yaw, alpha)
```

If the engine later uses quaternions, rotation interpolation should become `slerp`.
For v1, yaw-only interpolation is acceptable.

### 6.4 Save/Load Rule

`previous_transform` is not saved.

On load:

```text
previous_transform = current_transform
```

This prevents interpolating from stale or meaningless positions after loading.

## 7. Minimal ECS

### 7.1 Entity Identity

`EntityId` is a runtime handle:

```text
EntityId {
    index
    generation
}
```

Destroyed entities increment generation. This prevents stale handles from resolving
to newly created entities that reuse the same index.

`PersistentId` is stable identity:

```text
PersistentId = arena_id + "/" + entity_name
```

Examples:

```text
movement_test/player
movement_test/wall_north
movement_test/wall_south
```

Persistent IDs are automatically derived, but only from stable names in definition
files. They must not depend on load order.

### 7.2 Component Stores

Movement v1 uses explicit component stores:

- `TransformStore`
- `ColliderStore`
- `PlayerControllerStore`
- `CameraRigStore`
- optional `DebugNameStore`

Each store supports:

- add component
- get mutable component
- get const component
- remove component
- test component presence
- destroy component when entity is destroyed

No generic reflection is required.

### 7.3 Required Components

#### `Transform`

Responsibility:

- simulation position
- simulation yaw/rotation
- previous state for render interpolation

Persistent:

- current position
- current yaw

Transient:

- previous position
- previous yaw

#### `Collider`

Responsibility:

- describes the entity's collision shape
- marks whether collider is static or kinematic

v1 shape types:

- capsule
- box
- ground plane or ground box

Persistent:

- shape type
- dimensions
- static/kinematic mode

Transient:

- frame contact cache

#### `PlayerController`

Responsibility:

- player movement config
- current velocity
- grounded flag
- jump state

Persistent:

- velocity
- grounded
- movement configuration if not definition-driven

Definition-driven:

- speed
- jump velocity
- gravity

#### `CameraRig`

Responsibility:

- third-person camera follow settings
- camera yaw/pitch
- target entity

Persistent:

- yaw
- pitch
- distance
- height or pivot offset

Definition-driven:

- default distance
- default height
- pitch limits

#### `DebugName`

Responsibility:

- human-readable debug display

Persistent:

- optional, if loaded from definition

### 7.4 ECS Scope Rules

Movement v1 must not grow a full ECS framework.

Forbidden in v1:

- generic query language
- reflection metadata for all components
- automatic serialization of arbitrary components
- editor integration
- archetype chunk migration
- multithreaded system scheduler

Allowed:

- explicit systems
- explicit component stores
- small helper functions for common component access
- tests for ID generation and component lifecycle

## 8. Custom Text Format

### 8.1 Syntax

The custom text format uses block syntax:

```text
arena "movement_test" version 1
{
    entity "player"
    {
        transform position 0 1 0 yaw 0
    }
}
```

Supported tokens:

- identifiers
- quoted strings
- numbers
- booleans
- `{`
- `}`
- `#` comments until end of line

Not supported in v1:

- includes
- variables
- expressions
- inheritance
- macros
- multiline comments
- arbitrary maps

### 8.2 Schema Strictness

The format is strict.

The parser/schema loader must reject:

- unknown top-level blocks
- unknown fields
- missing required fields
- duplicate unique fields
- wrong value types
- malformed blocks
- invalid enum/shape names
- save references to unknown `PersistentId`s

Errors should include:

- file path
- line
- column
- message

Example error:

```text
assets/arenas/movement_test.arena:12:18: unknown field 'speeed' in player_controller
```

### 8.3 Definition Example

Example arena definition:

```text
arena "movement_test" version 1
{
    entity "player"
    {
        transform position 0 1 0 yaw 0
        player_controller speed 4.0 jump_velocity 5.5 gravity -9.81
        collider capsule radius 0.35 height 1.75
        camera_rig yaw 0 pitch -15 distance 4.0 height 1.5
    }

    entity "floor"
    {
        transform position 0 0 0 yaw 0
        collider box half_extents 6 0.25 6
    }

    entity "wall_north"
    {
        transform position 0 1 -6 yaw 0
        collider box half_extents 6 1 0.25
    }
}
```

### 8.4 Save Example

Example save:

```text
save "slot_01" version 1
{
    arena "movement_test"

    override "movement_test/player"
    {
        transform position 1.2 1.0 3.4 yaw 90
        player_controller velocity 0 0 0 grounded true
        camera_rig yaw 45 pitch -12 distance 4.0 height 1.5
    }
}
```

### 8.5 Future Compatibility

The v1 format should be intentionally small.

Future systems may reuse the same syntax for:

- attacks
- actor definitions
- loot
- items
- quests
- dialog
- chunk/cell definitions

However, Movement v1 must not implement those schemas yet.

## 9. Definition and Save Loading

### 9.1 Definition Loading

Definition loading creates the baseline world:

1. Parse definition file.
2. Validate strict schema.
3. Read `arena_id`.
4. Create entities for named definitions.
5. Derive `PersistentId = arena_id/entity_name`.
6. Attach components from definition.
7. Initialize transient state.

### 9.2 Save Loading

Save loading applies runtime state:

1. Parse save file.
2. Validate strict schema.
3. Read `arena_id`.
4. Load matching arena definition.
5. Build baseline ECS world from definition.
6. Apply overrides by `PersistentId`.
7. Reject unknown override IDs.
8. Reset transient state.
9. Set `previous_transform = current_transform`.

### 9.3 Save Writing

Save writing stores runtime overrides.

For v1 it may store all save-relevant movement components for the player, even if
some values are identical to defaults. Future versions can optimize by writing only
changed fields.

The save writer must not write:

- renderer handles
- GPU resource IDs
- debug draw buffers
- frame contact caches
- previous transform
- transient input state

## 10. Collision Foundation

### 10.1 Collision World

`CollisionWorld` owns collision queries for v1.

It contains:

- static arena colliders
- kinematic player collider reference
- helper queries for movement and debug

v1 does not require a broadphase. A small arena can iterate over all static colliders.
The API should still be shaped so a broadphase can be added later.

### 10.2 Shapes

Supported v1 shapes:

- `Capsule`
- `Box`
- `GroundPlane` or ground box

Player:

- capsule

Arena:

- boxes for floor and walls

### 10.3 Movement Collision

Movement v1 uses discrete collision:

1. Compute desired velocity from input.
2. Apply gravity.
3. Apply jump impulse if grounded and jump pressed.
4. Compute desired delta for fixed step.
5. Tentatively move capsule.
6. Test overlap against static colliders.
7. If overlap occurs, reject or correct movement.
8. Resolve ground state.

Response model:

- stop-on-impact
- no sliding in v1
- no swept collision in v1
- no step-up in v1
- no slopes in v1

### 10.4 Grounding

Grounding must be separate from wall collision.

Rules:

- ground contact comes from floor contact or downward probe
- wall contact must not set `grounded`
- jump is allowed only when `grounded`
- landing sets vertical velocity to zero

### 10.5 Known Limitations

Discrete collision can tunnel at high speed. This is accepted for v1 because:

- arena is small
- speeds are moderate
- walls can be thick
- fixed-step simulation limits movement per step

Future movement improvements:

- sliding
- swept collision
- step-up
- slope handling
- ceiling handling
- broadphase
- chunk-aware static collider registration

## 11. Player Movement

### 11.1 Input

Input target:

- keyboard for movement and jump
- mouse for camera

Suggested defaults:

- `WASD`: movement
- `Space`: jump
- mouse delta: camera yaw/pitch

### 11.2 Movement Direction

The camera is free behind the player, but movement remains Gothic-like:

- player facing is movement-driven
- attack-facing later uses player facing, not hard lock-on
- no target lock in Movement v1

Movement may be camera-relative or world-relative, but the decision should be kept
consistent once combat is added. For Gothic-like combat, the important rule is:

the player character's facing direction is explicit and visible.

### 11.3 Gravity and Jump

`PlayerController` stores vertical velocity.

Rules:

- gravity applies every fixed step
- jump can be triggered only when grounded
- jump sets vertical velocity to `jump_velocity`
- on landing, vertical velocity becomes zero
- no coyote time in v1
- no jump buffering in v1

## 12. Third-Person Camera

### 12.1 Camera Style

Movement v1 uses a free Gothic-like third-person camera:

- follows behind/around the player
- controlled by mouse
- no hard lock-on
- no target orbiting
- no combat camera logic

The camera should support:

- yaw
- pitch
- distance
- height/pivot offset
- pitch clamp

### 12.2 Camera Collision

Camera collision is not required in v1.

If the camera clips through walls in the box arena, that is acceptable unless it
prevents evaluating movement. Camera collision can be added later with raycasts or
sphere casts.

### 12.3 Render Interpolation

The camera may use interpolated player transform for visual smoothness. Debug draw
for collision should show simulation state, not interpolated render state.

## 13. Debug Draw and Debug UI

Debugging is part of the milestone, not polish.

Required debug visualization:

- player capsule
- floor/ground collider
- wall colliders
- ground contact/probe
- wall overlap/contact
- player facing direction

Optional debug display:

- fixed-step count this frame
- accumulator alpha
- player grounded flag
- vertical velocity
- current position
- current yaw
- `PersistentId`

Debug draw must distinguish:

- simulation state
- render-interpolated state

Collision/combat debugging should prefer simulation state.

## 14. Tests

Movement Foundation v1 includes tests.

### 14.1 ECS Tests

Required:

- creating an entity returns a valid `EntityId`
- destroyed entity handles become stale
- generation increments on index reuse
- add/get/remove component works
- destroying an entity removes its movement v1 components

### 14.2 Parser Tests

Required:

- valid arena definition loads
- `#` comments are ignored
- unknown top-level block fails
- unknown field fails
- missing required field fails
- duplicate unique field fails
- wrong type fails
- error reports line and column

### 14.3 Save/Load Tests

Required:

- arena definition creates player and static colliders
- `PersistentId` is derived as `arena_id/entity_name`
- save override applies player transform
- save/load roundtrip preserves `Transform`
- save/load roundtrip preserves `PlayerController` runtime state
- save/load roundtrip preserves `CameraRig`
- unknown save override `PersistentId` fails
- loading sets `previous_transform = current_transform`
- previous transform is not serialized

### 14.4 Collision Tests

Required:

- capsule can stand on floor
- gravity moves ungrounded capsule downward
- grounded capsule can jump
- ungrounded capsule cannot jump again
- wall overlap blocks movement
- wall contact does not set grounded
- floor contact sets grounded

### 14.5 Manual Acceptance Tests

Required:

- player moves with keyboard
- player jumps and lands
- player cannot pass through arena walls
- camera follows and rotates with mouse
- transform interpolation looks smooth enough
- debug draw matches simulation state

## 15. Milestones

### M1: Fixed-Step + Minimal ECS

Build:

- `SimClock`
- `EntityWorld`
- `EntityId` generation
- `Transform`
- transform interpolation

Acceptance:

- one entity updates in fixed step
- rendering can read interpolated transform
- stale entity handles are rejected
- ECS tests pass

### M2: Custom Text Data + Save/Load Roundtrip

Build:

- lexer/parser
- strict schema validation
- arena definition loader
- movement save loader/writer
- `PersistentId` derivation

Acceptance:

- arena definition loads
- save applies overrides
- save/load roundtrip works
- parser and save/load tests pass

### M3: Primitive Collision + Capsule Movement

Build:

- `CollisionWorld`
- static arena colliders
- capsule collider
- gravity
- jump
- discrete collision
- stop-on-impact

Acceptance:

- player remains grounded on floor
- player can jump and land
- player cannot move through walls
- collision tests pass

### M4: Third-Person Camera + Debug Draw

Build:

- `CameraRig`
- keyboard and mouse movement integration
- free third-person camera
- debug draw for collision and movement state

Acceptance:

- movement and camera are usable in the arena
- debug draw is accurate
- manual acceptance tests pass

### M5: Movement Feel Pass

Tune:

- speed
- jump velocity
- gravity
- camera distance
- camera pitch
- arena dimensions

Acceptance:

- movement is stable enough to support the next Combat Slice milestone
- known limitations are documented

## 16. Risks and Mitigations

### Risk: ECS becomes the project

Mitigation:

- no generic query language in v1
- only movement components
- tests cover lifecycle, not performance architecture

### Risk: Custom format becomes a language

Mitigation:

- no expressions
- no includes
- no inheritance
- strict schema
- small v1 grammar

### Risk: Character controller edge cases consume time

Mitigation:

- flat arena only
- box colliders only
- no slopes
- no steps
- no sliding
- no swept collision

### Risk: Save/load blocks gameplay progress

Mitigation:

- save only movement v1 components
- text format only
- no migration
- no binary/cooked pipeline

### Risk: Debug draw lies

Mitigation:

- explicitly label sim-state vs render-state
- collision debug uses sim-state
- interpolation debug is optional

### Risk: Camera makes movement hard to evaluate

Mitigation:

- no camera collision in v1
- simple free follow camera
- camera tuning in M5

## 17. Later Combat Slice Dependencies

Movement Foundation v1 prepares these later combat systems:

- `CombatState` can rely on fixed-step timing
- attack facing can use `Transform.current_yaw`
- attack movement locks can modify `PlayerController`
- hitboxes can use `CollisionWorld` overlap queries
- hurtboxes can reuse collider/debug infrastructure
- combat debug can reuse debug draw
- save/load can later persist HP, dummy state, and loot state

Next likely milestones after Movement Foundation v1:

1. glTF/GLB skeletal actor loading
2. animation clip playback
3. `ActorStats` and `Health`
4. `CombatState` with hard committed attack phases
5. 2-3 hit combo timing
6. Training Dummy Plus
7. damage, death, and loot

## 18. Open Questions for Later

These are intentionally deferred:

- camera-relative vs tank-like movement details
- exact camera pitch/yaw constraints
- whether player yaw rotates instantly or with turn speed
- when to add sliding
- when to add swept collision
- whether combat animation events are needed
- whether root motion is ever introduced
- how chunk collision registration works
- how production save format differs from debug text save

These questions do not block Movement Foundation v1.

## 19. Summary

Movement Foundation v1 is the first serious engine milestone for the RPG direction.
It prioritizes deterministic simulation, stable entity identity, strict text data,
debuggable save/load, primitive collision, capsule movement, and a free third-person
camera.

The milestone is intentionally narrow. It should make the player controllable and
the simulation inspectable. Combat begins only after this foundation is stable.

