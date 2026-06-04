# Phase A — Section-Occlusion-Skip — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Skip mesh jobs for empty or fully buried opaque sections while storing reusable `SectionRenderMeta` per section, conservatively correct at streaming borders.

**Architecture:** `Section::recompute_render_meta()` runs whenever occupancy is synced. `StreamingTerrainSystem::schedule_section_mesh` checks `is_empty` or `section_fully_occluded()` (cross-chunk via `neighbor_section`) and marks the section mesh-ready with zero indices instead of dispatching a job. Border block changes dirty adjacent chunks so previously buried neighbors remesh.

**Tech Stack:** C++20, flecs, Catch2, CMake/MSVC. Headless tests use `engine_core` + `#define private public` on `StreamingTerrainSystem` where needed.

**Spec:** `docs/superpowers/specs/2026-06-04-phaseA-section-occlusion-skip-design.md` (approved)

**Plan errata (rev. 2):** `face_solid_mask` must be computed for every non-empty section (not only `is_opaque_full`). `section_fully_occluded` checks `face_solid` on neighbors, which can be true on partial sections. Task 4 uses `neighbor_chunk_and_section` (section + chunk boundaries), not chunk-only offsets.

**Build/test commands (Developer PowerShell for VS 2022):**
```powershell
cmake -S . -B build
cmake --build build --config Debug --target engine_tests
ctest --test-dir build -C Debug -R "render meta|occlusion|greedy mesher border" --output-on-failure
cmake --build build --config Debug --target VoxelEngine
```

---

## Task 1: `SectionRenderMeta` + `recompute_render_meta()`

**Files:**
- Modify: `engine/world/Section.hpp`
- Modify: `engine/world/SectionIndexing.hpp` (helper only)
- Create: `tests/world/test_section_render_meta.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add struct + helper to `SectionIndexing.hpp`**

After `enum class Face`:

```cpp
struct SectionRenderMeta {
    bool    is_empty        = true;
    bool    is_opaque_full  = false;
    uint8_t face_solid_mask = 0;
};

inline bool face_solid(const SectionRenderMeta& m, Face f) {
    return (m.face_solid_mask >> static_cast<uint32_t>(f)) & 1u;
}

inline Face opposite_face(Face f) {
    return static_cast<Face>(static_cast<uint32_t>(f) ^ 1u);
}
```

- [ ] **Step 2: Extend `Section` in `Section.hpp`**

Add member:
```cpp
SectionRenderMeta render_meta{};
```

Add private helpers + public method:

```cpp
[[nodiscard]] static bool is_opaque_solid(BlockId id) {
    return is_solid(id) && !is_water(id);
}

[[nodiscard]] static bool is_renderable(BlockId id) {
    return is_solid(id) || is_water(id);
}

