# Voxel Engine Phase 1 (M0–M10) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a playable single-player voxel survival prototype (M0–M10) per `docs/superpowers/specs/2026-06-03-voxel-engine-design.md` (v9): C++/Vulkan 1.2, 32³ chunks, fixed 60 Hz sim, thin renderer first, vertical slice at M5.

**Architecture:** Single CMake monorepo (`engine/` libraries + `game/main.cpp`). Strict §3 init order; sim/render split via `WorldRenderSnapshot` (§5); world data in `ChunkStore` with palette sections; GPU uploads only through `MeshUploadQueue`. Hardware-derived budgets via two-phase `finalize_cpu` / `finalize_gpu`.

**Tech Stack:** MSVC, CMake 3.24+, Vulkan 1.2 (volk + VMA), GLFW, GLM, Flecs, enkiTS, toml++, Catch2, spdlog, Tracy (optional). **Dependencies are staged by milestone** (see table below) so first-build failures stay localized.

**Spec reference:** `docs/superpowers/specs/2026-06-03-voxel-engine-design.md`

### Dependency staging (FetchContent — do not pull everything in P0)

| Milestone | Add to `cmake/Dependencies.cmake` |
|-----------|-------------------------------------|
| **P0** | glm, spdlog, Catch2 |
| **M0-4** | toml++ |
| **M0-7** | flecs, enkiTS, glfw |
| **M0-8** | volk, VMA, Vulkan SDK (`find_package(Vulkan)`) |
| **M3-4** | imgui |
| **M5b / M10** | FastNoise2 (M10 replaces heightmap path) |
| **M6** | Jolt |
| **M8** | zstd |
| **M9** | miniaudio |
| **Optional** | Tracy when `ENGINE_TRACY=ON` |

---

## Target file map (created incrementally)

| Path | Responsibility |
|------|----------------|
| `CMakeLists.txt` | Root project, options, FetchContent, targets |
| `cmake/Dependencies.cmake` | Third-party pins |
| `engine/core/` | `HardwareProbe`, `EngineConfig`, `FrameTimer`, `CrashHandler`, `Log`, `math.hpp` |
| `engine/platform/` | GLFW window, input, Win32 paths |
| `engine/render/` | Vulkan RHI, `Renderer`, `ShaderManager`, passes, `MeshUploadQueue`, `GpuDeferredFreeQueue` |
| `engine/world/` | `WorldConfig`, `ChunkStore`, `read_block`/`write_block`, streaming, meshing, `occupancy_at` |
| `engine/persist/` | `SaveService`, `MinimalSaveBackend`, `RegionFileSaveBackend`, `player.dat` |
| `engine/gameplay/` | `BlockRegistry`, interaction, `BlockMutation`, inventory |
| `engine/physics/` | Jolt bridge, `VoxelCapsuleResolver` (M6) |
| `engine/procgen/` | Heightmap (M2b), FastNoise2 (M10) |
| `engine/audio/` | miniaudio (M9) |
| `engine/ui/` | ImGui host |
| `engine/net/` | Stub transport |
| `game/main.cpp` | `Engine::startup` / frame loop |
| `tests/` | Catch2 — mirrors `engine/` units |
| `shaders/` | `.vert`/`.frag` SPIR-V sources |
| `assets/default.toml` | Engine defaults |

---

## Milestone gates (do not skip)

Close each gate only after **Requirement** + **Risk check** (spec §26). Risk IDs match spec register rows.

| Gate | Requirement | Risk check (§26) |
|------|-------------|------------------|
| **M0 done** | `ctest` green; window opens; accumulator; SnapshotRing + VkFence; JobSystem pool smoke (M0-7); `default.toml` loads (M0-4b); validation layers Debug | GPU config @ step 4 → `finalize_gpu` only after Renderer; 144 Hz sim drift → accumulator cap tested |
| **M3 thin** | One chunk opaque draw; SPIR-V rebuild on shader edit; deferred free + host flush tests pass | Upload/draw hazard → copy+barrier same CB; Indirect/staging UAF → `per_frame[fif]` + `StagingRing`; GPU buffer UAF → deferred free tests |
| **M3 stream** | Multi-chunk snapshot; **unload calls `enqueue_free` before slot reuse** (M3-5); chunk count in ImGui | Snapshot GPU race → `SnapshotCount = fif+1`; Cull space → render-relative AABB; Chunk UAF → unload order §7 |
| **M5 vertical slice** | Fly-nav break/place; MinimalSave reload; **one `EvtBlockBroken` per break** (test) | Stale block overwrite → `old_state` test; Player build lost → MinimalSave path documented |
| **M6 done** | Walk + spawn gate + gravity + step-up; streaming edge solid | Break collision / streaming edge / spawn bubble; CharacterVirtual rejected (capsule only) |
| **M10 done** | ProductionSave, inventory, audio events wired, FastNoise2 | OOM / save stalls / VWR version — spot-check risk rows for persist + stream |

---

# Pre-M0: Repository & CMake skeleton

### Task P0: Root CMake + MSVC toolchain (first build must pass)

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/Dependencies.cmake` — **P0 deps only:** glm, spdlog, Catch2
- Create: `engine/CMakeLists.txt`
- Create: `game/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `engine/core/Log.cpp` (empty stub: `namespace engine { void log_init() {} }`)
- Create: `engine/core/Log.hpp`
- Create: `game/main.cpp` (minimal `int main() { return 0; }`)
- Create: `.gitignore` (build/, `.vs/`, `out/`)
- Create: `README.md` (build commands)

