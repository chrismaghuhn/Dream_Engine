# Combat Slice v1 — Playtest Guide

**Date:** 2026-06-04  
**Build target:** `movement_test`  
**Arena:** `assets/movement/combat_test.arena`

---

## Build & Run

```powershell
# Rebuild (Debug)
cmake --build build --target movement_test --config Debug

# Run
.\build\movement\Debug\movement_test.exe
```

Log output is written to both the console and `movement_log.txt` in the repo root.

---

## Controls

| Input | Action |
|-------|--------|
| `WASD` | Move |
| `Shift` | Sprint |
| `Space` | Jump |
| `Mouse` | Camera orbit (yaw / pitch) |
| `Scroll` | Camera distance |
| `LMB` | Start / continue 3-hit combo |
| `T` | Toggle debug-line depth test |
| `F5` | Save player state |
| `F9` | Load player state |
| `Alt` | Release / re-capture cursor |
| `Esc` / close window | Quit |

---

## Combo

The player has a fixed 3-hit unarmed combo, defined in  
`assets/character/combat_attacks.txt`:

1. **High Kick** — `hit_window [0.35, 0.48]`, range 1.25 m, radius 0.35 m  
2. **Elbow Strike** — `hit_window [0.32, 0.44]`, range 0.85 m, radius 0.30 m  
3. **Counterstrike** — `hit_window [0.38, 0.55]`, range 1.05 m, radius 0.35 m

Each attack is committed: movement is frozen for its duration. The combo
advances automatically on clip end. After the third hit there is a short
Recovery phase before returning to Idle.

---

## Training Dummy

The Straw Fantasy dummy stands at `(3, 0, 0)`, 3 metres in front of the player.

On a successful hit during the hit window:
- The dummy plays **Hit_Reaction_1** animation.
- The dummy receives a short **knockback** (0.3 m horizontal).
- The log prints `Hit! combo[n] attack 'name'`.

---

## Debug Overlay (ImGui panel)

| Line | Meaning |
|------|---------|
| `Clip: <name>  t=<norm>` | Current animation clip + normalised time |
| `Combat: <phase>  combo[n]` | FSM phase + current hit index |
| `Attack yaw: <deg>` | Locked facing at attack start |
| `Hit window: [start, end]` | Window for current attack (red = active) |
| `Hit consumed: yes/no` | Whether this swing has already registered a hit |
| `Sim steps/frame` | Fixed-step pacing (expect 1 at 60+ FPS) |
| `Accumulator alpha` | Render-interpolation blend factor |

---

## Debug Lines

| Color | Meaning |
|-------|---------|
| Green capsule | Player (grounded) |
| Orange capsule | Player (airborne) |
| Faint grey capsule | Render-interpolated position |
| Blue boxes | Static arena colliders + dummy |
| Red cross + arrow | Depenetration contact point + normal |
| Yellow cross | Ground probe hit |
| Red arrow | Player facing direction |
| **Red capsule** | Hit capsule — **inside** hit window |
| **Orange capsule** | Hit capsule — outside hit window (preview) |

---

## Balancing

All attack parameters are in `assets/character/combat_attacks.txt`.  
Edit the file and restart `movement_test` — no recompile needed.

Fields per attack:

```
attack <id> {
    clip       <AnimClipName>
    hit_window <start_normalized> <end_normalized>
    range      <meters>          # distance from player to hit-capsule center
    radius     <meters>          # hit-capsule radius
    recovery   <seconds>         # pause after last hit before Idle
}
```

---

## Known Limitations (v1)

- No HP, death, or loot.
- No enemy AI (dummy is static except for hit reaction).
- Camera does not collide with walls.
- No sliding or step-up in player movement.
- No coyote time or jump buffering.
- Skeleton retargeting across different rigs is not supported.