public:
void recompute_render_meta() {
    render_meta = {};
    if (palette.size() == 1 && block_id(palette[0]) == BLOCK_AIR) {
        render_meta.is_empty = true;
        return;
    }
    if (palette.size() == 1 && is_opaque_solid(block_id(palette[0]))) {
        render_meta.is_empty = false;
        render_meta.is_opaque_full = true;
        render_meta.face_solid_mask = 0x3Fu;
        return;
    }

    bool any_renderable = false;
    bool all_opaque = true;
    for (int y = 0; y < SECTION_DIM; ++y) {
        for (int z = 0; z < SECTION_DIM; ++z) {
            for (int x = 0; x < SECTION_DIM; ++x) {
                const BlockId id = block_id(read_block(x, y, z));
                if (is_renderable(id)) {
                    any_renderable = true;
                }
                if (!is_opaque_solid(id)) {
                    all_opaque = false;
                }
            }
        }
    }
    render_meta.is_empty = !any_renderable;
    render_meta.is_opaque_full = any_renderable && all_opaque;

    if (render_meta.is_empty) {
        return;
    }

    // face_solid_mask: always for non-empty sections (needed by section_fully_occluded
    // on neighbors that are not is_opaque_full but still present a solid face layer).
    auto face_layer_solid = [&](Face face) {
        switch (face) {
        case Face::PX:
            for (int y = 0; y < SECTION_DIM; ++y)
                for (int z = 0; z < SECTION_DIM; ++z)
                    if (!is_opaque_solid(block_id(read_block(SECTION_DIM - 1, y, z)))) return false;
            return true;
        case Face::NX:
            for (int y = 0; y < SECTION_DIM; ++y)
                for (int z = 0; z < SECTION_DIM; ++z)
                    if (!is_opaque_solid(block_id(read_block(0, y, z)))) return false;
            return true;
        case Face::PY:
            for (int x = 0; x < SECTION_DIM; ++x)
                for (int z = 0; z < SECTION_DIM; ++z)
                    if (!is_opaque_solid(block_id(read_block(x, SECTION_DIM - 1, z)))) return false;
            return true;
        case Face::NY:
            for (int x = 0; x < SECTION_DIM; ++x)
                for (int z = 0; z < SECTION_DIM; ++z)
                    if (!is_opaque_solid(block_id(read_block(x, 0, z)))) return false;
            return true;
        case Face::PZ:
            for (int x = 0; x < SECTION_DIM; ++x)
                for (int y = 0; y < SECTION_DIM; ++y)
                    if (!is_opaque_solid(block_id(read_block(x, y, SECTION_DIM - 1)))) return false;
            return true;
        case Face::NZ:
            for (int x = 0; x < SECTION_DIM; ++x)
                for (int y = 0; y < SECTION_DIM; ++y)
                    if (!is_opaque_solid(block_id(read_block(x, y, 0)))) return false;
            return true;
        }
        return false;
    };

    for (int f = 0; f < 6; ++f) {
        if (face_layer_solid(static_cast<Face>(f))) {
            render_meta.face_solid_mask |= static_cast<uint8_t>(1u << f);
        }
    }
}
```

Call at end of `sync_occupancy_from_blocks()`:
```cpp
recompute_render_meta();
```

- [ ] **Step 3: Register test file**

In `tests/CMakeLists.txt`, add `world/test_section_render_meta.cpp` to `engine_tests` sources.

- [ ] **Step 4: Write failing tests**

`tests/world/test_section_render_meta.cpp`:

```cpp
TEST_CASE("render meta air palette is empty") {
    engine::Section section;
    section.recompute_render_meta();
    REQUIRE(section.render_meta.is_empty);
    REQUIRE_FALSE(section.render_meta.is_opaque_full);
    REQUIRE(section.render_meta.face_solid_mask == 0);
}

TEST_CASE("render meta uniform stone is opaque full with all faces solid") {
    engine::Section section;
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int y = 0; y < engine::SECTION_DIM; ++y)
        for (int z = 0; z < engine::SECTION_DIM; ++z)
            for (int x = 0; x < engine::SECTION_DIM; ++x)
                REQUIRE(section.write_block(x, y, z, stone));
    section.sync_occupancy_from_blocks();
    REQUIRE_FALSE(section.render_meta.is_empty);
    REQUIRE(section.render_meta.is_opaque_full);
    REQUIRE(section.render_meta.face_solid_mask == 0x3F);
}

TEST_CASE("render meta water only is not empty") {
    engine::Section section;
    const engine::BlockState water = engine::make_block_state(engine::BLOCK_WATER, 0);
    REQUIRE(section.write_block(0, 0, 0, water));
    section.sync_occupancy_from_blocks();
    REQUIRE_FALSE(section.render_meta.is_empty);
    REQUIRE_FALSE(section.render_meta.is_opaque_full);
}

TEST_CASE("render meta mixed section sets single face solid bit") {
    engine::Section section;
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int z = 0; z < engine::SECTION_DIM; ++z)
        for (int x = 0; x < engine::SECTION_DIM; ++x)
            REQUIRE(section.write_block(x, 0, z, stone));
    section.sync_occupancy_from_blocks();
    REQUIRE(engine::face_solid(section.render_meta, engine::Face::NY));
    REQUIRE_FALSE(engine::face_solid(section.render_meta, engine::Face::PY));
}
```

- [ ] **Step 5: Run tests**

```powershell
ctest --test-dir build -C Debug -R "render meta" --output-on-failure
```
Expected: PASS

- [ ] **Step 6: Commit**

```powershell
git add engine/world/Section.hpp engine/world/SectionIndexing.hpp tests/world/test_section_render_meta.cpp tests/CMakeLists.txt
git commit -m "feat(world): add SectionRenderMeta and recompute_render_meta"
```

---

## Task 2: `section_fully_occluded` helper

**Files:**
- Modify: `engine/render/StreamingTerrainSystem.hpp`
- Modify: `engine/render/StreamingTerrainSystem.cpp`
- Create: `tests/render/test_section_occlusion.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add private method declaration**

In `StreamingTerrainSystem` private section:

```cpp
[[nodiscard]] bool section_fully_occluded(ChunkCoord coord, std::uint8_t section_index) const;
```

- [ ] **Step 2: Implement in `.cpp` (anonymous namespace optional)**