- [ ] **Step 1:** Root `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.24)
project(VoxelEngine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ENGINE_TRACY "Link Tracy" OFF)
include(cmake/Dependencies.cmake)
add_subdirectory(engine)
add_subdirectory(game)
enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2:** `cmake/Dependencies.cmake` — `FetchContent_MakeAvailable(glm spdlog Catch2)` only; comment placeholders for later milestones (see staging table above).

- [ ] **Step 3:** `engine/CMakeLists.txt`

```cmake
add_library(engine_core STATIC core/Log.cpp)
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC spdlog::spdlog glm::glm)
```

- [ ] **Step 4:** `game/CMakeLists.txt`

```cmake
add_executable(VoxelEngine main.cpp)
target_link_libraries(VoxelEngine PRIVATE engine_core)
```

- [ ] **Step 5:** `tests/CMakeLists.txt`

```cmake
add_executable(engine_tests core/test_smoke.cpp)
target_link_libraries(engine_tests PRIVATE engine_core Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(engine_tests)
```

Create `tests/core/test_smoke.cpp`: `TEST_CASE("smoke") { REQUIRE(true); }`

- [ ] **Step 6:** Build + run tests

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\game\Debug\VoxelEngine.exe
echo $LASTEXITCODE  # expect 0
```

- [ ] **Step 7:** Commit

```bash
git add CMakeLists.txt cmake/ engine/ game/ tests/ .gitignore README.md
git commit -m "chore: CMake skeleton with engine, game, tests targets"
```

---

# M0: Bootstrap

### Task M0-1: Core math + ChunkCoord tests (TDD)

**Files:**
- Create: `engine/core/math.hpp`
- Create: `tests/core/test_math.cpp`
- Modify: `tests/CMakeLists.txt`, `engine/core/CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/core/test_math.cpp
#include <catch2/catch_test_macros.hpp>
#include "engine/core/math.hpp"

TEST_CASE("floor_div negative") {
    REQUIRE(engine::floor_div(-1, 8) == -1);
    REQUIRE(engine::floor_div(-32, 32) == -1);
}
TEST_CASE("positive_mod negative") {
    REQUIRE(engine::positive_mod(-1, 8) == 7);
    REQUIRE(engine::positive_mod(-32, 32) == 0);
}
TEST_CASE("block_to_chunk boundaries") {
    using engine::block_to_chunk;
    auto c = block_to_chunk(31, 0, 31);
    REQUIRE(c.x == 0);
    auto c2 = block_to_chunk(-1, -1, -1);
    REQUIRE(c2.x == -1);
}
TEST_CASE("block_to_chunk all axes negative boundary") {
    auto c = engine::block_to_chunk(-1, -32, 32);
    REQUIRE(c.x == -1);
    REQUIRE(c.y == -1);
    REQUIRE(c.z == 1);
}
TEST_CASE("block_local_in_chunk") {
    using engine::block_local_in_chunk;
    REQUIRE(block_local_in_chunk(-1, -1, -1) == glm::ivec3(31, 31, 31));
    REQUIRE(block_local_in_chunk(32, 32, 32) == glm::ivec3(0, 0, 0));
    REQUIRE(block_local_in_chunk(31, 15, 0) == glm::ivec3(31, 15, 0));
}
```

- [ ] **Step 2:** Run — expect link/compile fail

```powershell
cmake --build build --config Debug --target engine_tests
.\build\tests\Debug\engine_tests.exe "[math]"
```

- [ ] **Step 3: Implement** `engine/core/math.hpp`

```cpp
#pragma once
#include <glm/glm.hpp>
namespace engine {
inline int floor_div(int a, int b) {
    int q = a / b, r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}
inline int positive_mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}
using ChunkCoord = glm::ivec3;
inline ChunkCoord block_to_chunk(int wx, int wy, int wz) {
    return { floor_div(wx, 32), floor_div(wy, 32), floor_div(wz, 32) };
}
inline glm::ivec3 block_local_in_chunk(int wx, int wy, int wz) {
    return {
        positive_mod(wx, 32), positive_mod(wy, 32), positive_mod(wz, 32)
    };
}
} // namespace engine
```

- [ ] **Step 4:** Run tests — PASS

- [ ] **Step 5:** Commit `feat(core): floor_div, block_to_chunk, block_local_in_chunk`

---

### Task M0-2: BlockState + §10.5 indexing tests

**Files:**
- Create: `engine/gameplay/BlockState.hpp`
- Create: `engine/world/SectionIndexing.hpp`
- Create: `tests/world/test_indexing.cpp`

- [ ] **Step 1: Failing tests** — `block_index` roundtrip; `section_index` ↔ `(sx,sy,sz)`; `pack_vertex` max corner 16; `sizeof(TerrainVertex)==8`.

```cpp
TEST_CASE("section_index roundtrip") {
    for (int sx = 0; sx < 2; ++sx)
    for (int sy = 0; sy < 2; ++sy)
    for (int sz = 0; sz < 2; ++sz) {
        int idx = engine::section_index(sx, sy, sz);
        auto sc = engine::section_coord_from_index(idx);
        REQUIRE(sc.x == sx); REQUIRE(sc.y == sy); REQUIRE(sc.z == sz);
    }
}
```

- [ ] **Step 2:** Run — FAIL

- [ ] **Step 3:** Implement `BlockState.hpp` (§10 masks) + `SectionIndexing.hpp` + `TerrainVertex` per spec §10.5

- [ ] **Step 4:** PASS + commit `feat(world): BlockState and canonical section indexing`

---

### Task M0-3: Logging + CrashHandler

**Files:**
- Create: `engine/core/Log.cpp`, `engine/core/Log.hpp`
- Create: `engine/core/CrashHandlerWin32.cpp`
- Create: `assets/default.toml` (log section §8)

- [ ] **Step 1:** spdlog async file + console; path `%LOCALAPPDATA%/VoxelEngine/logs/engine.log`

- [ ] **Step 2:** `SetUnhandledExceptionFilter` + minidump to `crashes/`

- [ ] **Step 3:** Manual test — `SPDLOG_INFO` in `game/main.cpp`, force nullptr deref in Debug-only path → `.dmp` exists

- [ ] **Step 4:** Commit `feat(core): logging and crash minidump`

---

### Task M0-4: HardwareProbe + EngineConfig two-phase finalize

**Files:**
- Create: `engine/core/HardwareProbe.hpp`, `engine/core/HardwareProbe.cpp`
- Create: `engine/core/EngineConfig.hpp`, `engine/core/EngineConfig.cpp`
- Create: `engine/core/MemoryBudget.hpp`
- Create: `tests/core/test_engine_config.cpp`

- [ ] **Step 1:** `run_cpu()` → `CpuHardware` (cores, RAM, SSD heuristic)

- [ ] **Step 2:** Add **toml++** to `cmake/Dependencies.cmake` (M0-4 milestone)

- [ ] **Step 3:** `load_toml("assets/default.toml")` + `finalize_cpu(cpu)` → `ThreadConfig`, RAM `MemoryBudget`, **store `CpuHardware` snapshot** on config for later subsystems (M9 occlusion §19: `occlusion_grid_radius_chunks()` = `cpu.physical_cores >= 6 ? 48 : 32`), **no** `gpu_mesh_vram`

- [ ] **Step 4:** Test — after `finalize_cpu`, `mem.gpu_mesh_vram == 0`; `cfg.occlusion_grid_radius_chunks() > 0`

- [ ] **Step 5:** Stub `finalize_gpu(GpuCaps)` sets `gpu_mesh_vram` from `vram_bytes * fraction`

- [ ] **Step 6:** Commit `feat(core): HardwareProbe and split config finalize`

---

### Task M0-4b: WorldConfig + default.toml (before M2/M2b)

**Files:**
- Create: `engine/world/WorldConfig.hpp`
- Modify: `engine/core/EngineConfig.hpp` — embed `WorldConfig`
- Modify: `assets/default.toml` — `[world]` section
- Create: `tests/world/test_world_config.cpp`

- [ ] **Step 1: Failing test** — defaults match spec §9

```cpp
TEST_CASE("WorldConfig defaults") {
    engine::WorldConfig w;
    REQUIRE(w.chunk_height_min == -4);
    REQUIRE(w.chunk_height_max == 8);
    REQUIRE(w.finite_bounds == false);
    REQUIRE(w.sea_level == 64);
}
```

- [ ] **Step 2:** Parse from TOML; missing keys use struct defaults above

- [ ] **Step 3: Integration test** — full `assets/default.toml` (after M0-3 `[log]`, M0-4 `[engine.threads]`, M0-4b `[world]`) loads without throw:

```cpp
TEST_CASE("default.toml loads end-to-end") {
    engine::EngineConfig cfg;
    REQUIRE_NOTHROW(cfg.load_toml("assets/default.toml"));
    cfg.finalize_cpu(engine::HardwareProbe::run_cpu()); // or mock CpuHardware
    REQUIRE(cfg.world().sea_level == 64);
}
```

Catches typos / wrong key paths in the grown TOML file.

- [ ] **Step 4:** PASS + commit `feat(world): WorldConfig and default.toml integration`

---

### Task M0-5: FrameTimer + sim accumulator (no Vulkan)

**Files:**
- Create: `engine/core/FrameTimer.hpp`
- Create: `engine/core/SimClock.hpp`
- Modify: `game/main.cpp`

- [ ] **Step 1:** `FrameTimer` records stage durations; warn if frame > 16.6 ms

- [ ] **Step 2:** Accumulator loop §4 — cap `4 * fixed_dt`, `alpha` for camera

- [ ] **Step 3:** Headless or GLFW window — log sim tick count over 2 s wall time ≈ 120 ticks @ 60 Hz effective

- [ ] **Step 4:** Commit `feat(core): fixed 60 Hz accumulator`

---

### Task M0-6a: SnapshotRing CPU policy (no Vulkan headers)

**Files:**
- Create: `engine/render/WorldRenderSnapshot.hpp`
- Create: `engine/render/SnapshotRing.hpp` — uses `bool slot_signaled[]`, **not** `VkFence`
- Create: `tests/render/test_snapshot_ring.cpp`

- [ ] **Step 1:** `SnapshotCount = frames_in_flight + 1`; initial all slots **signaled** (frame-0 safe §5)

```cpp
TEST_CASE("snapshot ring picks signaled slot") {
    SnapshotRing ring(2); // fif=2 → 3 slots, all signaled at start
    REQUIRE(ring.pick_write_slot() == 0);
    ring.mark_submitted(0); // slot 0 in-flight, not signaled
    ring.mark_gpu_complete(0);
    REQUIRE(ring.pick_write_slot() == 0);
}
```

- [ ] **Step 2:** Implement pick / mark_submitted / mark_gpu_complete rotation §5

- [ ] **Step 3:** Commit `feat(render): CPU snapshot ring policy`

---

### Task M0-7: Engine init order shell (steps 1–8, no GPU world)

**Depends on:** M0-4 `ThreadConfig` from `finalize_cpu(cpu)` — init order: probe → config → JobSystem.

**Files:**
- Create: `engine/Engine.hpp`, `engine/Engine.cpp`
- Create: `engine/core/JobSystem.hpp`, `engine/core/JobSystem.cpp`
- Create: `tests/core/test_job_system.cpp`
- Modify: `game/main.cpp`
- Modify: `cmake/Dependencies.cmake` — **add flecs, enkiTS, glfw** (M0-7 milestone)
- Modify: `engine/CMakeLists.txt` — link new deps

- [ ] **Step 1:** `Engine::startup()` runs steps 1–8 per §3 table; `shutdown()` reverse

- [ ] **Step 2:** `JobSystem::init(ThreadConfig)` — separate enkiTS task sets: `worker`, `io`, `meshing` (sizes from M0-4, not hardcoded)

- [ ] **Step 3: Pool smoke test** — catches VM / big.LITTLE mis-detection of `physical_cores` early:

```cpp
TEST_CASE("job pools run one task each") {
    auto cpu = engine::HardwareProbe::run_cpu();
    engine::EngineConfig cfg; cfg.load_toml("assets/default.toml");
    cfg.finalize_cpu(cpu);
    engine::JobSystem js;
    js.init(cfg.threads());
    std::atomic<int> worker_done{0}, mesh_done{0};
    js.run_worker([&]{ worker_done = 1; });
    js.run_meshing([&]{ mesh_done = 1; });
    js.wait_all();
    REQUIRE(worker_done == 1);
    REQUIRE(mesh_done == 1);
    REQUIRE(cfg.threads().meshing_threads <= cfg.threads().worker_threads);
}
```

- [ ] **Step 4:** Flecs world + empty module

- [ ] **Step 5:** GLFW window 1280×720; poll events; exit on ESC

- [ ] **Step 6:** Commit `feat(engine): init steps 1-8, JobSystem pools with smoke test`

---

### Task M0-8: Vulkan Renderer minimal + validation + finalize_gpu (step 9–9b)

**Files:**
- Create: `engine/render/VulkanContext.hpp`, `Renderer.hpp`, `Renderer.cpp`
- Create: `engine/render/VkCheck.hpp`, `engine/render/VulkanDebug.hpp`
- Modify: `cmake/Dependencies.cmake` — **add volk, VMA, Vulkan SDK**
- Modify: `Engine.cpp` — step 9, 9b before step 11

- [ ] **Step 1:** volk init, instance, surface, device, swapchain, `VK_CHECK` macro §0

- [ ] **Step 2:** **Debug builds:** enable `VK_LAYER_KHRONOS_validation` if present; `VkDebugUtilsMessengerEXT` → validation messages via `SPDLOG_WARN` / `SPDLOG_ERROR`

- [ ] **Step 3:** Fill `GpuCaps` including `max_memory_allocation_count`, `min_uniform_buffer_offset_alignment`, `non_coherent_atom_size`

- [ ] **Step 4:** Call `finalize_gpu(gpu)` **before** any `ChunkStore::init`

- [ ] **Step 5:** Clear color pass only (no mesh) — present loop with `OUT_OF_DATE` → `recreate_swapchain` using `vkDeviceWaitIdle` (§4 M0 policy)

- [ ] **Step 6:** Extent 0×0 → pause present (no spin)

- [ ] **Step 7:** Commit `feat(render): Vulkan bootstrap, validation, and finalize_gpu gate`

---

### Task M0-8b: Bind SnapshotRing to real VkFence

**Files:**
- Modify: `engine/render/SnapshotRing.hpp` — `VkFence` per slot, `VK_FENCE_CREATE_SIGNALED_BIT`
- Modify: `Renderer.cpp` — wait/signaled checks use `vkGetFenceStatus` / `vkWaitForFences`

- [ ] **Step 1:** Replace bool mock with `VkFence snapshot_fence[SnapshotCount]`

- [ ] **Step 2:** Submit path signals fence; `pick_write_slot` waits only on unsignaled slots

- [ ] **Step 3:** Commit `feat(render): snapshot ring VkFence binding`

---

### Task M0-9: Flecs events skeleton (§6)

**Files:**
- Create: `engine/world/WorldEvents.hpp`
- Modify: `Engine.cpp` — register `EvtOriginShift`, `EvtChunkLoaded`, observers stub

- [ ] **Step 1:** Define event structs; `ECS_EVENT` declarations

- [ ] **Step 2:** Empty observer for `ChunkDirty` → log only

- [ ] **Step 3:** Commit `feat(world): Flecs world events skeleton`

---

### M0 verification

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\game\Debug\VoxelEngine.exe
```

Expected: window + clear color; logs show init order; `engine_tests` all pass.

- [ ] **Commit:** `chore(m0): milestone M0 bootstrap complete`

---

# M1: Camera & WorldPosition

### Task M1-1: WorldPosition + origin rebase test

**Files:**
- Create: `engine/world/WorldPosition.hpp`
- Create: `engine/world/OriginRebase.hpp`
- Create: `tests/world/test_origin_rebase.cpp`

- [ ] **Step 1:** Test — rebase does not change `WorldPosition` chunk+local; `render_origin` delta correct

- [ ] **Step 2:** Implement `REBASE_RADIUS` from config; emit `EvtOriginShift` (no `ChunkDirty` on rebase)

- [ ] **Step 3:** Commit `feat(world): WorldPosition and origin rebase`

---

### Task M1-2: Fly camera + input

**Files:**
- Create: `engine/platform/Input.hpp`
- Create: `engine/gameplay/CameraSystem.hpp`
- Modify: `game/main.cpp` — WASD + mouse look

- [ ] **Step 1:** `Camera` Flecs component; write view/proj into snapshot each RenderBuild

- [ ] **Step 2:** `render_origin` uniform in stub UBO (values only, full GPU in M3)

- [ ] **Step 3:** **Expected without terrain:** clear-color scene only; camera matrices update each frame; **no ChunkStore / mesh required**

- [ ] **Step 4:** Manual — fly in empty scene; rebase triggers at radius

- [ ] **Step 5:** Commit `feat(m1): fly camera without terrain dependency`

---

# M2: Chunks & streaming (CPU only)

### Task M2-1: Section + palette + occupancy + border

**Files:**
- Create: `engine/world/Section.hpp`, `Chunk.hpp`, `ChunkStore.hpp`
- Create: `tests/world/test_palette.cpp`, `tests/world/test_occupancy.cpp`

- [ ] **Step 1:** Palette max 4096; reject 4097th unique state (test)

- [ ] **Step 2:** `SectionOccupancy` bitmask; sync from solid blocks on load

- [ ] **Step 3:** `BorderCell` + `SectionBorderCache` arrays

- [ ] **Step 4:** `occupancy_at(wx,wy,wz)` §12 — cross-chunk + `SolidIfChunkMissing`

- [ ] **Step 4b:** `ChunkStore` block API (used by M5 mutations):

```cpp
BlockState read_block(BlockPos pos) const;
bool write_block(BlockPos pos, BlockState state); // palette remap + occupancy hook
```

Tests: roundtrip write/read; occupancy updates when solid ↔ air.

- [ ] **Step 5:** Commit `feat(m2): section palette, occupancy, read_block/write_block`

---

### Task M2-2: Chunk entity lifecycle (§7)

**Files:**
- Create: `engine/world/ChunkLifecycle.hpp`
- Modify: `ChunkStore` — `coord_to_entity`, `ChunkSlotRef{slot, generation}`

- [ ] **Step 1:** Load order: allocate → ecs entity → fill → mesh queue stub

- [ ] **Step 2:** Unload order **stub (CPU-only, §7 steps 1–3):**

  1. Mark chunk **pending_unload** — RenderBuild excludes from next snapshot (no GPU slots yet).  
  2. `ecs_delete` chunk entity — observer **`on_chunk_unload`** stub logs slot IDs (real `enqueue_free` in **M3-5**).  
  3. `ChunkStore::free(coord)` → bump `generation`.

  **Do not** skip step 2 observer hook — M3-5 wires `GpuDeferredFreeQueue::enqueue_free` into it.

- [ ] **Step 3:** Test — stale `ChunkSlotRef` rejected after unload

- [ ] **Step 4:** Commit `feat(m2): chunk lifecycle with unload observer hook`

---

### Task M2-3: Euclidean streaming (§14)

**Files:**
- Create: `engine/world/StreamingSystem.hpp`
- Modify: `Engine.cpp` — step 11 after 9b only

- [ ] **Step 1:** `StreamingConfig` from finalized `EngineConfig` (GPU-aware)

- [ ] **Step 2:** Load set: `dx²+dz²≤r²` + vertical band; unload outside hull

- [ ] **Step 3:** Log loaded chunk count **once per second** via `SPDLOG_INFO` (no ImGui — that is M3-4)

- [ ] **Step 4:** Commit `feat(m2): 3D euclidean chunk streaming`

---

# M2b: Minimal worldgen

### Task M2b-1: Heightmap columns

**Files:**
- Create: `engine/procgen/HeightmapWorldgen.hpp`
- Create: `tests/procgen/test_heightmap_worldgen.cpp`
- Modify: `ChunkStore` load path — generate if missing

- [ ] **Step 1:** Stone/dirt/grass/water by `WorldConfig::sea_level` (from M0-4b)

- [ ] **Step 2:** Mark `CHUNK_GENERATED`; occupancy from solids

- [ ] **Step 3: Tests**

```cpp
TEST_CASE("heightmap worldgen deterministic for seed") {
    HeightmapWorldgen a(42), b(42);
    REQUIRE(a.block_at_world(10, 20, 30).raw == b.block_at_world(10, 20, 30).raw);
}
TEST_CASE("heightmap worldgen marks generated unmodified") {
    auto flags = gen.flags_for_new_chunk();
    REQUIRE((flags & CHUNK_GENERATED) != 0);
    REQUIRE((flags & CHUNK_MODIFIED_BY_PLAYER) == 0);
}
```

- [ ] **Step 4:** Manual — stream around player; chunk count log (M2-3)

- [ ] **Step 5:** Commit `feat(m2b): heightmap worldgen with deterministic tests`

---

# M3: Thin renderer (critical path §27)

### Task M3-1: Greedy mesher (single section)

**Files:**
- Create: `engine/world/GreedyMesher.hpp`
- Create: `tests/world/test_greedy_mesher.cpp`

- [ ] **Step 1:** 1³ solid → 6 faces; two adjacent solids → shared face culled

- [ ] **Step 1b:** Border neighbor solid hides exterior section face (mesher reads `BorderCell`, not live chunk mutex)

- [ ] **Step 1c:** `BorderCell` sky/block light copied into edge `TerrainVertex.light` nibbles

- [ ] **Step 2:** Read `BorderCell` only for neighbors

- [ ] **Step 3:** Output `TerrainVertex` + indices

- [ ] **Step 4:** Commit `feat(m3): greedy mesher with border cull and light`

---

### Task M3-2: GpuDeferredFreeQueue + per_frame rings + host_write

**Files:**
- Create: `engine/render/GpuDeferredFreeQueue.hpp`
- Create: `engine/render/PerFrameGpuWrites.hpp`
- Create: `engine/render/HostMemory.hpp` — `host_write` flush §13
- Create: `tests/render/test_deferred_free_queue.cpp`
- Create: `tests/render/test_host_memory_alignment.cpp`

- [ ] **Step 1:** `PendingGpuFree{slot_id, last_used_frame}`; free when `frame_submit_fence[last_used_frame % fif]` signaled

- [ ] **Step 2:** `per_frame[fif]` UBO + indirect buffers; alignment from `GpuCaps`

- [ ] **Step 3:** `StagingRing[fif]` size `chunk_mesh_cpu_ram / fif`; `pending_uploads` overflow

- [ ] **Step 4: Tests**

```cpp
TEST_CASE("deferred_free_does_not_free_until_last_used_fence_signaled") { /* mock fences */ }
TEST_CASE("host_write_aligns_flush_range_to_non_coherent_atom_size") { /* caps.non_coherent_atom_size = 64 */ }
TEST_CASE("staging_ring_overflow_keeps_upload_pending") { /* enqueue > slot size */ }
```

- [ ] **Step 5:** PASS + commit `feat(m3): GPU lifetime rings, deferred free, host flush tests`

---

### Task M3-3: MeshUploadQueue + one-chunk draw

**Files:**
- Create: `engine/render/MeshUploadQueue.hpp`
- Create: `engine/render/TerrainPass.hpp`
- Create: `shaders/terrain.vert`, `shaders/terrain.frag` (single procedural material)
- Create: `shaders/CMakeLists.txt` or `cmake/CompileShaders.cmake`

- [ ] **Step 0:** Shader compile at build time — `glslc` (Vulkan SDK) or shaderc:

```cmake
# MUST list DEPENDS on source shaders or CMake won't rebuild .spv on edit:
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/shaders/terrain.vert.spv
  COMMAND glslc -fshader-stage=vert ${CMAKE_SOURCE_DIR}/shaders/terrain.vert -o ...
  DEPENDS ${CMAKE_SOURCE_DIR}/shaders/terrain.vert
  ...
)
# Same for terrain.frag
add_custom_target(shaders_spv DEPENDS ...vert.spv ...frag.spv)
```

Runtime loads SPIR-V from build dir; fail **configure** if `glslc` missing (document in README). After edit `terrain.vert`, rebuild must refresh `.spv` (verify once manually in M3 thin gate).

- [ ] **Step 1:** One hardcoded chunk at origin — mesh → staging → copy → barrier → `vkCmdDrawIndexedIndirect`

- [ ] **Step 2:** `DrawSection::model_translation` = chunk*32 + section*16 − render_origin

- [ ] **Step 3:** **No water**, **no hot-reload**, **no LOD**, **no streaming** in this task

- [ ] **Step 4:** `GpuMeshPool` bucket alloc; enqueue_free on regrow

- [ ] **Step 5:** Manual — see gray/green terrain cube

- [ ] **Step 6:** Commit `feat(m3): thin renderer one chunk`

---

### Task M3-4: UiHost before world pass (same CB)

**Files:**
- Create: `engine/ui/UiHost.hpp`
- Modify: `cmake/Dependencies.cmake` — **add imgui** (M3-4 milestone)
- Modify: `Renderer.cpp` — frame order §4 step 5–6

- [ ] **Step 1:** ImGui overlay: FPS, sim tick, **loaded chunk count** (moved from M2-3 log)

- [ ] **Step 2:** `new_frame` before `record_and_submit`

- [ ] **Step 3:** Commit `feat(m3): ImGui debug overlay in render CB`

---

### Task M3-5: Multi-chunk snapshot + streaming hook + GPU unload (§7)

**Files:**
- Modify: `StreamingSystem`, `RenderBuild`, `ChunkLifecycle.hpp`
- Create: `tests/world/test_chunk_unload_gpu.cpp`

- [ ] **Step 1:** Join mesh jobs each frame; flush uploads

- [ ] **Step 2:** Frustum cull in render space; vector reuse §5

- [ ] **Step 3: Wire §7 unload with GPU slots (closes M2-2 gap):**

  On stream unload trigger:

  1. `pending_unload` — chunk excluded from `opaque_sections` next RenderBuild.  
  2. Collect all `MeshSectionHandle` / GPU slot IDs for chunk → `GpuDeferredFreeQueue::enqueue_free(slot, last_used_frame)`.  
  3. **`ecs_delete`** chunk entity (observer may duplicate enqueue — idempotent by slot generation).  
  4. `ChunkStore::free(coord)` bump generation.

  Test: load chunk → assign fake mesh slot → unload → slot in pending free queue, **not** in active pool; generation bumped.

- [ ] **Step 4:** Manual — fly in M2b world; chunks appear/disappear meshed; no crash after rapid stream

- [ ] **Step 5:** Commit `feat(m3): streaming draw and deferred GPU unload on chunk despawn`

---

# M4: Look (opaque, sky far-depth, water, materials, hot-reload)

### Task M4-1: Pass order Opaque → Sky far-depth → Water → UI

**Files:**
- Create: `engine/render/SkyPass.hpp`, `WaterPass.hpp`
- Modify: `shaders/`, greedy mesher — water faces, **no internal water faces** §18

Pass order per spec §18 (efficient — no sky overdraw on opaque):

1. **OpaqueTerrain** — clear depth+color; depth write ON  
2. **Sky** — fullscreen triangle; depth test EQUAL far; depth write OFF; fills only far-depth pixels  
3. **WaterTransparent** — depth test ON, write OFF, alpha blend; BTF sort by section AABB center  
4. **UI**

- [ ] **Step 1:** Opaque pass first (existing terrain pipeline)

- [ ] **Step 2:** Sky pass **after opaque** — only pixels still at far depth get sky color

- [ ] **Step 3:** Water pass — no internal water faces; coarse BTF sort; Phase 1 overlap artifacts OK

- [ ] **Step 4:** Procedural `material_id` shading

- [ ] **Step 5:** `ShaderManager::poll_hot_reload` Debug only

- [ ] **Step 6:** Commit `feat(m4): opaque sky water pass order and materials`

---

# M5: Vertical slice (gate)

### Task M5-1: Block registry + interaction + mutation

**Files:**
- Create: `engine/gameplay/BlockRegistry.hpp`
- Create: `engine/gameplay/BlockInteraction.hpp`
- Modify: `BlockMutation` apply — **reject unless `read_block == old_state`**

- [ ] **Step 1:** Raycast DDA `PlayerReach`

- [ ] **Step 2:** Break/place → `read_block`/`write_block` + occupancy same tick + `ChunkDirty`

- [ ] **Step 2b:** **Place source (M5 only):** `CreativeBlockPicker` debug selected `BlockId` — **not** inventory (M7 wires hotbar → place)

- [ ] **Step 3:** Emit `EvtBlockBroken` / `EvtBlockPlaced` (Flecs events — **consumer is M9**, but contract tested now)

- [ ] **Step 3b: Event contract test** (no audio required):

```cpp
TEST_CASE("break emits exactly one EvtBlockBroken") {
    EventCounter counter;
    ecs_observer(world, EvtBlockBroken, &counter);
    break_block_at(test_pos);
    ecs_run(world); // or flush observers
    REQUIRE(counter.count == 1);
}
```

Same pattern for place → `EvtBlockPlaced`. Prevents discovering broken emits four milestones later in M9.

- [ ] **Step 4:** Test stale mutation rejected

- [ ] **Step 5:** Commit `feat(m5): break place creative picker and block events`

---

### Task M5-2: SaveService + MinimalSaveBackend + player.dat

**Files:**
- Create: `engine/persist/SaveService.hpp` — stable API for game code
- Create: `engine/persist/MinimalSaveBackend.hpp`
- Create: `engine/persist/PlayerDat.hpp`

**MinimalSave layout (M5):**

```
<saves>/<world_name>/
  player.dat                              # VPL1
  minimal/chunks/c.<cx>.<cy>.<cz>.bin     # per modified chunk, uncompressed stub OK
  world.meta                              # seed + format marker
