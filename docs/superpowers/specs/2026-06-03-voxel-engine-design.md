# Voxel Engine — Phase 1 Technical Design

**Status:** Approved + v9 (init/GPU/persist/movement polish, 2026-06-03)  
**Target:** Minecraft-like voxel survival, Teardown-style destruction (post-prototype), C++ / Vulkan 1.2, Windows desktop  
**Repo:** Single CMake project, incremental growth  

## Confirmed design decisions

| Topic | Decision |
|-------|----------|
| LOD0 chunk size | **32³** voxels, **8× Section 16³** per chunk |
| World bounds default | **Infinite** (`WorldConfig::finite_bounds` optional for tests) |
| Block light (torch/flood) | **In prototype** (milestones M5–M10) |
| Audio occlusion grid | **1 m** voxel-aligned; update radius from hardware |
| Toolchain until M10 | **MSVC only** (MinGW deferred) |
| Chunk addressing | **`ChunkCoord` = `glm::ivec3` `(cx, cy, cz)`** from day one |
| Config / limits | **`HardwareProbe` first**, then auto-derived values; **TOML overrides** |
| Sim / render | **60 Hz fixed sim**, `frames_in_flight` + **`SnapshotCount = fif + 1`** (§4–§5) |
| Origin rebase | **No remesh** — section-local verts + `render_origin` uniform (§6, §9, §13) |
| Config finalize | **Two-phase:** `finalize_cpu` @ step 4, `finalize_gpu` @ step 9 (§0, §3) |
| GPU buffer free | **Deferred retire** until in-flight frames done (§5, §13) |
| Player vs terrain | **`VoxelCapsuleResolver`** + **`occupancy_at`** (`SolidIfChunkMissing` at edge) — **not** CV vs mesh (§12, §20) |
| GPU per-frame | **Indirect + UBO + staging** ringed `frames_in_flight` (§5, §13) |
| RAM budgeting | **Split `MemoryBudget`** — voxel / mesh CPU / VRAM / IO (§0.1, §14) |
| Streaming | **3D budget** — horizontal + vertical radii from layer count (§14) |
| Regions | **3D regions** `r.<rx>.<ry>.<rz>.vwr`, 8×8×8 chunks (§17) |
| Player save | **`player.dat` (VPL1)** separate from `world.meta` (§17) |
| Chunk version | **Modified → quarantine**; unmodified → regen (§17) |
| Structures | Large in-chunk only; **micro vegetation at edges** OK (§22) |
| Water | **Sky → Opaque → Water → UI** (§18) |
| Vertical slice | **Boring playable by M5** — break/place/save/load (§25) |
| Vertex / index | **5-bit corner pos 0..16**, canonical `x+16z+256y` (§10.5) |
| Section draw | **Chunk origin + section offset×16** in model matrix (§10.5) |
| Player position | **`WorldPosition` chunk+local** runtime + save (§9) |
| Streaming loadset | **Euclidean XZ** `dx²+dz²≤r²` (§14) |
| M5 / M8 save | **MinimalSave** vs **ProductionSave** (§17, §25) |

---

## 0. Core principle: HardwareProbe (no magic numbers)

Tunables are derived from hardware — **not** hardcoded. **`EngineConfig`** loads TOML; overrides win when set.

### Two-phase probe (fixes GPU-before-device contradiction)

**Problem:** `vram_bytes`, `multi_draw_indirect`, `discrete_gpu` need `VkPhysicalDevice`, but full `Renderer` is step 9 while `finalize()` was step 4.

**Solution:** split probe + finalize:

```cpp
// Step 3 — no VkDevice required
struct CpuHardware {
    int  logical_cores;
    int  physical_cores;   // GetLogicalProcessorInformationEx
    size_t ram_bytes;      // GlobalMemoryStatusEx
    bool has_ssd;          // IOCTL_STORAGE_QUERY_PROPERTY (SeekPenalty), no disk write
};

// Step 9 — Renderer fills after vkCreateDevice
struct GpuCaps {
    size_t vram_bytes;              // DEVICE_LOCAL heap sum
    bool   multi_draw_indirect;
    bool   discrete_gpu;
    bool   descriptor_indexing;     // optional features
    uint32_t graphics_queue_family;
    uint32_t max_memory_allocation_count;
    VkDeviceSize max_storage_buffer_range;
    VkDeviceSize min_uniform_buffer_offset_alignment;
    VkDeviceSize non_coherent_atom_size;
};
```

**Step 3:** `HardwareProbe::run_cpu()` → `CpuHardware`.

**Step 4:** `EngineConfig::load_toml()` → **`finalize_cpu(cpu)`** → `ThreadConfig`, `MemoryBudget` RAM pools, `StreamingConfig` (voxel RAM only), `MeshingConfig`, audio radii — **no GPU fields yet**.

**Step 9:** `Renderer::init(platform)` → creates instance/device/swapchain → returns **`GpuCaps`**.

**Step 9b:** **`finalize_gpu(gpu_caps)`** → `MemoryBudget::gpu_mesh_vram`, `RenderPreset`, optional streaming render tweaks.

`DetectedHardware` in docs = `{CpuHardware cpu; GpuCaps gpu;}` assembled after step 9b for subsystems that want one struct.

Full application **init order:** §3.

Affected systems: thread pools, **`MemoryBudget`**, `StreamingConfig`, `MeshingConfig`, `RenderPreset` (GPU part), Jolt hints.

---

## 0.1 Memory budgets (split, not one chunk size)

**Do not** derive `max_loaded_chunks` from a single optimistic 16 KB/chunk figure. LOD0 voxel RAM alone is ~**130–180 KB/chunk** before meshes, GPU, and containers.

### Per-section voxel estimate

| Field | Size |
|-------|------|
| `blocks[4096]` uint16 | 8 KB |
| `skyLight[4096]` | 4 KB |
| `blockLight[4096]` | 4 KB |
| palette + flags | ~4–8 KB |
| `SectionBorderCache` (`BorderCell` × 6 faces) | ~6 KB |
| `SectionOccupancy` (16³ bits) | 512 B |
| **Total / section** | **~31 KB** |

**Per chunk (8 sections):** `section_bytes_est = 31 * 1024` → **`chunk_voxel_bytes_est ≈ 248 KB`** safety estimate.

### `MemoryBudget` (two-phase: `finalize_cpu` + `finalize_gpu`)

```cpp
struct MemoryBudget {
    size_t chunk_voxel_ram;      // loaded LOD0 palettes + lights + borders
    size_t chunk_mesh_cpu_ram;   // staging VB/IB, greedy temp buffers
    size_t gpu_mesh_vram;        // device-local terrain + water meshes
    size_t io_cache_ram;         // region decompress buffers, write coalesce
};

MemoryBudget finalize_cpu_budget(const CpuHardware& cpu) {
    const size_t ram = cpu.ram_bytes;
    return {
        .chunk_voxel_ram    = ram * 10 / 100,
        .chunk_mesh_cpu_ram = ram * 3 / 100,
        .gpu_mesh_vram      = 0,  // set in finalize_gpu
        .io_cache_ram       = ram * 2 / 100,
    };
}

void finalize_gpu_budget(MemoryBudget& mem, const GpuCaps& gpu, RenderPreset preset) {
    // VRAM-only — do NOT cap GPU mesh budget by system RAM
    size_t pct = (preset == RenderPreset::High) ? 45 : (preset == RenderPreset::Medium) ? 35 : 25;
    mem.gpu_mesh_vram = gpu.vram_bytes * pct / 100;
    // reserve headroom for swapchain + uniforms + ImGui (~512 MB min free target)
    const size_t reserve = 512 * 1024 * 1024;
    if (mem.gpu_mesh_vram > gpu.vram_bytes - reserve)
        mem.gpu_mesh_vram = gpu.vram_bytes > reserve ? gpu.vram_bytes - reserve : gpu.vram_bytes / 4;
}
```

```cpp
const size_t chunk_voxel_bytes_est = 31 * 1024 * 8;  // 248 KB
max_loaded_chunks = chunk_voxel_ram / chunk_voxel_bytes_est;
```

Mesh and VRAM pressure **further cap** effective streaming (§14): if `chunk_mesh_cpu_ram` exhausted, pause load spiral even if voxel budget has headroom.

### GPU mesh budget & eviction (§13)

`MemoryBudget::gpu_mesh_vram` is a **hard cap**, not only `RenderPreset` input:

```cpp
struct GpuMeshPool {
    size_t bytes_used;
    size_t bytes_budget;  // from MemoryBudget::gpu_mesh_vram
    // LRU over MeshGpuSlot — evict device-local VB/IB, keep CpuMeshBlob in RAM
};
```

On upload when `bytes_used + mesh_size > bytes_budget`: **LRU-evict** GPU slots (CPU mesh retained → re-upload). Eviction and unload use **deferred GPU free** (§5) — never `vkFreeMemory` while a snapshot or in-flight frame may reference the slot.

`max_loaded_chunks` alone does not prevent VRAM OOM on wide `render_radius_xz`.

TOML overrides per pool; `0` = auto.

---

## 1. Stack