```cpp
bool StreamingTerrainSystem::section_fully_occluded(
    const ChunkCoord coord, const std::uint8_t section_index) const {
    if (store_ == nullptr) {
        return false;
    }
    const Chunk* chunk = store_->try_get(coord);
    if (chunk == nullptr || store_->is_pending_unload(coord)) {
        return false;
    }
    const Section& section = chunk->sections[section_index];
    const SectionRenderMeta& m = section.render_meta;
    if (m.is_empty) {
        return true;
    }
    if (!m.is_opaque_full) {
        return false;
    }
    const glm::ivec3 section_coord = section_coord_from_index(section_index);
    for (int fi = 0; fi < 6; ++fi) {
        const Face face = static_cast<Face>(fi);
        Section* neighbor = neighbor_section(*store_, coord, section_coord, face);
        if (neighbor == nullptr) {
            return false; // missing / unloading neighbor → conservative (render)
        }
        if (!face_solid(neighbor->render_meta, opposite_face(face))) {
            return false;
        }
    }
    return true;
}
```

Use existing `neighbor_section(store, chunk, section_coord, face)` from `BlockLight.hpp`. Recompute neighbor `render_meta` must already be current (sync on load/write).

- [ ] **Step 3: Add render tests (precise semantics)**

`section_fully_occluded` is true only when:
- center `is_empty` **or**
- center `is_opaque_full` **and** all 6 neighbors exist **and** each `face_solid(neighbor, opposite(face))`.

Neighbors do **not** need `is_opaque_full` — only the opposite face layer.

`tests/render/test_section_occlusion.cpp` with `#define private public`:

```cpp
static void fill_section_stone(engine::Section& section) {
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int y = 0; y < engine::SECTION_DIM; ++y)
        for (int z = 0; z < engine::SECTION_DIM; ++z)
            for (int x = 0; x < engine::SECTION_DIM; ++x)
                REQUIRE(section.write_block(x, y, z, stone));
    section.sync_occupancy_from_blocks();
}

TEST_CASE("section_fully_occluded false when center not opaque full") {
    engine::ChunkStore store;
    store.init(4);
    engine::Chunk* chunk = store.allocate({0, 0, 0});
    REQUIRE(chunk != nullptr);
    // Floor layer only — is_opaque_full false, but NY face solid
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int z = 0; z < engine::SECTION_DIM; ++z)
        for (int x = 0; x < engine::SECTION_DIM; ++x)
            REQUIRE(chunk->section_at({0, 0, 0}).write_block(x, 0, z, stone));
    chunk->section_at({0, 0, 0}).sync_occupancy_from_blocks();
    REQUIRE(engine::face_solid(chunk->section_at({0, 0, 0}).render_meta, engine::Face::NY));

    engine::StreamingTerrainSystem streaming;
    // init minimal: store_ pointer only needed — set store_ = &store in test via init()
    // ...
    REQUIRE_FALSE(streaming.section_fully_occluded({0, 0, 0}, 0));
}

TEST_CASE("section_fully_occluded true when buried in stone shell") {
    engine::ChunkStore store;
    store.init(8);
    engine::Chunk* interior = store.allocate({0, 0, 0});
    engine::Chunk* shell_px  = store.allocate({1, 0, 0});
    REQUIRE(interior != nullptr);
    REQUIRE(shell_px != nullptr);
    fill_section_stone(interior->section_at({0, 0, 0}));
    fill_section_stone(shell_px->section_at({0, 0, 0}));
    // Also allocate -X,-Y,-Z,-Z chunk neighbors with full stone sections (test setup helper)
    // refresh borders on interior section 0
    engine::refresh_section_border_cache(store, {0, 0, 0}, {0, 0, 0});
    // After all 6 face neighbors loaded + full stone opposite faces:
    REQUIRE(streaming.section_fully_occluded({0, 0, 0}, 0));
}

TEST_CASE("section_fully_occluded false when neighbor opposite face has air") {
    // interior chunk full stone; shell_px chunk has solid NX layer only at x=0, rest air
    // opposite face from interior PX view = neighbor NX — still solid
    // shell_py chunk: air section → interior PY opposite NY not solid → false
}
```

Implement the shell setup with 6 neighbor chunks OR use border `BorderCell` + in-chunk neighbors where possible; prefer real `neighbor_section` resolution over border-only hacks.

- [ ] **Step 4: Run tests**

```powershell
ctest --test-dir build -C Debug -R "section occlusion" --output-on-failure
```

- [ ] **Step 5: Commit**

```powershell
git add engine/render/StreamingTerrainSystem.hpp engine/render/StreamingTerrainSystem.cpp tests/render/test_section_occlusion.cpp tests/CMakeLists.txt
git commit -m "feat(render): add section_fully_occluded cross-chunk helper"
```

