# Phase D — Chunk LOD — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Distance-based LOD0 (sections) vs LOD1 (coarse per-chunk opaque mesh) with water/shoreline forced LOD0, preset-driven draw distances, and ImGui metrics — optional color impostors at horizon (D.2).

**Architecture:** Downsample 2×2×2 fine voxels inside each LOD0 chunk into a 16³ grid; `mesh_chunk_lod1` reuses greedy axis logic with stride-2 sampling. `StreamingTerrainSystem` owns parallel LOD1 mesh/GPU state; `build_snapshot` picks per-chunk LOD using hysteresis + `ChunkRenderMeta::has_water` + neighbor water buffer. Snapshot contract unchanged (no `ChunkStore*` in snapshot).

**Tech Stack:** C++20, flecs, Vulkan indirect draw, Catch2, CMake/MSVC.

**Spec:** `docs/superpowers/specs/2026-06-04-phaseD-chunk-lod-design.md` (**Implemented 2026-06-04**)

**Plan errata (rev. 2, aligns with spec review):**
- LOD1 vertices: **coarse 0..16** in `pack_vertex`; world extent via **`vertex_scale = 2`** in push constants + `terrain.vert` — never pack 32.
- LOD1 mesher: **no neighbor chunk culling** (exterior faces always; overdraw OK).
- LOD transitions: draw fallback until replacement **`gpu_uploaded`**; no early GPU free.
- Streaming-edge LOD0 and water-border LOD0 are **separate** rules (tests 12–13).

**Build/test commands (Developer PowerShell for VS 2022):**
```powershell
cmake -S . -B build
cmake --build build --config Debug --target engine_tests
ctest --test-dir build -C Debug -R "chunk lod|lod1|section_mesh_distance|section occlusion|greedy mesher" --output-on-failure
cmake --build build --config Debug --target VoxelEngine
```

---

## Task 0: `pack_vertex` LOD contract (Blocker)

**Files:**
- Create: `tests/world/test_pack_vertex_lod.cpp`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1:** Assert `pack_vertex(16,16,16)` unpacks as 16; `pack_vertex(32,0,0) & POS_MASK == 0`
- [x] **Step 2:** Document in `SectionIndexing.hpp` comment: LOD1 uses 0..16 + draw scale 2

---

## Task 1: Types + distance helpers

**Files:**
- Modify: `engine/world/SectionIndexing.hpp` (or new `engine/world/TerrainLod.hpp`)
- Create: `tests/world/test_chunk_lod_selection.cpp`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1:** Add `TerrainLodLevel`, `chunk_horizontal_distance_sq`, default thresholds struct `TerrainLodConfig`
- [x] **Step 2:** `select_chunk_lod(dist_sq, prev_lod, config)` with hysteresis — unit tests 5–7
- [x] **Step 3:** Build + `ctest -R "chunk lod selection"`

---

## Task 2: `ChunkRenderMeta` + water / shoreline

**Files:**
- Modify: `engine/world/Chunk.hpp`
- Modify: `engine/world/ChunkStore.cpp` (recompute on load/write)
- Extend: `tests/world/test_chunk_lod_selection.cpp`

- [x] **Step 1:** `ChunkRenderMeta { bool has_water }` + `recompute_chunk_render_meta(Chunk&)`
- [x] **Step 2:** `chunk_force_lod0_water_border(store, coord)` — self or **loaded** XZ neighbor has water (missing neighbor ≠ water)
- [x] **Step 3:** `chunk_requires_lod0_streaming_edge(store, coord)` — incomplete border / missing neighbor chunk / pending unload
- [x] **Step 4:** Tests 3, 7, 8, 12 (streaming-edge vs water independent)
- [x] **Step 5:** Wire recompute after worldgen fill and `write_block` when block is water or palette changes

---

## Task 3: `mesh_chunk_lod1` (downsample + greedy)

**Files:**
- Create: `engine/world/ChunkLodMesher.cpp` (+ `.hpp`) or extend `GreedyMesher.cpp`
- Create: `tests/world/test_chunk_lod_mesher.cpp`
- Modify: `engine/CMakeLists.txt`, `tests/CMakeLists.txt`

- [x] **Step 1:** Internal `CoarseCell { opaque, water, sky, block }` from 2³ fine samples across sections (own chunk only)
- [x] **Step 2:** Greedy on 16³ coarse grid; `pack_vertex` coords **0..16 only** (D5.1)
- [x] **Step 3:** `vert.ao = 3`, light = mean nibbles; no water quads
- [x] **Step 4:** D5.2: solid coarse on chunk exterior ⇒ face (no neighbor culling)
- [x] **Step 5:** Tests 1, 2, 9, 10, 11
- [x] **Step 6:** `ctest -R "chunk lod mesher|pack_vertex"`