| Component | Library | License | Source |
|-----------|---------|---------|--------|
| Language / API | C++20, Vulkan 1.2 | Khronos | [Vulkan Spec](https://registry.khronos.org/vulkan/) |
| Build | CMake 3.24+ | — | — |
| Config | **toml++** v3.4+ | MIT | [marzer/tomlplusplus](https://github.com/marzer/tomlplusplus) |
| Jobs | **enkiTS** | zlib | [github.com/dougbinks/enkiTS](https://github.com/dougbinks/enkiTS) |
| Window / Input | GLFW 3.4+ | zlib/libpng | [glfw.org](https://www.glfw.org/) |
| Math | GLM | MIT | [github.com/g-truc/glm](https://github.com/g-truc/glm) |
| ECS | Flecs | MIT | [github.com/SanderMertens/flecs](https://github.com/SanderMertens/flecs) |
| Physics | Jolt Physics | MIT | [github.com/jrouwe/JoltPhysics](https://github.com/jrouwe/JoltPhysics) |
| Worldgen noise | **FastNoise2** v0.10+ | MIT | [github.com/Auburn/FastNoise2](https://github.com/Auburn/FastNoise2) |
| UI | Dear ImGui | MIT | [github.com/ocornut/imgui](https://github.com/ocornut/imgui) |
| Vulkan helpers | volk, VMA | MIT | [GPUOpen VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| Shaders | shaderc (Vulkan SDK) | Apache 2.0 | LunarG SDK |
| Region compression | zstd (primary), LZ4 (dev fast path) | BSD | [facebook/zstd](https://github.com/facebook/zstd) |
| Logging | spdlog (async file + console) | MIT | [github.com/gabime/spdlog](https://github.com/gabime/spdlog) |
| Tests | **Catch2** v3 | BSL-1.0 | [github.com/catchorg/Catch2](https://github.com/catchorg/Catch2) |
| Profiling | **Tracy** (optional CMake flag) | BSD | [github.com/wolfpld/tracy](https://github.com/wolfpld/tracy) |
| Audio | miniaudio + engine voxel occlusion | Public domain / MIT-0 | [miniaud.io](https://miniaud.io/) |
| Networking (post-1.0) | ENet | MIT | [github.com/lsalzman/enet](http://sauerbraten.org/enet/) |

**Excluded:** Custom ECS, Vulkan RT extensions, external texture assets, mod/scripting (post-1.0), hand-rolled Perlin (replaced by FastNoise2).

### Windows / MSVC (through M10)

- Primary generator: **Visual Studio 2022**, x64, `/std:c++20`, `/permissive-`.
- Vulkan SDK: `VULKAN_SDK` for loader, validation layers (Debug), shaderc.
- CRT linking: consistent runtime (/MD recommended) across all `FetchContent` deps.
- MinGW: not supported in CI/build docs until after M10.

### Vulkan error strategy (before first `vk*` call)

```cpp
#define VK_CHECK(expr)                                              \
    do {                                                            \
        VkResult _r = (expr);                                       \
        if (_r != VK_SUCCESS) {                                     \
            SPDLOG_CRITICAL("Vulkan {} at {}:{}",                   \
                vk_result_string(_r), __FILE__, __LINE__);          \
            std::abort();                                           \
        }                                                           \
    } while (0)
```

- **`VK_CHECK`:** required paths (device, swapchain, submit) — fail fast.
- **Explicit `VkResult`:** optional paths (pipeline cache load, hot-reload compile) — graceful fallback.

### GPU device pick (Vulkan bootstrap)

**Required features** (abort if missing):

```cpp
VkPhysicalDeviceFeatures required {
    .multiDrawIndirect = VK_TRUE,  // vkCmdDrawIndexedIndirect
    .fillModeNonSolid  = VK_TRUE,  // debug wireframe
    .samplerAnisotropy = VK_TRUE,
};
```

Store in `DetectedHardware` / `GpuCaps`: features above + `max_memory_allocation_count`, `max_storage_buffer_range`, `min_uniform_buffer_offset_alignment`, `non_coherent_atom_size` (UBO ring alignment, staging flush ranges).

**Render preset (auto):**

```cpp
enum class RenderPreset { Low, Medium, High };

RenderPreset from_gpu(const GpuCaps& gpu) {  // called in finalize_gpu() only
    if (gpu.vram_bytes >= 8_GiB && gpu.discrete_gpu) return RenderPreset::High;
    if (gpu.vram_bytes >= 4_GiB) return RenderPreset::Medium;
    return RenderPreset::Low;
}
```

`RenderPreset` scales `StreamingConfig` multipliers and far-LOD aggressiveness (applied after step 9b).

---

## 2. Module layout

```
engine/
  core/       HardwareProbe, EngineConfig, JobSystem (enkiTS), Clock, FrameTimer, CrashHandler, Log
  platform/   GLFW window, Win32 paths, input mapping
  render/     Vulkan RHI, ShaderManager (+ hot-reload Debug), passes, sky
  world/      Voxel storage, chunks, streaming, meshing, border cache, lighting
  physics/    Jolt bridge, layer matrix, character, destruction debris
  gameplay/   Block registry, interaction, inventory, survival rules
  audio/      miniaudio, buses, 1m occlusion grid
  persist/    Region files (.vwr), async save/load
  procgen/    FastNoise2 terrain, biomes, structures
  net/        Command types, INetTransport stub
  ui/         ImGui inventory/HUD
game/
  main.cpp    Bootstrap: probe → config → engine
shaders/
assets/       default.toml, shaders (no textures)
```

### Thread model (enkiTS)

Separate task pools, sizes from `DetectedHardware`:

```cpp
struct ThreadConfig {
    int worker_threads;   // max(1, physical_cores - 2)
    int io_threads;       // SSD → 2, else 1
    int meshing_threads;  // min(worker_threads, 4)  // memory-bound cap
};

ThreadConfig from_hardware(const DetectedHardware& hw);
```

- **enkiTS** ([enkiTS](https://github.com/dougbinks/enkiTS)):
  - **`worker_threads`:** worldgen (M2b/M10), general tasks, procgen column fills.
  - **`meshing_threads`:** greedy mesh + collision mesh jobs only (memory-bound, cap 4).
- **miniaudio** uses its own thread — not counted in pool.
- **IO threads:** dedicated persist/load queue consumers.

TOML override (`0` = auto):

```toml
[engine.threads]
worker  = 0
io      = 0
meshing = 0
```

**Core components:** `Transform`, `Velocity`, `PlayerTag`, `Camera`, `ChunkSlotRef`, `ChunkDirty`, `MeshSectionHandle`, `RigidBodyProxy`, `Inventory`, `ItemStack`, `NetworkId` (0 = local).

Runtime contract (init, frame loop, snapshot, events, entities): **§3–§8**. Implement **§5 (snapshot) before M3**.

---

## 3. Application bootstrap & init order

Strict **linear init** in `Engine::startup()`; `Engine::shutdown()` reverses the list. No subsystem may call a later stage during its own `init`.

| Step | Subsystem | Depends on |
|------|-----------|------------|
| 1 | **CrashHandler** (SEH + minidump) | — |
| 2 | **Logging** (spdlog sinks, module loggers) | — |
| 3 | **HardwareProbe::run_cpu()** | — |
| 4 | **EngineConfig** load TOML + **`finalize_cpu()`** | 3 |
| 5 | **Tracy** (`ENGINE_TRACY` CMake option) | 4 |
| 6 | **Flecs** `ecs_world_t` + modules registered | 4 |
| 7 | **JobSystem** (enkiTS pools from `ThreadConfig`) | 4 |
| 8 | **Platform** (GLFW window, input) | 4 |
| 9 | **Renderer** (Vulkan instance/device/swapchain) → **`GpuCaps`** | 4, 8 |
| **9b** | **`EngineConfig::finalize_gpu(gpu_caps)`** | 9 |
| 10 | **ShaderManager** | 9 |
| 11 | **ChunkStore** + streaming (RAM, no GPU upload yet) | **Final `EngineConfig`** (after **9b**), Flecs, JobSystem |
| 12 | **MeshUploadQueue** (staging, VMA allocators) | 9, 11 |
| 13 | **ProcGen** (FastNoise2 graphs) | 4, 11 |
| 14 | **PersistService** (IO threads, region cache) | 4, 7, 11 |
| 15 | **Physics** (Jolt) — **from M6** | 9, 11 |
| 16 | **AudioEngine** (miniaudio) — **from M9** | 4, 11 |
| 17 | **UiHost** (ImGui → Renderer) | 9 |
| 18 | **GameSession** (load world, **`PlayerSpawnReadyGate`** §12) | 11–14 |

**Rules:**

- `Renderer` never reads `ChunkStore` during `init` — only stores `EngineConfig` + creates RHI.
- **`ChunkStore` / streaming must not init before step 9b** — `StreamingConfig` gets `RenderPreset` / GPU-derived tweaks in `finalize_gpu()`.
- `MeshUploadQueue` is the **only** path from CPU mesh jobs to GPU (avoids World→Vulkan back-edge at init).
- Deferred milestones (15–16) register stubs in Flecs until enabled.

---

## 4. Frame loop & simulation timing

### Rates

- **Simulation:** fixed **60 Hz** (`fixed_dt = 1/60 s`). Gameplay, physics, block logic, inventory rules.
- **Render:** **uncoupled** display rate (60 / 144 / variable). Rendering never advances simulation by itself.

### Accumulator (Gaffer pattern)

```cpp
sim_accumulator += frame_delta;  // clamped: min 0, max 4 * fixed_dt (spiral-of-death cap)
while (sim_accumulator >= fixed_dt) {
    run_fixed_tick();  // Flecs PreUpdate → FixedUpdate → PostFixed
    sim_accumulator -= fixed_dt;
}
alpha = sim_accumulator / fixed_dt;  // 0..1 for optional visual interpolation (camera only Phase 1)
```

- At **144 Hz** display (~6.94 ms/frame) with vsync off: `fixed_dt` ≈ 16.67 ms → sim advances **once every ~2–3 rendered frames** (not multiple sim steps per 144 Hz frame).
- At **30 Hz** display (~33 ms/frame): **2** fixed sim steps per rendered frame → effective 60 Hz sim.
- Cap `4 * fixed_dt` prevents spiral-of-death catch-up.

### Vsync & present

| `present_mode` (config) | Behavior |
|-------------------------|----------|
| `fifo` (default) | Vsync on; frame_delta ≈ refresh interval |
| `mailbox` | Low-latency vsync if supported |
| `immediate` | Unlocked FPS (benchmark / Debug) |

TOML: `[engine.render] present_mode = "fifo"`.

### Swapchain & resize (M0 — required)

```cpp
struct RenderConfig {
    uint32_t frames_in_flight = 2;  // 2 default; 3 if mailbox + GPU can overlap more
    // SnapshotCount = frames_in_flight + 1 (§5)
};
```

**Acquire / present handling:**

| Result | Action |
|--------|--------|
| `VK_SUCCESS` / `VK_SUBOPTIMAL_KHR` | Render frame (suboptimal → recreate when convenient) |
| `VK_ERROR_OUT_OF_DATE_KHR` | **`recreate_swapchain()`** — new extent, image views, may invalidate swapchain-sized targets |
| Extent **0×0** (minimized) | **Pause** render loop — no acquire, no present, no snapshot swap |

`recreate_swapchain()` order: `vkDeviceWaitIdle` → destroy framebuffer-dependent resources → `vkDestroySwapchainKHR` → recreate swapchain + image views + framebuffers → reset `current_image_index`.

**Resize policy:** **M0/M3:** `vkDeviceWaitIdle` on recreate is **allowed** (simplicity). **Post-prototype:** wait only per-frame submit fences + recreate swapchain-dependent resources — **no** `vkDeviceWaitIdle` in the normal frame loop (resize path may still idle until fence-based recreate exists).

Resize debounce: recreate on `glfwSetFramebufferSizeCallback` after 1-frame delay (avoid drag spam).

### Per-frame order (main thread)

```
1. Poll input / window
2. Run sim accumulator loop (zero or more fixed ticks)
3. Join mesh jobs → `MeshUploadQueue::flush_uploads()` (recording upload CB, §12)
3b. `GpuDeferredFreeQueue::process_completed()` — retire slots whose frames finished
4. RenderBuild → fill snapshot `write_slot` (fence-safe, §5); precompute per-`DrawSection` `model_translation` (§10.5)
5. `UiHost::new_frame()` + build ImGui draw lists (must exist **before** world pass if UI is in same CB)
6. `Renderer::record_and_submit(read snapshot)` — upload barriers + opaque + sky + water (M4+) + **UI** in one CB (§13); M3 thin path: opaque only
7. Present
8. Tracy FrameMark
```

**CPU frame timer:** `FrameTimer` logs wall time per stage; warns if total > 16.6 ms at target 60 Hz sim budget.

**GPU profiling (from M3):** `VkQueryPool` timestamp queries per pass (terrain, water, sky, ui); readback previous frame.

---

## 5. WorldRenderSnapshot (Sim ↔ Render contract)

**Required before M3.** Defines exactly what simulation exposes to Vulkan — no `ChunkStore` pointers on render thread.

### Snapshot count vs. frames in flight

`frames_in_flight` comes from `RenderConfig` (§4), typically **2** (optionally 3).

```cpp
// Invariant: never reuse a snapshot slot for RenderBuild until GPU finished with it
const uint32_t SnapshotCount = frames_in_flight + 1;  // fif=2 → 3 slots minimum
WorldRenderSnapshot snapshots[SnapshotCount];
```

**Per-slot GPU fence:** `snapshot_fence[slot]` — created with **`VK_FENCE_CREATE_SIGNALED_BIT`** so frame 0 never blocks on an unsignaled fence. Wait before reusing a slot; first use is immediately valid.

**Rotation algorithm:**

1. Pick `write_slot` where `snapshot_fence[write_slot]` is signaled (or never used).
2. **RenderBuild** fills `snapshots[write_slot]` only.
3. `read_slot = write_slot`; record commands reading `snapshots[read_slot]`.
4. Submit with fence → `snapshot_fence[read_slot]`.
5. Next frame: choose new `write_slot` ≠ in-flight slots (count = `frames_in_flight`).

**Do not** swap two indices blindly if `SnapshotCount == 2` and `frames_in_flight == 2` — that is exactly the GPU race. **`SnapshotCount` must be `frames_in_flight + 1`**, or block RenderBuild on the fence of the slot being overwritten.

**Vector reuse:** `opaque_sections.clear(); water_sections.clear();` — **keep capacity** across frames (no per-frame `vector` realloc storm).

**Lifetime rules:**

1. No `ChunkStore` pointers in snapshot — handles/indices only.
2. Renderer treats active `read_slot` as **immutable** from record through fence signal.
3. `DrawSection` uses **chunk origin + section offset − render_origin** (§10.5) — not rebaked vertex positions.

### GPU buffer lifetime (separate from snapshot fences)

Snapshot fences protect **`WorldRenderSnapshot` structs**, not the **`VkBuffer`** behind `vertex_buffer_id` / `index_buffer_id`.

**Failure mode:** Frame N−1 in-flight references `slot_id=42` → Frame N unloads chunk → immediate `vkDestroyBuffer` → GPU UAF.

**`GpuDeferredFreeQueue` (required M3):**

Free when the **submit fence of the last frame that referenced the slot** has signaled — not a coarse `current_frame + frames_in_flight` guess (skipped frames, minimized window, stalled GPU, no-present frames).

```cpp
struct PendingGpuFree {
    uint32_t slot_id;
    uint64_t last_used_frame;  // frame_index when slot last appeared in submit
};

// Same fence array as frame-in-flight submit (one fence per submitted frame_index)
VkFence frame_submit_fence[fif];

void enqueue_free(uint32_t slot_id, uint64_t last_used_frame) {
    pending.push({ slot_id, last_used_frame });
}

void process_completed() {
    for (auto& p : pending) {
        const uint32_t ring = uint32_t(p.last_used_frame % fif);
        if (vkGetFenceStatus(device, frame_submit_fence[ring]) == VK_SUCCESS) {
            actually_destroy_vk_buffers(p.slot_id);
            // remove p from pending
        }
    }
}
```

On each submit: `frame_submit_fence[frame_index % fif]` reset → record → submit → signals when GPU finishes **that** frame's CB (including draws referencing mesh slots).

**Applies to:** chunk unload (§7), `GpuMeshPool` LRU eviction (§0.1), mesh regrow bucket replace (§13), swapchain recreate (wait idle then flush queue).

**Rule:** No slot ID reuse until `actually_destroy` ran. New uploads get **new** slot IDs (monotonic generation per slot table).

**LRU eviction guards (never evict if):**

- `visible_this_frame` (in current snapshot lists)
- `referenced_by_in_flight_frame` (submit fence for `last_used_frame` not signaled)
- `upload_pending` (copy recorded this frame, draw depends on it)

**`FrameGarbage`:** per-submit list of `{slot_id, frame_index}` appended to deferred queue — snapshot fence alone is insufficient.

### Per-frame GPU write ring (same class of bug as snapshots)

CPU snapshot rings do **not** protect GPU buffers filled from those snapshots. All **CPU-written GPU resources** each frame:

```cpp
struct PerFrameGpuWrites {
    VkBuffer indirect_draw_buffer;   // vkCmdDrawIndexedIndirect source
    VkBuffer frame_ubo;              // view, proj, render_origin, sun
    // optional: draw_section_ssbo
};
PerFrameGpuWrites per_frame[fif];  // index = frame_index % fif
```

**RenderBuild** writes only `per_frame[frame_index % fif]`. **Record/submit** reads the same slot. Frame submit fence(s) use **`VK_FENCE_CREATE_SIGNALED_BIT`** (same rule as snapshot fences, §5) so lap 0 never deadlocks on “wait for prior fence.”

**Never** refill a single global indirect buffer while GPU may still consume the previous frame's draws.

### Snapshot contents (read-only, plain data)

```cpp
struct WorldRenderSnapshot {
    uint64_t frame_index;
    uint64_t sim_tick;              // last completed fixed tick
    glm::vec3 render_origin;        // origin rebase (§9)
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 sun_dir_intensity;    // from DayNightCycle at sim_tick
    glm::vec4 ambient_fog;          // stub uniform until GI

    // Opaque terrain
    std::vector<DrawSection> opaque_sections;
    // Water (§18) — separate pass list
    std::vector<DrawSection> water_sections;

    // Later: debris instances, debug draws
};

struct DrawSection {
    ChunkCoord coord;
    uint8_t    section_index;       // 0..7, §10.5
    glm::vec3  model_translation;   // chunk*32 + section_offset*16 - render_origin (built in RenderBuild)
    uint32_t   indirect_index;
    uint32_t   vertex_buffer_id;
    uint32_t   index_buffer_id;
    uint32_t   index_count;
    glm::vec3  cull_min;            // AABB in render space (same as view: absolute - render_origin)
    glm::vec3  cull_max;
};
```

**Not in snapshot:** `Chunk*`, Flecs entities, Jolt bodies, mutable palettes.

### RenderBuild responsibilities (main thread, after job join)

1. Frustum-cull in **render space** (`absolute - render_origin`); store `cull_min`/`cull_max` on each `DrawSection` (§13).
2. Copy/transform camera matrices relative to `render_origin`.
3. Pull completed mesh data from `MeshUploadQueue` → assign `vertex_buffer_id` / indirect slot.
4. Split sections with water faces into `water_sections` (or tag in mesh — builder fills both lists).
5. Fill chosen `write_slot`; submit with fence tied to that slot (§ above).

### Simulation rules

- Fixed tick mutates **ChunkStore** + Flecs only — never Vulkan handles.
- `MeshSectionHandle` on entities is opaque `uint32_t` slot ID; GPU mapping resolved only in RenderBuild.

---

## 6. Events (Flecs-native)

**No parallel callback bus** for gameplay/world events — use **Flecs observers + custom events** for consistency.

### Custom events (`ecs_event`)

```cpp
struct EvtOriginShift { glm::vec3 new_origin; };
struct EvtChunkLoaded  { ChunkCoord coord; };
struct EvtChunkUnloaded { ChunkCoord coord; };
struct EvtChunkMeshReady { ChunkCoord coord; uint8_t section; };
```

Emit via `ecs_event_desc_t` from main thread (or job completion marshaled to main queue).

### Observers (examples)

| Trigger | Observer action |
|---------|-----------------|
| `EvtOriginShift` | Update `render_origin` only — **no remesh** (§9); offset Jolt bodies; shift audio listener; refresh audio occlusion origin; update `world.meta` `last_render_origin` |
| `ChunkDirty` added | Enqueue **render + collision + light** queues; propagate `SectionBorderCache` (§12) |
| `EvtChunkUnloaded` | `enqueue_free(slot_ids)` — **deferred** (§5); drop from next snapshot lists |
| `MeshSectionHandle` removed | `enqueue_free(id)` — never immediate `vkDestroy` |

### Async → main marshaling

IO and mesh jobs push **`MainThreadQueue`** — **pre-allocated command pool**, not `std::function` per completion:

```cpp
enum class MainCmdType : uint8_t { ChunkMeshReady, ChunkLoaded, IoComplete, /* ... */ };
struct MainCmd { MainCmdType type; /* POD union payload */ };

// Ring buffer or fixed vector<MainCmd> + count; workers push, main thread drains step 3–4
```

Avoids heap alloc on every mesh/IO completion. **Do not** touch Flecs world from worker threads.

---

## 7. Entity lifecycle (chunks ↔ Flecs)

### Chunk entities

- **One Flecs entity per loaded LOD0 chunk** in `ChunkStore`.
- Components: `ChunkCoord`, `ChunkSlotRef`, optional aggregate `ChunkMeshTag`.

```cpp
struct ChunkSlotRef {
    uint32_t slot_id;
    uint32_t generation;  // bumped on reuse of slot
};
```

**Load sequence:**

1. `ChunkStore::allocate(coord)` → slot + generation.
2. `ecs_entity_t e = ecs_new(world)` + add `ChunkCoord` component + `ChunkSlotRef` — store `e` in `ChunkStore::coord_to_entity[coord]` (authoritative map). **Do not** use `chunk_coord_hash` as entity id (collision risk).
3. Idiomatic Flecs lookup: query `(ChunkCoord, ChunkSlotRef)` or map from store — single source of truth in `ChunkStore`.
4. Procgen or disk fill palette; initialize `SectionOccupancy` from solid blocks.
5. Schedule mesh jobs → on complete, emit `EvtChunkMeshReady`.

**Unload sequence (order fixed — prevents UAF):**

1. Remove draw data from next snapshot (exclude in RenderBuild).
2. `ecs_delete` chunk entity → observer **`enqueue_free`** all mesh slot IDs (§5).
3. `ChunkStore::free(coord)` bumps generation; slot enters LRU free list.

### Resolving references

- Gameplay **must not** cache `ChunkSlotRef` across frames without validating `generation`.
- Preferred: store `ChunkCoord` + `BlockPos`; resolve via `ChunkStore::try_get(coord) → {slot*, gen}`.

### Player entity

- Created at session start; **never** streamed.
- `PlayerTag` + `Inventory` + `Transform` persist via `player.dat` (§15).

### Debris (M11+)

- Separate entities `DebrisTag` + `RigidBodyProxy`; not tied to chunk unload (despawn by distance).

---

## 8. Logging, profiling, crash handling, tests

### Logging (M0)

- **Async rotating file** + colored stdout (Debug).
- Path: `%LOCALAPPDATA%/VoxelEngine/logs/engine.log` (Win32 `SHGetKnownFolderPath`).
- Module loggers: `world`, `render`, `persist`, `physics`, `audio` — levels from TOML:

```toml
[log]
default = "info"
world   = "debug"
render  = "warn"
file_mb = 16
```

- Structured save/load: `SPDLOG_INFO("save_region region=({},{}) chunks={}", rx, rz, n)`.

### Profiling

- **CPU:** `FrameTimer` per stage; Tracy zones (`ZoneScoped`) on hot paths (meshing, greedy, upload).
- **GPU:** Vulkan timestamp queries per render pass (M3+).
- CMake: `ENGINE_TRACY=ON` links `TracyClient`; default **OFF** in Release, **ON** in RelWithDebInfo.

### Crash handler (M0, always Release)

- `SetUnhandledExceptionFilter` → `MiniDumpWriteDump` to `%LOCALAPPDATA%/VoxelEngine/crashes/<timestamp>.dmp`.
- Log path printed before exit.
- ~2 files in `engine/core/CrashHandlerWin32.cpp`.

### Tests (M0 CMake target `engine_tests`)

- **Catch2** v3 `FetchContent`, `ctest` on CI/local.

**Required test matrix (implement as milestones unlock systems):**

| Area | Cases |
|------|--------|
| **ChunkCoord** | negative world block → correct `(cx,cy,cz)` + local; boundaries **31/32, -1/-32**; negative `cy` (below sea) |
| **BlockState** | pack/unpack id + props; `BLOCK_ID_MASK` / `BLOCK_PROP_MASK` roundtrip |
| **Palette** | air-only section minimal palette; mixed blocks save/load roundtrip |
| **Indexing** | `block_index` / `section_index` roundtrip; `pack_vertex` corners 0..16 |
| **Meshing** | solid 1³ cube → 6 faces; adjacent solids hide internal face; `BorderCell` light at edge |
| **Draw** | two sections in one chunk → distinct `model_translation` |
| **Lighting** | torch flood radius deterministic; removed torch invalidates; **cross-chunk** border torch |
| **Collision** | `occupancy_at` cross-chunk; streaming edge = solid |
| **section_index** | roundtrip `section_index` ↔ `(sx,sy,sz)` §10.5 |
| **floor_div** | `floor_div(-1,8)==-1`, `positive_mod(-1,8)==7` |
| **Persistence** | `CHUNK_MODIFIED_BY_PLAYER` survives save/load; CRC fail vs version mismatch §17; atomic rename keeps previous file |
| **BlockMutation** | stale `old_state` → reject, no dirty |
| **Palette** | 4097th unique state rejected §11 |
| **Streaming** | unload bumps `generation`; stale `ChunkSlotRef` rejected |
| **Origin rebase** | chunk+local stable; **no** `ChunkDirty` storm; `render_origin` uniform changes only |

Integration: save/load vertical slice (§25) by **M5**.

---

## 9. World coordinate system

**Block indices:** `int32` **chunk-local** within a section (0..15 per axis).

**Chunk key:** `using ChunkCoord = glm::ivec3;` — **`(cx, cy, cz)` required from M0** (caves, vertical worldgen, 3D streaming, lighting).

**Vertical extent (config, not hardcoded):**

```cpp
struct WorldConfig {
    int chunk_height_min = -4;  // chunk units → −128 m at 32 m/chunk
    int chunk_height_max =  8;  // → +256 m
    bool finite_bounds = false; // default infinite horizontal + vertical clamp via min/max
    // optional AABB for test maps when finite_bounds = true
};
```

Worldgen skips / air-fills chunk columns outside `[chunk_height_min, chunk_height_max]`.

**Rendering / physics:** Camera-relative **origin rebasing** when player crosses `REBASE_RADIUS` (from config, default 512 m):

- `render_origin = floor(player_pos / REBASE_RADIUS) * REBASE_RADIUS`
- GPU / Jolt use `float` **relative to `render_origin`** — mesh vertices stay **section-local** (0..16); chunk world offset applied per draw (§13).
- **`EvtOriginShift` (§6):** update uniform `render_origin`, Jolt body translations, debris offsets, audio listener — **does not** enqueue remesh / `ChunkDirty` for all chunks.

**Why no remesh on rebase:** Greedy verts use packed local coordinates; `DrawSection` push constant = `glm::vec3(coord * 32) - render_origin`. Rebasing only changes that transform — remeshing hundreds of chunks after each 512 m cross would blow `frame_budget_ms` and cause visible popping.

**No** GPU double precision; **no** global `int64` block coords in Phase 1.

### `WorldPosition` (runtime + save — not absolute `glm::vec3`)

`render_origin` fixes **rendering** precision; player logic uses chunk-local form:

```cpp
struct WorldPosition {
    ChunkCoord chunk;
    glm::vec3  local;  // meters within chunk, 0..32 per axis
};
```

- **Player `Transform` / physics / raycast** use `WorldPosition` (M1+).
- Float world pos for camera: `world = vec3(chunk)*32 + local - render_origin` (derived each frame).
- `player.dat` stores `chunk` + `local` only (§17) — matches runtime.

### Integer division helpers (canonical — negative coords)

Used for block→chunk, region index (§17), streaming. **Do not** use raw C++ `/` and `%` on negatives.

```cpp
// b > 0
inline int floor_div(int a, int b) {  // b > 0
    int q = a / b, r = a % b;
    return (r != 0 && r < 0) ? q - 1 : q;
}

inline int positive_mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}
```

Examples: `floor_div(-1, 8) == -1`, `positive_mod(-1, 8) == 7`. Catch2 required (§8).

```cpp
inline ChunkCoord block_to_chunk(int wx, int wy, int wz) {
    return {
        floor_div(wx, 32), floor_div(wy, 32), floor_div(wz, 32)
    };
}
inline glm::ivec3 block_local_in_chunk(int wx, int wy, int wz) {
    return { positive_mod(wx, 32), positive_mod(wy, 32), positive_mod(wz, 32) };
}
```

---

## 10. Block registry (M2 prerequisite)

Stable IDs for meshing, worldgen, networking, persistence.

```cpp
enum class BlockId : uint16_t {
    Air = 0, Stone, Dirt, Grass, Sand, Wood, Leaves,
    Torch, Water, Gravel, Coal_Ore, Iron_Ore,
    COUNT
};

struct BlockDef {
    std::string_view name;
    bool opaque;
    bool solid;
    uint8_t  light_emission;  // 0–15
    MaterialId material_id;
    float    hardness;        // break time; 0 = indestructible
};

inline constexpr std::array<BlockDef, size_t(BlockId::COUNT)> BLOCK_REGISTRY = {{
    { "air",   false, false, 0, MAT_NONE,  0.0f },
    { "stone", true,  true,  0, MAT_STONE, 7.5f },
    // ...
}};
```

### `BlockState` bit layout (fixed now)

Palette stores **`BlockState` as `uint16_t raw`** — not separate fields on disk.

```cpp
struct BlockState {
    uint16_t raw = 0;
};

constexpr uint16_t BLOCK_ID_MASK   = 0x0FFF;  // bits 0..11  → 4096 BlockId values
constexpr uint16_t BLOCK_PROP_MASK = 0xF000;  // bits 12..15 → 16 variants per block

inline BlockId    block_id(BlockState s) { return BlockId(s.raw & BLOCK_ID_MASK); }
inline uint8_t    block_props(BlockState s) { return (s.raw & BLOCK_PROP_MASK) >> 12; }
inline BlockState make_block_state(BlockId id, uint8_t props) {
    return { uint16_t((uint16_t(id) & BLOCK_ID_MASK) | ((props & 0xF) << 12)) };
}
```

**Props nibble (per `BlockId`, registry documents meaning):**

| BlockId | Prop bits | Meaning (Phase 1 / reserved) |
|---------|-----------|------------------------------|
| Water | 0–3 | fluid level 0–15 (static in Phase 1, default full) |
| Torch | 0–3 | facing enum |
| Wood/log | 0–3 | axis |
| Generic | 0–3 | rotation / variant / snow cover (future) |

Protocol versions pin **`BlockId` numeric values**; prop semantics versioned in `world.meta` `block_schema_version`.

---

## 10.5 Mesh vertices & canonical indexing (binding before M3)

**Mandatory before any meshing code.** Greedy mesh places vertices on **block corners**. A 16³ section has blocks `0..15` but corner coordinates **`0..16` inclusive** (17 values) — **5 bits per axis**, not 4.

### Canonical index order (one rule everywhere)

**X fastest, Z middle, Y slowest** — matches §17 `local_index = lx + 8*lz + 64*ly` and horizontal layer locality.

```cpp
constexpr int SECTION_DIM = 16;
constexpr int CHUNK_SECTIONS_PER_AXIS = 2;

// Block in section (x,y,z each 0..15):
inline int block_index(int x, int y, int z) {
    return x + SECTION_DIM * z + SECTION_DIM * SECTION_DIM * y;
}

// Section in chunk — bits encode (sx, sz, sy): X=bit0, Z=bit1, Y=bit2
inline int section_index(int sx, int sy, int sz) {
    return sx + 2 * sz + 4 * sy;  // NOT sx+2*sy+4*sz — sy is slow axis (×4)
}
// Returns glm::ivec3(sx, sy, sz) in normal (x,y,z) order — bit decode order differs from index formula
inline glm::ivec3 section_coord_from_index(int idx) {
    return { (idx     ) & 1, (idx >> 2) & 1, (idx >> 1) & 1 };
}
```

| Location | Formula |
|----------|---------|
| Meshing / `blocks[]` | `x + 16*z + 256*y` |
| `section_index()` | `sx + 2*sz + 4*sy` |
| Region `local_index` (§17) | `lx + 8*lz + 64*ly` |

Catch2: roundtrip `block_index` ↔ `(x,y,z)` and `section_index` ↔ `(sx,sy,sz)` including negatives at world level (§8).

### `packed_position_normal` (uint32)

```cpp
// bits  0..4  : x corner 0..16
// bits  5..9  : y corner 0..16
// bits 10..14 : z corner 0..16
// bits 15..17 : Face 0..5 (+X,-X,+Y,-Y,+Z,-Z)
// bits 18..31 : reserved (GI / future)

constexpr uint32_t POS_BITS = 5;
constexpr uint32_t POS_MASK = (1u << POS_BITS) - 1;

enum class Face : uint32_t { PX=0, NX=1, PY=2, NY=3, PZ=4, NZ=5 };

inline uint32_t pack_vertex(uint32_t x, uint32_t y, uint32_t z, Face f) {
    return (x & POS_MASK)
         | ((y & POS_MASK) << 5)
         | ((z & POS_MASK) << 10)
         | ((uint32_t(f) & 7u) << 15);
}
```

### `TerrainVertex` — stride **8 bytes** (fixed)

```cpp
struct TerrainVertex {
    uint32_t packed_position_normal;
    uint16_t material_id;
    uint8_t  ao;     // 0..3 typical (2 bits used)
    uint8_t  light;  // sky high nibble | block low nibble, each 0..15
};
static_assert(sizeof(TerrainVertex) == 8);
```

`TerrainVertexCompact` (AO/light in upper bits of uint32) deferred post-prototype.

### Section offset in draw (sections must not stack)

Vertices are **section-local** `0..16`. Without offset, all 8 sections draw at the same chunk origin.

```cpp
glm::ivec3 sc = section_coord_from_index(section_index);
glm::vec3 chunk_origin_world = glm::vec3(ChunkCoord) * 32.0f;
glm::vec3 section_offset     = glm::vec3(sc) * 16.0f;
glm::vec3 model_translation  = chunk_origin_world + section_offset - snapshot.render_origin;
// stored in DrawSection::model_translation (§5)
```

Origin rebase: only `render_origin` changes — **no remesh** (§6, §9).

---

## 11. Voxel data model

### Chunk LOD0 (32³)

- **8 sections** per chunk, each **16³** blocks.
- Per section:
  - `palette[]`: unique `BlockState` entries (`uint16_t raw`, §10)
  - `blocks[4096]`: **`uint16_t`** indices into `palette[]`

**Palette invariants (hard):**

| Rule | Value |
|------|--------|
| Max unique states per section | **4096** (= block count) |
| `blocks[]` index type | **`uint16_t`** (fits 0..4095) |
| New unique state when palette full | **Reject** mutation (or repack section offline — not in hot path) |

A section cannot hold more than 4096 distinct `BlockState` values; palette growth beyond 4096 is impossible by construction.
  - `skyLight[4096]`, `blockLight[4096]` (`uint8`)
  - **`SectionOccupancy`** — 16³ solid bitmask for physics (§12, §20)
  - **`SectionBorderCache`:** 1-block ghost faces (§12)

### Keys and addressing

- `ChunkCoord` `(cx, cy, cz)` → world origin `(cx*32, cy*32, cz*32)` meters (1 m/voxel).
- `BlockPos` = `ChunkCoord` + block offset within section (`block_index` §10.5).

### Destruction adjunct

- `FractureVolume`: temp up to 32³ for cluster extraction.
- `DebrisEntity`: render/collision mesh + Jolt body; voxels removed from terrain palette.

### Why palette + flat section (not octree)

Linear greedy meshing, Anvil-like persistence, O(1) access. RAM budgeting: **§0.1** (~31 KB/section, ~248 KB/chunk voxel data).

---

## 12. Meshing & update queues

- **Greedy meshing** on six axes per section.
- **Ghost cells:** mesher reads **`SectionBorderCache` only** — no live neighbor mutex.

```cpp
struct BorderCell {
    BlockState block;
    uint8_t    sky_light;
    uint8_t    block_light;
};

struct SectionBorderCache {
    std::array<BorderCell, 16 * 16> face[6];
    bool dirty = true;
};
```

Mesher reads **block + light** at section edges (no lighting seams). On load/dirty: refresh borders + mark six neighbor caches dirty.

### Section dirty coalescing

```cpp
enum SectionDirtyBits : uint8_t {
    Dirty_Render    = 1 << 0,
    Dirty_Collision = 1 << 1,
    Dirty_Light     = 1 << 2,
    Dirty_Persist   = 1 << 3,
};
// Per (chunk, section): enqueue queue X only if bit not set; clear bit when job starts
```

Prevents mining the same section from flooding queues with duplicate jobs.

- AO + sky/block light baked at mesh time (after light flood invalidation).

### Cross-chunk block light

`SectionBorderCache` covers **meshing** neighbors only. **Light** must cross chunk faces explicitly:

1. On torch place/break at border block: enqueue flood seed in **both** sections (local + neighbor section via `ChunkStore::neighbor_section(coord, face)`).
2. `LightUpdateQueue` propagates within section, writes into **edge voxels** of neighbor chunk's `blockLight[]` when light level at face > neighbor's current.
3. Mark **both** sections `ChunkDirty` + enqueue **RenderRemesh** on both (and collision if solid changed).

Without step 2–3: hard **1-block light seam** at every chunk boundary.

Sky light: same pattern for height/sky propagation across horizontal chunk borders (vertical column continuity).

### Remesh dispatcher (frame budget, not fixed count)

```cpp
struct MeshingConfig {
    float frame_budget_ms;  // stop when exceeded
    int   max_per_frame;    // hard safety cap
};

MeshingConfig from_hardware(const CpuHardware& hw) {
    return {
        .frame_budget_ms = hw.physical_cores >= 8 ? 4.0f : 2.0f,
        .max_per_frame   = hw.physical_cores >= 8 ? 16   : 6,
    };
}
```

**`RenderRemeshQueue`** drains until `MeshingConfig` budget/cap hit.

TOML:

```toml
[engine.meshing]
frame_budget_ms = 0   # 0 = auto
max_per_frame   = 0
```

### Separate work queues (do not share one dispatcher)

Block changes fan out to **four queues** with independent budgets:

| Queue | Work | Default budget |
|-------|------|----------------|
| **`RenderRemeshQueue`** | Greedy → CPU VB, snapshot upload | `MeshingConfig` (ms + cap) |
| **`CollisionRemeshQueue`** | Simplified greedy → Jolt `MeshShape` | separate `collision_budget_ms` (≥ render priority near player) |
| **`LightUpdateQueue`** | Block/sky flood per section | `light_budget_ms` |
| **`PersistDirtyQueue`** | Region sector encode + IO enqueue | `io_threads`, not frame-budgeted |

### Global priority (cross-queue, same chunk/section)

When multiple queues have pending work for the same target:

1. **Raycast target block** (break/place line of sight)
2. **Player-near collision** (within `collision_priority_radius` chunks)
3. **Visible render** (frustum + `render_radius`)
4. **Light** (if torch/sky dirty)
5. **Far render / collision**
6. **Persist flush** (coalesced, never starve — min 1 sector/frame if dirty set non-empty)

```cpp
struct CollisionMeshingConfig {
    float frame_budget_ms;
    int   max_sections_per_frame;
    int   collision_priority_radius;  // chunks
    int   collision_max_stale_ticks;  // full MeshShape swap deadline for *far* chunks
};
```

### Player terrain collision (decision before M6)

**Problem:** `CharacterVirtual` collides only against **Jolt bodies**. It does **not** read `SectionOccupancy`. Clearing a bitmask does not affect CharacterVirtual.

**Rejected approaches:**

- (b) Rebuild Jolt box per changed voxel same-tick — expensive; shape becomes authoritative, not the grid.
- CharacterVirtual vs async terrain `MeshShape` only — same-tick break/place breaks (ghost floor or fall-through).

**Phase 1 decision — (a) `VoxelCapsuleResolver`:**

| Role | System |
|------|--------|
| **Player vs terrain** | Custom **swept AABB/capsule** vs `SectionOccupancy` in loaded chunks (same-tick bit updates) |
| **Player vs debris / dynamics** | Jolt `CharacterVirtual` or rigid cast — **M11+** when debris exists; M6 may stub |
| **Debris vs terrain** | Jolt static `MeshShape` from `CollisionRemeshQueue` (async OK) |

```cpp
// gameplay/physics/VoxelCapsuleResolver.hpp
struct Capsule {
    glm::vec3 center;  // render-origin-relative or WorldPosition + derive
    float radius, half_height;
};

struct VoxelMovementConfig {
    float step_height = 0.6f;   // auto step-up for block ledges (1.0f = full block, Minecraft-like)
    float skin_width  = 0.03f; // shrink capsule slightly for stable sliding
    int   max_solver_iterations = 4;
};
// move_and_slide: sample occupancy along capsule sweep — no Jolt for terrain
// After horizontal resolve: if blocked and floor within step_height, try +step_height snap (step-up)
```

**Phase 1:** document **step-up ≤ `step_height`** (default 0.6 m). Without step-up, 1-block ledges require jump only — acceptable for debug, poor for survival feel.

### `occupancy_at` — cross-chunk + streaming edge (required)

Capsule (~0.3 m radius, ~1.8 m tall) spans **multiple sections/chunks** at boundaries. Every sample uses the same path as block logic — not section-local-only reads.

```cpp
enum class OccupancyPolicy : uint8_t {
    SolidIfChunkMissing = 0,  // Phase 1 default — invisible wall at streaming edge
    AirIfChunkMissing   = 1,  // debug / noclip only
};

// World block integer position (absolute blocks, not render-relative)
inline bool occupancy_at(int wx, int wy, int wz, ChunkStore& store,
                         OccupancyPolicy policy = OccupancyPolicy::SolidIfChunkMissing)
{
    const ChunkCoord cc = block_to_chunk(wx, wy, wz);  // §9 floor_div
    const auto* chunk = store.try_get(cc);
    if (!chunk) {
        return policy == OccupancyPolicy::SolidIfChunkMissing;
    }
    const glm::ivec3 local = block_local_in_chunk(wx, wy, wz);  // 0..31
    const glm::ivec3 sec  = { local.x >> 4, local.y >> 4, local.z >> 4 };  // 0..1
    const glm::ivec3 blk  = { local.x & 15, local.y & 15, local.z & 15 };
    return chunk->section(sec).occupancy_bit(blk);  // + BLOCK_REGISTRY solid check
}
```

**Cross-chunk:** implicit via `block_to_chunk` — no special case when capsule crosses `cx`/`cy`/`cz` borders as long as **both** chunks are loaded.

**Out-of-loaded policy (Phase 1):** `SolidIfChunkMissing` — player cannot walk/fall into unloaded volume (survival-safe streaming skirt). TOML override only for dev.

**Spawn / load exception (`PlayerSpawnReadyGate`):** `SolidIfChunkMissing` would trap the player in a “solid unloaded bubble” at world start. **`GameSession` must not enable movement** until:

- spawn chunk loaded and meshed (occupancy valid),
- foot + head neighborhood chunks loaded (at least 3×3×3 chunk shell around spawn feet),
- `occupancy_at` samples around spawn feet/head succeed without relying on missing chunks.

Until gate passes: freeze capsule / no `VoxelCapsuleResolver` input; show loading only.

**Sweep:** `VoxelCapsuleResolver::move_and_slide` samples capsule volume (grid of block centers or AABB corner tests) calling `occupancy_at` per world block — same function for raycast foot probe.

Catch2: capsule straddling two chunks; sample at edge with neighbor unloaded → solid (§8); spawn gate blocks movement until neighborhood loaded.

| Event | Same fixed tick |
|-------|-----------------|
| **Break** solid | Clear occupancy bit → `occupancy_at` returns air **immediately** |
| **Place** solid | Set bit → solid **immediately** |

`CollisionRemeshQueue` updates Jolt meshes for **debris vs terrain** — **not** player locomotion.

See §20.

## 13. Renderer geometry

**Strategy:** CPU greedy mesh → section VB/IB → **`vkCmdDrawIndexedIndirect`** (requires `multiDrawIndirect`).

### Vertex format

See **§10.5** (`TerrainVertex`, 5-bit corners, 8-byte stride). GI adds fields in a **new vertex layout version**, not by widening Phase 1 stride ad hoc.

**Draw transform:** `DrawSection::model_translation` (§5, §10.5) — chunk + section offset − `render_origin`.

### Draw batching

- **`VkBuffer indirect_draw_buffer[fif]`** — filled in RenderBuild into `per_frame[frame_index % fif]` (§5).
- CPU frustum + LOD in **render space**; `DrawSection::cull_min/max` precomputed there.
- Camera UBO / frame SSBO: same **per-frame ring** — never single-buffered.

### GPU mesh allocation (variable size — not fixed ring slots)

Greedy output size varies; edits can **grow** a section mesh. **Do not** use a fixed-size ring slot per chunk without overflow strategy.

**`MeshGpuSlot` strategy:**

| Approach | Phase 1 choice |
|----------|----------------|
| Size buckets (4/16/64/256 KB) + freelists | **Yes** — allocate nearest bucket, split pools by bucket |
| VMA virtual allocator + defrag pass | Deferred post-prototype |
| Fixed slot = max mesh size | **No** — wastes VRAM |

On regrow: `enqueue_free(old_slot)` → allocate **new** slot id → re-upload. Track `bytes_used` against `GpuMeshPool` (§0.1); LRU-evict via deferred free.

**Collision meshes** use separate pool (often smaller, simpler greedy).

### `MeshUploadQueue` — transfer sync + staging lifetime

Upload path is **graphics queue only** in Phase 1 (no dedicated transfer queue).

**Staging buffers:** `StagingRing[fif]` — one staging slot per frame-in-flight.

- **Slot size:** `staging_slot_bytes = chunk_mesh_cpu_ram / fif` (from `MemoryBudget`, §0.1).
- **Lifetime:** allocation valid until that frame's submit **fence** signals (fences created **signaled**, §5).
- **Overflow:** if uploads in one frame exceed slot size → queue remainder on **`pending_uploads`** for next frame(s); never stomp the in-flight staging buffer.
- Not covered by `GpuDeferredFreeQueue` (device-local mesh slots only).

Per frame:

```
1. Use staging = StagingRing[frame_index % fif] (reuse only after prior fence done)
2. CPU memcpy mesh → staging
3. Same command buffer as draws:
     vkCmdCopyBuffer(staging → device_local[slot])
     vkCmdPipelineBarrier TRANSFER_WRITE → VERTEX_INPUT | INDEX_INPUT
     ... terrain draws using indirect_draw_buffer[frame_index % fif] ...
4. Submit with fence; on signal, staging slot eligible for reuse
```

**Never** record draws without barrier after copy. **Never** single global indirect/staging buffer written every frame while GPU reads in-flight.

### Host-visible memory (staging + per-frame UBO)

All CPU writes to `HOST_VISIBLE` memory must follow Vulkan coherency rules (`GpuCaps::non_coherent_atom_size`):

```cpp
void host_write(void* mapped, size_t offset, size_t size, VkDeviceMemory mem, const GpuCaps& caps) {
    std::memcpy(static_cast<char*>(mapped) + offset, src, size);
    if (!memory_is_host_coherent(mem)) {
        VkMappedMemoryRange range { .memory = mem, .offset = offset, .size = size };
        align_mapped_range(range, caps.non_coherent_atom_size);  // offset & size aligned
        vkFlushMappedMemoryRanges(device, 1, &range);
    }
    // HOST_COHERENT: memcpy alone is sufficient
}
```

UBO dynamic offsets in `per_frame[fif]` must respect `min_uniform_buffer_offset_alignment`. Prefer **HOST_COHERENT** for staging/UBO rings when available; if not, flush every write batch before submit.

### LOD visuals (non-persistent)

| LOD | Extent | Effective scale | Use |
|-----|--------|-----------------|-----|
| 0 | 32³ | 1 m | Gameplay, save, MP |
| 1 | 64³ | 2 m | Mid distance |
| 2 | 128³ | 4 m | Horizon |

Only **LOD0** mutable / persisted / synced.

### Shader hot-reload (Debug, before M4 materials)

```cpp
void ShaderManager::poll_hot_reload();  // fs mtime → async compile → swap on success
```

Active only `#ifdef ENGINE_DEBUG`. Enables fast iteration on procedural materials.

### Swapchain / resize

See §4 — `OUT_OF_DATE_KHR`, extent 0×0 pause, `recreate_swapchain()` integrated with `Renderer` lifetime.

---

## 14. Chunk streaming (3D RAM budget)

**Primary knob:** `max_loaded_chunks` from **`MemoryBudget::chunk_voxel_ram`** (§0.1) — not a 2D `sqrt(n/π)` alone.

### Vertical vs horizontal (Survival-default)

Minecraft-like play needs **wide XZ**, not all Y layers loaded at once.

```cpp
struct StreamingConfig {
    int max_loaded_chunks;           // hard cap from voxel RAM budget
    int horizontal_load_radius;    // XZ euclidean disk radius (see load predicate)
    int vertical_load_radius_above; // cy+ layers around player chunk
    int vertical_load_radius_below;
    int simulate_radius_xz;
    int render_radius_xz;
};

StreamingConfig from_hardware(
    const CpuHardware& /*cpu*/,
    const MemoryBudget& mem,
    const WorldConfig& world,
    RenderPreset preset)
{
    const size_t chunk_voxel_bytes_est = 31 * 1024 * 8;  // 248 KB
    StreamingConfig c;
    c.max_loaded_chunks = int(mem.chunk_voxel_ram / chunk_voxel_bytes_est);

    const int vertical_layers =
        world.chunk_height_max - world.chunk_height_min + 1;

    // Survival: only load a band of Y around player, not full world height
    c.vertical_load_radius_above = 2;   // TOML override
    c.vertical_load_radius_below = 2;
    const int active_y_layers =
        c.vertical_load_radius_above + c.vertical_load_radius_below + 1;

    const int chunks_per_layer_budget =
        std::max(1, c.max_loaded_chunks / std::max(1, active_y_layers));

    int r = int(std::sqrt(float(chunks_per_layer_budget) / 3.14159f));
    c.horizontal_load_radius = clamp(r, 4, 24);
    c.render_radius_xz       = clamp(r - 2, 3, 20);
    c.simulate_radius_xz     = clamp(r / 2, 2, 10);

    // preset High → +1 horizontal; Low → -1
    return c;
}
```

**Load predicate (euclidean XZ — matches `sqrt(budget/π)` math):**

```cpp
int dx = cx - pcx, dz = cz - pcz;
bool in_horizontal_disk = (dx*dx + dz*dz) <= r*r;  // r = horizontal_load_radius
bool in_vertical_band = (cy >= pcy - below && cy <= pcy + above);
bool in_bounds = cy >= world.chunk_height_min && cy <= world.chunk_height_max;
```

**Rejected:** axis-aligned square `|dx|≤r && |dz|≤r` loads ~`(2r+1)²` chunks — overshoots circular budget from `sqrt(n/π)`.

**Policy:**

- 3D spiral prioritized: player chunk → same-Y ring → vertical neighbors.
- Hysteresis: unload when outside **1.2×** horizontal or vertical radius.
- LRU when `loaded_count > max_loaded_chunks` **or** `chunk_mesh_cpu_ram` exhausted **or** `GpuMeshPool` over `gpu_mesh_vram` (evict GPU first, §0.1).

TOML can override radii and `max_loaded_chunks` independently.

---

## 15. Block interaction & inventory

### Raycast

- 3D grid DDA; `PlayerReach` from config.

### Mutations

```cpp
struct BlockMutation {
    BlockPos pos;
    BlockState old_state;
    BlockState new_state;
    EntityId source;
    uint64_t tick;
};
```

**Apply rule:** commit only if `read_block(pos) == old_state`; otherwise **reject** (no write, no dirty). Protects undo, MP, save-replay, queued actions, and rapid tool input from stale overwrites.

Flow: validate + **old_state match** → palette write → section dirty → border propagate → **cross-chunk light** (§12) → occupancy update (§12) → persist dirty.

### Inventory (Phase 1 complete)

- `ItemId`, `ItemStack`, `Inventory`, hotbar view, ImGui DnD.
- `InventoryDelta` command for future MP (runtime only).
- **Disk:** full `InventorySnapshot` in `player.dat` (§17).

---

## 16. Lighting & sky

### Prototype (M4–M10)

- Ambient + vertex AO (meshing).
- Directional sun/moon from `DayNightCycle`.
- Rayleigh/Mie sky pass.
- **Block light flood** M5b; per-section `blockLight[]`; **cross-chunk propagation** §12.

### GI-ready (stubbed)

- `LightingFrameData`: `sun`, `ambient`, `irradiance_volume`, `gi_debug`.
- Section `RadianceBrick` 8³ buffer reserved.
- Pass graph: `ShadowCSM` → `GI_Inject` → `GI_Apply`.

---

## 17. Persistence & player save

### 3D region files (explicit layout)

**Region coordinate:** `using RegionCoord = glm::ivec3;` — one file per region volume.

**Path:**

```
regions/r.<rx>.<ry>.<rz>.vwr
```

**Region size:** **8×8×8 chunks** per region (512 entries). **Fully 3D** — no XZ-only index, no `cy` in filename side channel.

**ChunkCoord → RegionCoord / local index:**

```cpp
RegionCoord region_of(ChunkCoord c) {
    return { floor_div(c.x, 8), floor_div(c.y, 8), floor_div(c.z, 8) };
}
// local 0..7 per axis
int lx = positive_mod(c.x, 8), ly = positive_mod(c.y, 8), lz = positive_mod(c.z, 8);
int local_index = lx + 8 * lz + 64 * ly;
```

Negative `cx/cy/cz` supported via **floor division** (not C++ `%` alone). File path uses `region_of` components (can be negative): `r.-1.0.2.vwr`.

Index in file:

```
local_index = lx + 8 * lz + 64 * ly   // lx,ly,lz in 0..7 — 512 slots
```

Header:

```
Magic "VWR1", version u16, compression, sector_shift
ChunkIndex[512]   // 8×8×8
  offset u24, compressed_size u20, flags u8
Sector payloads 4KB aligned
```

**Per-chunk blob payload header** (inside sector, before zstd payload):

```
uncompressed_size u32
compressed_size   u32
crc32             u32   // CRC-32 of uncompressed bytes (or xxHash32 — pick one, document in code)
chunk_version     u16
flags             u8    // ChunkBlobFlags
```

**Load decision order:** (1) CRC fail → **corrupt** → quarantine if `CHUNK_MODIFIED_BY_PLAYER`, else regen; (2) `chunk_version != CURRENT` → **version mismatch** path below; (3) decompress size mismatch → corrupt. Distinguishes torn writes / bad compression from intentional version drift.

**Chunk blob flags:**

```cpp
enum ChunkBlobFlags : uint8_t {
    CHUNK_GENERATED           = 1 << 0,
    CHUNK_MODIFIED_BY_PLAYER  = 1 << 1,
};
```

**Versioning / migration (Phase 1):**

- `version` field required in every blob.
- **No auto-migrator** VWR1 → VWR2 in prototype.
- On version mismatch:

```cpp
if (blob.version != CURRENT_CHUNK_VERSION) {
    if (blob.flags & CHUNK_MODIFIED_BY_PLAYER) {
        quarantine_chunk_blob(coord);  // per-chunk, NOT whole region file
        invalidate_region_slot(coord); // leave other 511 chunks in .vwr intact
        SPDLOG_ERROR("chunk version mismatch, quarantined c.{}.{}.{}", cx, cy, cz);
        // do NOT regenerate — player builds must not vanish
    } else {
        SPDLOG_WARN("chunk version mismatch, regenerating {}", coord);
        regenerate_from_seed(coord);
    }
}
```

- Migration tool = post-1.0 scope.

### Save layout (per world folder)

```
<saves>/<world_name>/
  world.meta      # world-global state
  player.dat      # player state (VPL1)
  regions/        # r.<rx>.<ry>.<rz>.vwr (8³ chunks each)
  quarantine/chunks/c.<cx>.<cy>.<cz>.blob   # single chunk, never whole 8³ region
```

### `world.meta` (TOML or minimal binary)

- `seed`, `day_time`, `format_version`, `last_render_origin`
- **Not** inventory / health (see `player.dat`)

### `player.dat` (binary `VPL1`)

```cpp
struct PlayerSaveV1 {
    ChunkCoord chunk;             // authoritative spawn / save location
    glm::vec3  local_offset;      // meters within chunk (0..32 per axis)
    float health;
    float hunger;                 // stub 0 if unused
    uint8_t hotbar_selected;
    InventorySnapshot inventory;  // flat slot array, same layout as ECS Inventory
};
```

- Serialize `Inventory` 1:1 (slots, counts, durability); no separate `InventoryDelta` on disk.
- **Load order:** `world.meta` → `player.dat` → stream regions around saved `chunk`.
- **Save order:** flush dirty `.vwr` sectors → `player.dat` (atomic rename) → `world.meta` (atomic rename).

### M5 MinimalSave vs M8 ProductionSave

| | **M5 MinimalSave** (vertical-slice gate) | **M8 ProductionSave** |
|---|------------------------------------------|------------------------|
| Purpose | Prove persist loop early | Full production pipeline |
| Chunks | Simple serialized blobs or single-region stub | 3D `.vwr` regions, zstd, sector index |
| Player | `player.dat` VPL1 | Same + validated roundtrip |
| Quarantine | Optional stub | Per-chunk `quarantine/chunks/...` |
| Atomic rename | Best-effort | Required |

M5 must reload **at least one player-placed block** — not deferred to M8.

### Async IO

- `io_threads` from `ThreadConfig`; atomic rename on Windows.

---

## 18. Fluids (Phase 1 stub)

**Decision:** Water exists as blocks from worldgen (`sea_level`); **no flow simulation** in prototype.

### Block definition (`BlockId::Water`)

| Property | Value |
|----------|-------|
| `opaque` | false |
| `solid` | false |
| `light_emission` | 0 |
| Collision (Jolt) | **none** — excluded from static collision mesh |
| Gameplay | not placeable in survival until bucket item exists (optional M5: place as block) |

### Rendering

- Same greedy mesh as terrain; faces tagged `is_water`.
- **No internal faces** between adjacent water blocks (same rule as opaque culling) — otherwise alpha sort becomes expensive and visually noisy.
- **Pass order (efficient — no full-screen sky overdraw):**

```
1. OpaqueTerrain     // clear depth+color; depth write ON
2. Sky               // fullscreen triangle writes **depth = 1.0** (far); depth test EQUAL, depth write OFF
3. WaterTransparent  // depth test ON, depth write OFF, alpha blend
4. UI / Debug
```

Depth clear sets `far_depth`. Sky fills only pixels still at far depth (no opaque hit). Water then tests against terrain + sky depth correctly.

**Rejected:** Sky-first full-screen draw then opaque overdraw (wastes fill rate).

- Water pass listed in `WorldRenderSnapshot::water_sections` (§5).
- Fluid level from `BlockState` prop nibble (§10); Phase 1 all levels render as full block.
- **Sort:** `water_sections` sorted **back-to-front** by distance to camera each frame (rough center of section AABB). Phase 1 accepts minor overlap artifacts if sort approximate; intersecting transparent surfaces between sections are **accepted** in Phase 1 (no per-triangle sort).

### Lighting

- Water does not cast AO; receives sky light; block light attenuated (simple mul in shader).

### Phase 2+ (out of scope)

- Flow simulation — prop nibble stores level (§10) but static in Phase 1.

---

## 19. Audio

**Backend:** miniaudio — Master / Music / SFX / UI buses.

**Occlusion:** 1 m `AudioSolidGrid`; listener→source DDA; lowpass + gain.

**Grid update radius (CPU-scaled):**

```cpp
int occlusion_grid_radius_chunks(const CpuHardware& hw) {
    return hw.physical_cores >= 6 ? 48 : 32;  // in meters = same as blocks
}
```

Update cost ~O(r²) per dirty column — throttle with chunk dirty events.

---

## 20. Physics & destruction

**Jolt** used for **dynamic/rigid** simulation (debris M11+). **Player locomotion vs terrain does not use CharacterVirtual against terrain meshes.**

### Player (M6)

- **`VoxelCapsuleResolver`** (§12): `occupancy_at()` + `SolidIfChunkMissing` at streaming edge — authoritative, same-tick.
- Optional Jolt **sensor** body following capsule for triggers only — no terrain mesh collision for player.

### Static terrain in Jolt (debris support)

- Simplified greedy **`MeshShape`** per chunk/section via **`CollisionRemeshQueue`** — async.
- Used when **debris bodies** collide with world geometry, not for player walking.

### Debris (M11+)

- Dynamic bodies on debris layer; collide with static terrain meshes + capsule resolver does not apply to debris internals.

### Object layer matrix (explicit)

|  | Static | Player | Debris | Sensor |
|--|--------|--------|--------|--------|
| **Static** | — | ✓ | ✓ | ✗ |
| **Player** | ✓ | ✗ | ✓ | ✓ |
| **Debris** | ✓ | ✓ | **✗** | ✗ |
| **Sensor** | ✗ | ✓ | ✗ | ✗ |

**Debris↔Debris disabled** to avoid O(n²) explosions; re-fracture uses explicit pair tests when needed.

**Destruction:** M11+ (post playable prototype). Config: `max_active_debris`, `max_fracture_depth` from `EngineConfig` (auto defaults from RAM).

---

## 21. Procedural visuals

- `MaterialParams` SSBO; shader 3D noise / triplanar.
- **ShaderManager** hot-reload in Debug (§13).
- `MaterialId` stable across net + GI.

---

## 22. World generation (phased)

### M2b — minimal terrain (before full FastNoise2)

**Goal:** real chunks for meshing/streaming/save tests, not void cubes.

- Deterministic **heightmap** from `WorldGenConfig::seed` (can use simple hash noise or early FastNoise2 1D node).
- Columns: stone below, dirt/grass above `sea_level`, **water** at/below sea (§18).
- No biomes/caves/structures yet.
- Jobs on **`worker_threads` pool** (§2) — **not** `meshing_threads` (memory-bound, capped at 4).

### M3+

Mesh and stream **M2b-generated** terrain; validate greedy + snapshot on real data.

### M10 — full generator (FastNoise2)

Deterministic from `WorldGenConfig::seed` via FastNoise2 SIMD ([FastNoise2](https://github.com/Auburn/FastNoise2)).

| Parameter | Role |
|-----------|------|
| `seed` | All noise nodes |
| `sea_level` | Water table |
| `biome_scale` | 2D biome |
| `structure_density` | Trees, rocks |
| `finite_bounds` | Optional |

Pipeline: 3D density → 2D Whittaker biomes → surface blocks → structure stamps → 3D caves. Writes **LOD0** only; respects `chunk_height_min/max`. Scheduled on **`worker_threads`** (enkiTS), same as M2b — never on `meshing_threads`.

### Structures — tiered Phase 1 rules

**No cross-chunk structures in M10** (no neighbor lookahead). Three tiers:

| Tier | Examples | Placement rule |
|------|----------|----------------|
| **Micro vegetation** | grass tufts, flowers, small shrubs | **Allowed near chunk edges** (no margin requirement) |
| **Medium** | small trees, boulders | Must fit in **32³** with **4-block margin** from chunk AABB |
| **Large POI** | ruins, tall trees | **Deferred** — or reservation map post-prototype |

- Medium trees: `structure_fits(chunk, template)` before stamp; trunk+canopy inside margin box.
- Edge chunks look natural via micro decor; large silhouettes only when chunk has space.

**Later:** deterministic **structure reservation map** (seed-derived grid) for cross-chunk POIs without refactor of `ChunkCoord`.

---

## 23. Networking preparation (post-1.0)

- `GameCommand` + `tick` + `sequence`.
- `IWorldMutationSink::apply`.
- `INetTransport` loopback stub.
- `BlockId` / `BlockState` versioned per protocol.
- Chunk sync: **section palette delta**.

---

## 24. CMake / deps

```cmake
# cmake/Deps.cmake (FetchContent)
# tomlplusplus v3.4.0, enkiTS, flecs, jolt, glfw, glm, imgui,
# volk, vma, zstd, miniaudio, FastNoise2 v0.10.0-alpha
# Catch2 v3 (tests), Tracy (optional)
# enet — fetched, not linked until MP
```

```
Engine/
  CMakeLists.txt
  cmake/Deps.cmake
  assets/default.toml
  engine/ ...
  game/ ...
  shaders/ ...
```

**M0 deliverables tied to architecture (do first):**

| Priority | Item |
|----------|------|
| 🔴 | Init order §3 + CrashHandler + Logging §8 |
| 🔴 | `ChunkCoord` = `glm::ivec3`, `WorldConfig` height bounds |
| 🔴 | `run_cpu` + `finalize_cpu` / `finalize_gpu` split §0, §3 |
| 🔴 | `GpuDeferredFreeQueue` design §5 (before M3 mesh upload) |
| 🔴 | `VK_CHECK` + Vulkan init |
| 🔴 | Frame loop skeleton §4 (accumulator, present mode) |
| 🔴 | §10.5 vertex/index binding before any mesh code |
| 🔴 | `MemoryBudget` §0.1 + `BlockState` §10 |
| 🔴 | `frames_in_flight`, swapchain recreate §4, snapshot fences §5 |
| 🟡 | Flecs events/observers §6–§7 skeleton |
| 🟡 | `BLOCK_REGISTRY` + `Water` stub §18 |
| 🟡 | `SectionBorderCache` in chunk struct |
| 🟡 | Catch2 + origin-rebase unit test |
| 🟢 | toml++ `EngineConfig` |
| 🟢 | Tracy optional link |
| 🟢 | `ShaderManager` hot-reload hook (Debug) |

---

## 25. Roadmap

### Vertical slice rule (“boring playable” by M5)

**Hard gate before M6:** player can complete this loop without debug cheats:

1. Start world (M2b terrain, not flat void)
2. Walk/fly (M1)
3. Chunks load/stream around player (M2)
4. See meshed terrain (M3)
5. Target block via raycast, **break** and **place** blocks (M5)
6. **Save** world, quit, **reload** — **MinimalSave** (§17): at least one modified chunk + `player.dat`

Inventory polish (M7) and full audio (M9) are **not** required for this gate — but break/place/save/load are.

### Milestones

| ID | Milestone | Deliverable |
|----|-----------|-------------|
| M0 | Bootstrap | §3 init, crash dump, logging, **Catch2**, **FrameTimer**, accumulator §4, **triple snapshot** §5, `MemoryBudget` §0.1 |
| M1 | Camera | Input, fly cam, **`WorldPosition`**, `EvtOriginShift` |
| M2 | Chunks | §10.5 indexing, `BorderCell`, dirty bits, euclidean stream §14, `coord_to_entity` §7 |
| **M2b** | **Minimal worldgen** | Heightmap + sea + grass/stone/water columns §22 |
| M3 | **Thin render** | One material, **one chunk**, no water, no hot-reload, no LOD, no streaming — then add streaming in M3b/M4 path §27 |
| M4 | Look | Procedural materials, **Sky→Opaque→Water** §18, shader hot-reload, multi-chunk stream |
| **M5** | **Vertical slice** | Break/place, **`MinimalSave`** reload, occupancy §12 |
| M5b | Light | Block light flood + mesh |
| M6 | Player | **`VoxelCapsuleResolver`** + occupancy; Jolt layer matrix for debris prep |
| M7 | Inventory | Full UI |
| M8 | **ProductionSave** | Full 3D `.vwr`, zstd, per-chunk **quarantine**, atomic rename §17 |
| M9 | Audio | miniaudio + 1m grid |
| M10 | Worldgen | Full FastNoise2 biomes/caves/structures §22 |
| **Done** | **Playable prototype** | SP survival (inventory, audio, full save) |
| M11 | Destruction | Fracture + debris |
| M12 | Shadows | CSM |
| M13+ | Voxel GI | Cone / DDGI / cascades |

---

## 26. Risk register

| Risk | Mitigation |
|------|------------|
| Origin rebase bugs | Catch2 §8; rebase without remesh; Jolt offset test |
| Origin rebase perf cliff | **Never** mass `ChunkDirty` on `EvtOriginShift` §6 |
| Snapshot GPU race | `SnapshotCount = fif + 1` + per-slot fences §5 |
| GPU buffer UAF | `GpuDeferredFreeQueue` §5 — not snapshot fences alone |
| GPU config @ step 4 | `finalize_gpu` only after Renderer §3 |
| Swapchain resize | `OUT_OF_DATE` / 0×0 §4; idle + flush deferred free |
| VRAM mesh OOM | `GpuMeshPool` LRU §0.1; budget **VRAM-only** |
| Upload/draw hazard | Copy + barrier same CB §13 |
| Chunk light seam | Cross-chunk flood §12 |
| Break collision | `occupancy_at` + `VoxelCapsuleResolver` §12/§20 |
| Streaming edge fall-through | `SolidIfChunkMissing` policy §12 |
| Spawn solid bubble | `PlayerSpawnReadyGate` §12 |
| Stale block overwrite | `old_state` match on mutation §15 |
| CharacterVirtual vs grid | **Rejected** — documented §12 |
| Indirect buffer UAF | `per_frame[fif]` ring §5/§13 |
| Staging UAF | `StagingRing[fif]` §13 |
| Cull space mismatch | `cull_*` render-relative §5 |
| Sections stacked draw | `model_translation` includes section offset §10.5 |
| Corner coord overflow | 5-bit pack 0..16 §10.5 |
| Stream overshoot | Euclidean disk §14 |
| GPU UAF on evict | Evict guards + `FrameGarbage` §5 |
| Chunk UAF on unload | Fixed unload order §7; generation counter |
| Render/sim race | Snapshot **triple-buffer** §5; immutable during record |
| 144 Hz sim drift | Accumulator cap §4 |
| Meshing spikes | Render + collision budgets §12 |
| Collision stale after break | `collision_max_stale_ticks` §12 |
| OOM on 32 GB RAM | Split **MemoryBudget** §0.1; mesh/VRAM caps |
| 3D streaming blow-up | Vertical band + per-layer budget §14 |
| Save stalls | Dedicated IO pool; zstd off-thread |
| Player build lost on version bump | **Quarantine** if `CHUNK_MODIFIED_BY_PLAYER` §17 |
| VWR version drift (generated only) | Regen from seed |
| Negative block coords | Catch2 matrix §8 |

---

## 27. Implementation order (M0–M3 critical path)

**M3 coding rule — build one thin renderer first:**

1. Single procedural material, **no water pass**, **no shader hot-reload**, **no LOD**, **one fixed chunk** in view.
2. Upload + draw + `GpuDeferredFreeQueue` + `per_frame[fif]` rings working end-to-end.
3. **Then** hook euclidean streaming (M2 already has CPU chunks) and multi-chunk snapshots.
4. Water / sky polish → **M4** (not bundled into first Vulkan milestone).

Do **not** implement the full render stack (water BTF, hot-reload, LOD, full stream) in the first M3 PR.

| Order | Item | Milestone |
|-------|------|-----------|
| 1 | §10.5 vertex format (5-bit, index order, section offset) | before first mesh line |
| 2 | CPU/GPU probe split + `finalize_gpu`; ChunkStore **after 9b** §3 | M0 |
| 3 | `GpuDeferredFreeQueue` (fence per `last_used_frame`) + `per_frame[fif]` + host flush §13 | M0 design, M3 code |
| 3b | `VoxelCapsuleResolver` + `VoxelMovementConfig` step-up | before M6 |
| 4 | `WorldPosition` | M1 |
| 5 | `BorderCell` + `occupancy_at` + dirty bits + palette cap §11 | M2 |
| 6 | Euclidean streaming + `coord_to_entity` | M2 |
| 7 | **Thin** renderer: 1 chunk → draw (see rule above) | M3 |
| 8 | UI before `record_and_submit` | M3 |
| 9 | Upload barrier same CB | M3 |
| 10 | Streaming + multi-chunk snapshot | after M3 thin path |
| 11 | Per-chunk quarantine + CRC §17 + MinimalSave/ProductionSave | M5 / M8 |
| 12 | `PlayerSpawnReadyGate` §12 | M6 / GameSession |
| 13 | Water BTF sort §18 (internal face cull) | M4 |

---

## 28. References

- Vulkan 1.2: https://registry.khronos.org/vulkan/
- Jolt Physics: https://github.com/jrouwe/JoltPhysics
- Flecs: https://www.flecs.dev/flecs/
- FastNoise2: https://github.com/Auburn/FastNoise2
- enkiTS: https://github.com/dougbinks/enkiTS
- toml++: https://github.com/marzer/tomlplusplus
- miniaudio: https://miniaud.io/docs/manual/
- ENet: http://sauerbraten.org/enet/
- Minecraft Anvil (reference): https://minecraft.wiki/w/Anvil_file_format
- Catch2: https://github.com/catchorg/Catch2
- Tracy: https://github.com/wolfpld/tracy
- Flecs observers: https://www.flecs.dev/flecs/md_docs_ObserversManual.html