---

## Task 3: Skip path in `schedule_section_mesh`

**Files:**
- Modify: `engine/render/StreamingTerrainSystem.hpp`
- Modify: `engine/render/StreamingTerrainSystem.cpp`
- Modify: `tests/render/test_section_occlusion.cpp`

- [ ] **Step 1: Extend `SectionMeshState`**

```cpp
bool occluded_skip = false;
```

Reset `occluded_skip = false` in `soft_invalidate_chunk_mesh` and `invalidate_chunk_mesh` per section.

- [ ] **Step 2: Add `mark_section_mesh_skipped` private helper**

```cpp
void StreamingTerrainSystem::mark_section_mesh_skipped(SectionMeshState& section_state) {
    section_state.mesh_ready = true;
    section_state.mesh_job_pending = false;
    section_state.needs_remesh = false;
    section_state.occluded_skip = true;
    section_state.opaque_vertices.clear();
    section_state.opaque_indices.clear();
    section_state.water_vertices.clear();
    section_state.water_indices.clear();
    section_state.opaque_index_count = 0;
    section_state.water_index_count = 0;
    section_state.opaque_draw_index_count = 0;
    section_state.water_draw_index_count = 0;
}
```

- [ ] **Step 3: Early-out in `schedule_section_mesh`**

After loading `chunk` and `section_index`, before `mesh_job_pending = true`:

```cpp
const Section& live_section = chunk->sections[section_index];
if (live_section.render_meta.is_empty || section_fully_occluded(coord, section_index)) {
    mark_section_mesh_skipped(section_state);
    return;
}
section_state.occluded_skip = false;
```

Ensure `live_section.render_meta` is fresh: call `chunk->sections[section_index].recompute_render_meta()` if dirty palette path might skip sync — safe to call `recompute_render_meta()` here once per schedule (cheap with fast paths).

- [ ] **Step 4: Test — empty section does not increment pending jobs**

```cpp
TEST_CASE("schedule_section_mesh skips empty section without pending job") {
    // allocate chunk, leave section air, init streaming + chunk_meshes entry
    streaming.schedule_section_mesh(coord, 0);
    REQUIRE_FALSE(streaming.count_pending_mesh_jobs() > 0); // or section mesh_job_pending false
    REQUIRE(chunk_meshes_[coord].sections[0].mesh_ready);
    REQUIRE(chunk_meshes_[coord].sections[0].occluded_skip);
}
```

- [ ] **Step 5: Run tests + commit**

```powershell
git add engine/render/StreamingTerrainSystem.hpp engine/render/StreamingTerrainSystem.cpp tests/render/test_section_occlusion.cpp
git commit -m "feat(render): skip mesh jobs for empty and fully occluded sections"
```

---

## Task 4: Border invalidation + buried-neighbor remesh

**Files:**
- Modify: `engine/world/BlockLight.hpp` (export existing helper)
- Modify: `engine/gameplay/BlockInteraction.cpp`
- Modify: `engine/world/ChunkStore.cpp`

- [ ] **Step 1: Export `neighbor_chunk_and_section`**

Already implemented in `BlockLight.cpp` (handles **section** and **chunk** boundaries). Add to `BlockLight.hpp`:

```cpp
void neighbor_chunk_and_section(
    ChunkCoord chunk,
    glm::ivec3 section_coord,
    Face face,
    ChunkCoord& out_chunk,
    glm::ivec3& out_section);
```

Do **not** add `adjacent_chunk_for_face` — it only shifts chunk coords and breaks mutations on `x=15`/`x=0` section faces inside the same chunk.

- [ ] **Step 2: Helper — block on section face boundary**

In `BlockInteraction.cpp` anonymous namespace:

```cpp
[[nodiscard]] bool block_on_section_face(const BlockPos& pos, Face& out_face) {
    const glm::ivec3 blk = pos.block_in_section();
    if (blk.x == 0) { out_face = Face::NX; return true; }
    if (blk.x == SECTION_DIM - 1) { out_face = Face::PX; return true; }
    if (blk.y == 0) { out_face = Face::NY; return true; }
    if (blk.y == SECTION_DIM - 1) { out_face = Face::PY; return true; }
    if (blk.z == 0) { out_face = Face::NZ; return true; }
    if (blk.z == SECTION_DIM - 1) { out_face = Face::PZ; return true; }
    return false;
}
```

- [ ] **Step 3: After successful break/place `write_block`**

When solid occupancy changed (`was_solid != now_solid`):

