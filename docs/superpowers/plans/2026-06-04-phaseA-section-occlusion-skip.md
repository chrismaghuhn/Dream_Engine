# Phase A — Section-Occlusion-Skip — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Skip mesh jobs for empty or fully buried opaque sections while storing reusable `SectionRenderMeta` per section, conservatively correct at streaming borders.

**Architecture:** `recompute_render_meta()` runs on sync/load/`ChunkStore::write_block` — not in `schedule_section_mesh`. Scheduling only reads `render_meta` and skips mesh jobs for empty vs fully occluded (separate flags). Cross-chunk `ChunkDirty` only when a section-face mutation crosses a chunk boundary; intra-chunk section neighbors are covered by dirtying the mutating chunk.

**Tech Stack:** C++20, flecs, Catch2, CMake/MSVC. Headless tests use `engine_core` + `#define private public` on `StreamingTerrainSystem` where needed.

**Spec:** `docs/superpowers/specs/2026-06-04-phaseA-section-occlusion-skip-design.md` (implemented)

**Implementation:** `000de64` (2026-06-04). Headless tests: `render meta`, `section occlusion`, `break on chunk face`.

**Plan errata (rev. 3):**
- `face_solid_mask` for every non-empty section (not only `is_opaque_full`).
- Task 4: cross-**chunk** dirty only when `neighbor_chunk != mutation.chunk`; same-chunk section neighbors rely on existing `ChunkDirty` on the mutating chunk. Corners: check every touched section face.
- `schedule_section_mesh` **reads** `render_meta` only — no `recompute_render_meta()` on schedule path.
- Separate `empty_skip` vs `occluded_skip` counters (not one combined flag).

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

- [ ] **Step 1: Extend `SectionMeshState` (two skip flags)**

```cpp
bool empty_skip = false;
bool occluded_skip = false;
```

Reset both to `false` in `soft_invalidate_chunk_mesh` and `invalidate_chunk_mesh`.

- [ ] **Step 2: Add `mark_section_mesh_skipped` private helper**

```cpp
enum class SectionMeshSkipKind { Empty, FullyOccluded };

void StreamingTerrainSystem::mark_section_mesh_skipped(
    SectionMeshState& section_state, SectionMeshSkipKind kind) {
    section_state.mesh_ready = true;
    section_state.mesh_job_pending = false;
    section_state.needs_remesh = false;
    section_state.empty_skip = (kind == SectionMeshSkipKind::Empty);
    section_state.occluded_skip = (kind == SectionMeshSkipKind::FullyOccluded);
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

- [ ] **Step 3: Early-out in `schedule_section_mesh` (read-only meta)**

After loading `chunk` and `section_index`, before `mesh_job_pending = true`:

```cpp
const Section& live_section = chunk->sections[section_index];
// render_meta must already be current (sync_occupancy / write_block / load paths).
#ifndef NDEBUG
    // Optional: assert palette/occupancy consistency, or compare against a one-off recompute.
#endif
if (live_section.render_meta.is_empty) {
    mark_section_mesh_skipped(section_state, SectionMeshSkipKind::Empty);
    return;
}
if (section_fully_occluded(coord, section_index)) {
    mark_section_mesh_skipped(section_state, SectionMeshSkipKind::FullyOccluded);
    return;
}
section_state.empty_skip = false;
section_state.occluded_skip = false;
```

**Do not** call `recompute_render_meta()` here (`live_section` is const; scheduling must not full-scan).

- [ ] **Step 4: Tests — empty vs occluded skip flags**

```cpp
TEST_CASE("schedule_section_mesh skips empty section with empty_skip") {
    streaming.schedule_section_mesh(coord, air_section_index);
    REQUIRE(chunk_meshes_[coord].sections[i].mesh_ready);
    REQUIRE(chunk_meshes_[coord].sections[i].empty_skip);
    REQUIRE_FALSE(chunk_meshes_[coord].sections[i].occluded_skip);
}