---

## Task 4: `StreamingTerrainSystem` LOD1 state machine

**Files:**
- Modify: `engine/render/StreamingTerrainSystem.hpp`
- Modify: `engine/render/StreamingTerrainSystem.cpp`
- Create: `tests/render/test_chunk_lod_streaming.cpp`

- [x] **Step 1:** `ChunkLod1MeshState` + extend `ChunkMeshState`
- [x] **Step 2:** `schedule_chunk_lod1_mesh`, `process_lod1_mesh_backlog` (cap `kMaxPendingLod1MeshJobs`)
- [x] **Step 3:** Completion drain, GPU alloc/upload (reuse Phase C section-distance sort; LOD1 jobs lower priority than LOD0)
- [x] **Step 4:** Stale slots for lod1 + sections; free opposing GPU **only after** replacement `gpu_uploaded` (D5.3)
- [x] **Step 5:** `empty_skip` for empty coarse chunk
- [x] **Step 6:** Public test hooks: `select_chunk_lod_for_coord`, `schedule_chunk_lod1_mesh` if needed
- [x] **Step 7:** Headless test: schedule LOD1 at distance, verify job completes

---

## Task 5: Shader scale + `build_snapshot` + `DrawSection`

**Files:**
- Modify: `shaders/terrain.vert`, `shaders/water.vert`
- Modify: `engine/render/TerrainPass.hpp`, `TerrainPass.cpp`, `WaterPass.hpp`, `WaterPass.cpp`
- Modify: `engine/render/WorldRenderSnapshot.hpp`
- Modify: `engine/render/StreamingTerrainSystem.cpp`

- [x] **Step 1:** `DrawPushConstants { vec3 model_translation; float vertex_scale; }` — default 1.0
- [x] **Step 2:** `DrawSection::lod_level` + `vertex_scale` (2 for LOD1)
- [x] **Step 3:** `build_snapshot` draw selection per D5.3 (desired_lod vs uploaded fallback)
- [x] **Step 4:** Streaming-edge / water-border gates before LOD1-only draw
- [x] **Step 5:** Test 13 (LOD1 pending ⇒ still LOD0 sections drawn)
- [x] **Step 6:** `kMaxDrawDistance` + `kMaxLod1DrawChunks` from config
- [x] **Step 7:** Regression `ctest` filter from plan header

---

## Task 6: Presets + observability

**Files:**
- Modify: `engine/core/EngineConfig.hpp` / `.cpp`
- Modify: `engine/world/StreamingConfig.hpp` / `.cpp` (optional)
- Modify: ImGui overlay site (grep `count_occluded_skip`)

- [x] **Step 1:** Map `RenderPreset` → `TerrainLodConfig` defaults (spec D7 table)
- [x] **Step 2:** Counters: `lod1_draw_chunks`, `pending_lod1_mesh_jobs`, `water_border_lod0_forced`
- [x] **Step 3:** Manual: fly out — verify ImGui LOD1 > 0, shoreline stays detailed

---

## Task 7 (optional): D.2 Color impostors

**Files:**
- New: `engine/render/TerrainImpostorPass.*` or extend `TerrainPass`
- Shaders: `shaders/impostor.vert`, `impostor.frag`

- [ ] **Step 1:** `compute_chunk_impostor_color(Chunk&)` — dominant surface material / height range
- [ ] **Step 2:** Draw path in `build_snapshot` for `TerrainLodLevel::Impostor`
- [ ] **Step 3:** Extend draw distance to 32 chunks on High preset; ImGui `impostor_draw_chunks`

Skip this task if scope/time constrained — document in commit message.

---

## Task 8: Docs + roadmap

**Files:**
- Modify: `docs/superpowers/specs/2026-06-04-phaseD-chunk-lod-design.md` (status → Implemented + commit)
- Modify: `docs/superpowers/specs/2026-06-04-rendering-performance-roadmap-design.md`

- [x] **Step 1:** After merge, update roadmap Phase D row + audit #8
- [x] **Step 2:** Spec status + commit hash

---

## Verification checklist (before “done”)

- [x] Filtered `ctest` green (~25 s)
- [ ] No new holes at streaming border (manual short walk)
- [x] Water chunk / loaded neighbor water → LOD0; streaming-edge → LOD0 draw (may coexist)
- [x] `pack_vertex` / `vertex_scale` — no coordinate > 16 in LOD1 meshes
- [ ] ImGui shows LOD1 draws when camera > `lod0_far`