```cpp
Face boundary_face{};
if (block_on_section_face(mutation.pos, boundary_face)) {
    ChunkCoord neighbor_chunk{};
    glm::ivec3 neighbor_section{};
    neighbor_chunk_and_section(
        mutation.pos.chunk,
        mutation.pos.section_coord(),
        boundary_face,
        neighbor_chunk,
        neighbor_section);
    mark_chunk_dirty(world, store, neighbor_chunk);
}
```

`mark_chunk_dirty` on the mutating chunk already happens elsewhere; this adds the **across-face** chunk/section neighbor.

- [ ] **Step 4: `recompute_render_meta` in `ChunkStore::write_block`**

After occupancy update:

```cpp
section.recompute_render_meta();
```

- [ ] **Step 5: Integration test**

Extend `tests/render/test_section_occlusion.cpp` or `tests/gameplay/test_block_events.cpp`:

Break border block on chunk boundary ⇒ neighbor chunk gets remesh scheduled (`needs_remesh` or pending job on neighbor section).

- [ ] **Step 6: Commit**

```powershell
git add engine/world/BlockLight.hpp engine/gameplay/BlockInteraction.cpp engine/world/ChunkStore.cpp tests/
git commit -m "fix(gameplay): dirty neighbor chunk across section face on solid change"
```

---

## Task 5: Greedy mesher regression + L-corner exposure test

**Files:**
- Modify: `tests/world/test_greedy_mesher.cpp`

- [ ] **Step 1: Fully buried interior with one air neighbor face**

Two chunks or one chunk with border air: mesh produces only exposed quads (face count < 6 for 1³ exposed).

- [ ] **Step 2: Run mesher tests**

```powershell
ctest --test-dir build -C Debug -R "greedy mesher" --output-on-failure
```

- [ ] **Step 3: Commit**

```powershell
git commit -m "test(mesh): occlusion exposure produces limited faces"
```

---

## Task 6: Observability (ImGui)

**Files:**
- Modify: `engine/render/StreamingTerrainSystem.hpp`
- Modify: `engine/render/StreamingTerrainSystem.cpp`
- Modify: `engine/ui/UiHost.hpp`
- Modify: `engine/ui/UiHost.cpp`
- Modify: `engine/Engine.cpp`

- [ ] **Step 1: Add counter**

```cpp
[[nodiscard]] std::size_t count_occluded_sections() const;
```

Count sections where `occluded_skip && mesh_ready`.

- [ ] **Step 2: UiOverlayStats field**

```cpp
std::uint32_t occluded_skip_sections = 0;
```

Display: `ImGui::Text("Occluded skip: %u", stats.occluded_skip_sections);`

Wire in `Engine.cpp` where other mesh stats are filled.

- [ ] **Step 3: Commit**

```powershell
git commit -m "feat(ui): show occluded section skip count in debug overlay"
```

---

## Task 7: Optional `build_snapshot` fast-path + smoke

**Files:**
- Modify: `engine/render/StreamingTerrainSystem.cpp` (only if profiling shows benefit)

- [ ] **Step 1: Early continue when `occluded_skip`**

In `build_snapshot` section loop, skip GPU liveness checks when `section_state.occluded_skip`.

- [ ] **Step 2: Manual smoke**

Run `VoxelEngine.exe`, dig toward buried stone, watch ImGui: `Occluded skip` rises underground; breaking surface face increases mesh jobs / exposed geometry.

- [ ] **Step 3: Full test sweep**

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

- [ ] **Step 4: Commit + update roadmap**

Mark Phase A implemented in plan notes if desired.

---

## Spec coverage checklist

| Spec | Task |
|------|------|
| A1 `SectionRenderMeta` | Task 1 |
| A2 fast paths + full scan | Task 1 |
| A3 `section_fully_occluded` | Task 2 |
| A4 schedule skip + zero geom | Task 3 |
| A4 invalidation | Task 4 |
| A5 tests 1–7 | Tasks 1–5 |
| A6 ImGui counter | Task 6 |
| A7 non-goals | No BFS/LOD/GPU |

---

## File map

| File | Change |
|------|--------|
| `SectionIndexing.hpp` | `SectionRenderMeta`, `face_solid`, `opposite_face` |
| `Section.hpp` | `render_meta`, `recompute_render_meta`, hook in `sync_occupancy` |
| `ChunkStore.cpp` | `recompute_render_meta` on `write_block` |
| `StreamingTerrainSystem.*` | occluded skip, schedule early-out, counter |
| `BlockInteraction.cpp` | neighbor `ChunkDirty` on boundary solid change |
| `UiHost.*` / `Engine.cpp` | overlay stat |
| `test_section_render_meta.cpp` | meta unit tests |
| `test_section_occlusion.cpp` | occlusion + schedule tests |