```

M8 adds `RegionFileSaveBackend` implementing same `SaveService` — M5 gameplay code unchanged.

- [ ] **Step 1:** `SaveService::save_world` / `load_world` delegates to `MinimalSaveBackend`

- [ ] **Step 2:** Atomic rename for `player.dat` and chunk bins

- [ ] **Step 3:** Integration test — place block (creative picker), save, reload, block still there

- [ ] **Step 4:** Commit `feat(m5): SaveService with MinimalSaveBackend`

**M5 gate checklist (manual — fly camera, not walk):**

1. Start world (M2b terrain)  
2. **Fly / noclip navigate** (M1 camera — no M6 capsule yet)  
3. See meshed terrain (M3+)  
4. Raycast target block → **break** → **place** (creative BlockId)  
5. **Save** → quit → **reload** → block + player position persist  

**M6 gate checklist (separate):** spawn gated → walk on terrain → step-up ≤ `step_height` → cannot enter unloaded chunks (`SolidIfChunkMissing`).

---

# M5b: Block light

### Task M5b-1: Flood + cross-chunk propagation

**Files:**
- Create: `engine/world/BlockLight.hpp`
- Modify: `GreedyMesher` — pack light into `TerrainVertex`

- [ ] **Step 1:** Torch place seeds flood §12 border queue

- [ ] **Step 2:** Deterministic radius test (Catch2)

- [ ] **Step 3:** Commit `feat(m5b): block light flood`

---

# M6: Player movement

### Task M6-1: VoxelCapsuleResolver + step-up + gravity

**Files:**
- Create: `engine/physics/VoxelCapsuleResolver.hpp`
- Create: `engine/gameplay/PlayerMotorConfig.hpp`
- Create: `engine/gameplay/PlayerSpawnReadyGate.hpp`
- Create: `tests/physics/test_capsule_resolver.cpp`
- Modify: `cmake/Dependencies.cmake` — **add Jolt** (M6 milestone)
- Modify: `GameSession` — gate before movement

```cpp
struct PlayerMotorConfig {
    float gravity = -24.0f;
    float jump_velocity = 8.0f;
    float max_walk_speed = 5.0f;
    float ground_snap = 0.1f;
};
struct VoxelMovementConfig {
    float step_height = 0.6f;
    float skin_width = 0.03f;
    int max_solver_iterations = 4;
};
```

- [ ] **Step 1:** `move_and_slide` + gravity integration uses `occupancy_at` only

- [ ] **Step 2:** Step-up after horizontal block within `step_height`

- [ ] **Step 3:** `PlayerSpawnReadyGate` — 3×3×3 neighborhood loaded before input

- [ ] **Step 4:** Jolt init step 15 — sensor only, layer matrix §20 stub

- [ ] **Step 5: Tests / manual**

  - Falls onto ground and stops (vertical velocity → 0 within `ground_snap`)  
  - Cannot tunnel through 1-block wall  
  - Step-up works for ledge ≤ `step_height`  
  - Sample at streaming edge with missing chunk → solid  

- [ ] **Step 6:** Emit `EvtPlayerLanded` / footstep cadence hook for M9

- [ ] **Step 7:** Commit `feat(m6): capsule movement gravity and spawn gate`

---

# M7: Inventory UI

### Task M7-1: Inventory components + ImGui

**Files:**
- Create: `engine/gameplay/Inventory.hpp`
- Modify: `BlockInteraction` — place reads **hotbar selected BlockId** (replaces M5 `CreativeBlockPicker` as default in Release; Debug may keep creative override via TOML)

- [ ] **Step 1:** `ItemStack`, `Inventory`, hotbar selection

- [ ] **Step 2:** Persist full `InventorySnapshot` in `player.dat`

- [ ] **Step 3:** Commit `feat(m7): inventory drives place BlockId`

---

# M8: ProductionSave

### Task M8-1: .vwr regions + CRC + quarantine

**Files:**
- Create: `engine/persist/RegionFileSaveBackend.hpp`
- Create: `engine/persist/Quarantine.hpp`
- Modify: `cmake/Dependencies.cmake` — **add zstd**

- [ ] **Step 0: MinimalSave migration policy (explicit):** On first `load_world` after M8, if `<world>/minimal/` exists → **import modified chunk bins into `.vwr` once** → write `world.meta` flag `minimal_migrated=true` → keep `minimal/` as backup (do not auto-delete in prototype). New worlds use Region backend only.

- [ ] **Step 1:** `RegionFileSaveBackend` implements `SaveService`; `VWR1` header, sector index, blob header `uncompressed_size`, `compressed_size`, `crc32`

- [ ] **Step 2:** Load: CRC fail → quarantine if modified else regen; version mismatch path §17

- [ ] **Step 3:** zstd compress on IO thread pool

- [ ] **Step 4:** Atomic rename save order §17

- [ ] **Step 5:** Tests — CRC corrupt vs version mismatch

- [ ] **Step 6:** Commit `feat(m8): ProductionSave vwr and quarantine`

---

# M9: Audio

### Task M9-1: miniaudio + event-driven SFX

**Files:**
- Create: `engine/audio/AudioEngine.hpp`
- Create: `engine/audio/OcclusionGrid.hpp`
- Modify: `cmake/Dependencies.cmake` — **add miniaudio**

- [ ] **Step 1:** Engine step 16 — buses, listener at camera

- [ ] **Step 2:** **No direct gameplay calls into AudioEngine** — subscribe to Flecs events:

| Event | Source | SFX |
|-------|--------|-----|
| `EvtBlockBroken` | M5 | break sound |
| `EvtBlockPlaced` | M5 | place sound |
| `EvtPlayerFootstep` / ground contact cadence | M6 | footstep |

- [ ] **Step 3:** Occlusion grid 1 m — update on `ChunkDirty`

  **Radius source (§19):** `EngineConfig::occlusion_grid_radius_chunks()` — computed in `finalize_cpu()` from stored `CpuHardware` (M0-4), e.g. `physical_cores >= 6 ? 48 : 32` chunks. Do not re-probe hardware in M9; read from config snapshot.

- [ ] **Step 4:** Commit `feat(m9): event-driven audio and occlusion grid`

---

# M10: Full worldgen & prototype done

### Task M10-1: FastNoise2 biomes + caves + structures

**Files:**
- Create: `engine/procgen/TerrainGraph.hpp`
- Modify: `ProcGen` — replace heightmap-only as default

- [ ] **Step 1:** Add FastNoise2 to `cmake/Dependencies.cmake`; seed from `world.meta`

- [ ] **Step 2:** Biomes, caves, structure scatter §22 (large in-chunk; micro at edges)

- [ ] **Step 3:** Performance — meshing budget caps §12

- [ ] **Step 4:** Playtest full loop with ProductionSave + inventory + audio

- [ ] **Step 5:** Commit `feat(m10): FastNoise2 worldgen — prototype complete`

---

## Spec coverage checklist (self-review)

| Spec section | Plan tasks |
|--------------|------------|
| §0 HardwareProbe / GpuCaps | M0-4, M0-8 (+ validation) |
| §0.1 MemoryBudget | M0-4, M3-2 |
| §3 Init order 9b before 11 | M0-8, M2-3 |
| §4 Accumulator / swapchain | M0-5, M0-8 |
| §5 Snapshot + deferred free | M0-6a, M0-8b, M3-2 |
| §6–7 Flecs chunks | M0-9, M2-2 |
| §8 Catch2 matrix | M0-1,2,4b,7; M2-1,2; M2b; M3-1,2,5; M5-1,2; M6; M8-1 |
| §19 Audio occlusion radius | M0-4 `CpuHardware` → `EngineConfig`; M9 reads config |
| §26 Risk register | Milestone **Risk check** column above |
| §9 WorldConfig / WorldPosition | M0-4b, M1-1 |
| §10–10.5 BlockState / verts | M0-2, M3-3 (+ shader compile) |
| §11 Palette cap | M2-1 |
| §12 Meshing / occupancy / step-up | M2-1, M3-1, M6-1 |
| §13 Upload / staging / host flush | M3-2, M3-3 |
| §14 Streaming | M2-3 (log), M3-4 (ImGui), M3-5 |
| §15 Mutation old_state | M5-1 |
| §17 .vwr CRC / quarantine / migration | M8-1 Step 0 |
| §18 Water pass order | M4-1 Opaque→Sky→Water |
| §20 Physics | M6-1 |
| §22 Worldgen | M2b-1, M10-1 |
| §25 M5 / M6 gates | M5-2 fly checklist; M6-1 walk checklist |
| §27 Thin M3 first | M3-3 before M3-5 |

**Deferred post-M10 (documented, not in this plan):** M11 destruction, M12 CSM, M13+ GI, MP/E Net.

---

## Suggested commit rhythm

- One commit per task above (or per Step 5 when TDD task).
- Do not batch M3 thin + water + streaming in one commit.
- Tag milestones: `git tag m0-bootstrap`, `m5-vertical-slice`, `m10-prototype`.

---

## References during implementation

- Design spec: `docs/superpowers/specs/2026-06-03-voxel-engine-design.md`
- Critical path table: spec §27
- Risk register: spec §26 — each milestone **Gate** table includes mapped risk checks; do not tag milestone without them
