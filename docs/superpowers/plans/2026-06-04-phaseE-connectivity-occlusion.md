# Phase E — Connectivity Occlusion — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** BFS section visibility from camera; skip opaque draws not in visible set **only when BFS completed without truncate**; water and streaming-edge unchanged.

**Architecture:** Portal edges from `face_solid_mask`; `connectivity_allows_draw()` is the **only** draw gate; `truncated` or `skipped_no_seed` disables culling for the whole snapshot.

**Tech Stack:** C++20, Catch2, CMake/MSVC, `glm::floor` for world→section keys.

**Spec:** `docs/superpowers/specs/2026-06-04-phaseE-connectivity-occlusion-design.md` — **implemented** (`f3f337f`, `1a37719`)

**Plan errata (rev. 3):**
- **Task 0:** Phase A gate — `face_solid_mask` on all non-empty sections (IST already correct; tests must pass).
- **Never** `if (!visible.contains(k)) skip` without `connectivity_allows_draw()`.
- **`truncated`:** connectivity culling **off** for entire snapshot (not per-section conservative draw).
- **Streaming-edge chunk:** no connectivity cull on **any** opaque LOD0 section **or** LOD1 draw in that chunk.
- **`section_key_from_world`:** `glm::floor`; tests at chunk boundaries and negative coords.
- Default BFS cap: **8192** (High **16384**); watch ImGui `connectivity_bfs_truncated`.

**Build/test commands:**
```powershell
cmake -S . -B build
cmake --build build --config Debug --target engine_tests
ctest --test-dir build -C Debug -R "section render meta|section visibility|section portal|section_fully_occluded|chunk lod" --output-on-failure
```

---

## Task 0: Phase A gate (Blocker)

**Files:**
- Verify: `engine/world/Section.hpp` (`recompute_render_meta` face loop for all non-empty)
- Run: `tests/world/test_section_render_meta.cpp`

- [x] **Step 1:** Confirm `face_solid_mask` computed whenever `!is_empty` (not gated on `is_opaque_full` alone)
- [x] **Step 2:** `ctest -R "section render meta"` — must pass before any E code
- [x] **Step 3:** If failing, fix Phase A first; do not start Task 1

---

## Task 1: Portal helpers + unit tests

**Files:**
- Create: `engine/render/SectionVisibility.hpp`
- Create: `engine/render/SectionVisibility.cpp` (portal helpers only)
- Create: `tests/render/test_section_portal.cpp`
- Modify: `engine/CMakeLists.txt`, `tests/CMakeLists.txt`

- [x] **Step 1:** `section_face_has_portal`, `sections_connected_portal`
- [x] **Step 2:** Tests 1–4
- [x] **Step 3:** `ctest -R "section portal"`

---

## Task 2: BFS + `section_key_from_world` + draw helpers

**Files:**
- Extend: `SectionVisibility.cpp`
- Create: `tests/render/test_section_visibility.cpp`

- [x] **Step 1:** `SectionVisKey`, hash, `section_key_from_world` with **`glm::floor`**
- [x] **Step 2:** Tests 8 — focus points `(0,0,0)`, `(31.9,0,31.9)`, `(32,0,32)`, `(-0.1,0,-0.1)`, `(-32,0,-32)`
- [x] **Step 3:** `compute_section_visibility` BFS; set `ran_bfs=true` on success path
- [x] **Step 4:** Cap → `truncated=true` (do **not** add unvisited sections to a “culled” set)
- [x] **Step 5:** `connectivity_culling_active`, `connectivity_allows_draw` (spec E3)
- [x] **Step 6:** Tests 5–7, 9 (`truncated` ⇒ all keys allowed)
- [x] **Step 7:** `ctest -R "section visibility"`

---

## Task 3: `build_snapshot` integration

**Files:**
- Modify: `engine/render/StreamingTerrainSystem.hpp/.cpp`
- Extend: `tests/render/test_section_visibility.cpp`

- [x] **Step 1:** `TerrainOcclusionConfig` + preset caps (Low 4096 / Med 8192 / High 16384)
- [x] **Step 2:** One `compute_section_visibility` per `build_snapshot` when enabled
- [x] **Step 3:** **`append_lod0_opaque_draws`:** if `chunk_requires_lod0_streaming_edge(coord)` → **never** connectivity-skip; else only `connectivity_allows_draw`
- [x] **Step 4:** **LOD1:** if streaming-edge chunk → no connectivity filter on `emit_lod1`; else require ≥1 visible section when `connectivity_culling_active`
- [x] **Step 5:** Water pass untouched
- [x] **Step 6:** Tests 10, 12 (isolated opaque culled; streaming-edge still draws)
- [x] **Step 7:** Grep codebase — **no** raw `!visibility.visible.contains` outside `connectivity_allows_draw`

---

## Task 4: Observability + config

**Files:**
- Modify: `engine/Engine.cpp`, `engine/ui/UiHost.hpp/.cpp`, `engine/core/EngineConfig.hpp/.cpp`

- [x] **Step 1:** Counters: `connectivity_visible_sections`, `connectivity_culled_sections`, `connectivity_bfs_truncated`
- [x] **Step 2:** ImGui — highlight if truncated every frame (document in commit / debug tip)
- [x] **Step 3:** `TerrainOcclusionConfig::enabled` debug toggle (optional)

---

## Task 5: Docs + roadmap

- [x] Spec → Implemented + commits
- [x] Roadmap audit #9 after E.1

---

## Task 6 (optional): E.2 mesh schedule skip

Skip unless E.1 profiled stable.

---

## Verification checklist

- [ ] Task 0 gate green
- [ ] `truncated` never causes missing terrain (manual + test 9)
- [ ] Streaming-edge walk: no holes
- [ ] ImGui: culled > 0 underground; truncated **not** always true at High preset
- [ ] Filtered `ctest` green