TEST_CASE("schedule_section_mesh skips buried section with occluded_skip") {
    // stone shell setup ...
    REQUIRE(chunk_meshes_[coord].sections[i].occluded_skip);
    REQUIRE_FALSE(chunk_meshes_[coord].sections[i].empty_skip);
}
```

- [ ] **Step 5: Run tests + commit**

```powershell
git add engine/render/StreamingTerrainSystem.hpp engine/render/StreamingTerrainSystem.cpp tests/render/test_section_occlusion.cpp
git commit -m "feat(render): skip mesh jobs for empty and fully occluded sections"
```

---

## Task 4: Invalidation (section vs chunk boundary)

**Files:**
- Modify: `engine/world/BlockLight.hpp` (export existing helper)
- Modify: `engine/gameplay/BlockInteraction.cpp`
- Modify: `engine/world/ChunkStore.cpp`

**Rules:**

| Situation | Action |
|-----------|--------|
| Block mutation (any) | `mark_chunk_dirty(mutation.chunk)` — already via `ChunkDirty`; remeshes all 8 sections in chunk |
| Block on **section** face, neighbor in **same chunk** | No extra dirty — covered by row above (e.g. `section_y=1` NY → `section_y=0` same chunk) |
| Block on section face, neighbor in **different chunk** | `mark_chunk_dirty(neighbor_chunk)` |
| Block on **section corner/edge** (multiple faces) | For **each** axis where `blk` is `0` or `15`, resolve neighbor; dirty **distinct** neighbor chunks only |

- [ ] **Step 1: Export `neighbor_chunk_and_section`**

Add declaration to `BlockLight.hpp` (implementation already in `BlockLight.cpp`).

- [ ] **Step 2: Helpers in `BlockInteraction.cpp`**

```cpp
// Returns all section faces touched by this block position (1–3 faces at corners/edges).
void section_faces_at_block(const BlockPos& pos, std::array<Face, 3>& faces, int& face_count) {
    const glm::ivec3 blk = pos.block_in_section();
    face_count = 0;
    if (blk.x == 0) faces[face_count++] = Face::NX;
    if (blk.x == SECTION_DIM - 1) faces[face_count++] = Face::PX;
    if (blk.y == 0) faces[face_count++] = Face::NY;
    if (blk.y == SECTION_DIM - 1) faces[face_count++] = Face::PY;
    if (blk.z == 0) faces[face_count++] = Face::NZ;
    if (blk.z == SECTION_DIM - 1) faces[face_count++] = Face::PZ;
}

void mark_cross_chunk_occlusion_neighbors_dirty(
    flecs::world& world, ChunkStore& store, const BlockPos& pos) {
    std::array<Face, 3> faces{};
    int face_count = 0;
    section_faces_at_block(pos, faces, face_count);
    for (int i = 0; i < face_count; ++i) {
        ChunkCoord neighbor_chunk{};
        glm::ivec3 neighbor_section{};
        neighbor_chunk_and_section(pos.chunk, pos.section_coord(), faces[i],
                                   neighbor_chunk, neighbor_section);
        if (neighbor_chunk != pos.chunk) {
            mark_chunk_dirty(world, store, neighbor_chunk);
        }
    }
}
```

- [ ] **Step 3: After successful break/place when solid changed**

```cpp
mark_chunk_dirty(world, store, mutation.pos.chunk); // if not already guaranteed
if (was_solid != now_solid) {
    mark_cross_chunk_occlusion_neighbors_dirty(world, store, mutation.pos);
}
```

- [ ] **Step 4: `recompute_render_meta` in `ChunkStore::write_block`**

After occupancy update:

```cpp
section.recompute_render_meta();
```

- [ ] **Step 5: Integration tests**

1. Break block on **chunk** face (`x=0` in section at `section_coord.x==0`) ⇒ neighbor chunk `ChunkDirty` / remesh.
2. Break block on **intra-chunk** section face (`y=16` boundary between sections) ⇒ mutating chunk remeshes; **no** requirement that a second chunk is dirtied.
3. Corner on chunk edge ⇒ two distinct neighbor chunks dirtied when applicable.

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

- [ ] **Step 1: Add counters (separate empty vs occluded)**

```cpp
[[nodiscard]] std::size_t count_empty_skip_sections() const;
[[nodiscard]] std::size_t count_occluded_skip_sections() const;
```

- `empty_skip && mesh_ready`
- `occluded_skip && mesh_ready`

- [ ] **Step 2: UiOverlayStats + overlay**

```cpp
std::uint32_t empty_skip_sections = 0;
std::uint32_t occluded_skip_sections = 0;
```

```cpp
ImGui::Text("Empty skip: %u", stats.empty_skip_sections);
ImGui::Text("Occluded skip: %u", stats.occluded_skip_sections);
```

- [ ] **Step 3: Commit**

```powershell
git commit -m "feat(ui): separate empty and occluded mesh skip counters"
```

---

## Task 7: Optional `build_snapshot` fast-path + smoke

**Files:**
- Modify: `engine/render/StreamingTerrainSystem.cpp` (only if profiling shows benefit)

- [ ] **Step 1: Early continue when skipped**

In `build_snapshot` section loop, skip GPU liveness checks when `empty_skip || occluded_skip`.

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
| A6 ImGui counters (empty + occluded) | Task 6 |
| recompute only on write/sync/load | Task 1 + 4, not Task 3 |
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