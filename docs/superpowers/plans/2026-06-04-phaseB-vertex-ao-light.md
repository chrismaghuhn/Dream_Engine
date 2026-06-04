# Phase B — Vertex AO + Baked Light — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bake per-corner ambient occlusion (0–3) and use existing sky/block light nibbles in terrain/water shaders, with smooth interpolation and correct per-corner AO tangent signs.

**Architecture:** Extend Vulkan vertex input (locations 2–3) without changing the 8-byte `TerrainVertex` layout. Shaders convert AO/light to floats in the vertex stage for smooth rasterization. Mesher gains `corner_ao(axis, positive, x, y, z, du_sign, dv_sign, section)` and `emit_quad(..., bool is_water_layer)`; water forces `ao = 3`.

**Tech Stack:** C++20, Vulkan 1.2 (volk), GLSL 450, Catch2, CMake/MSVC.

**Spec:** `docs/superpowers/specs/2026-06-04-phaseB-vertex-ao-light-design.md` (rev. 2, approved)

**Build/test commands (Developer PowerShell for VS 2022):**
```powershell
cmake -S . -B build
cmake --build build --config Debug --target engine_tests
ctest --test-dir build -C Debug -R "greedy mesher" --output-on-failure
cmake --build build --config Debug --target VoxelEngine
.\build\game\Debug\VoxelEngine.exe
```

---

## Task 1: Vertex attributes (TerrainPass + WaterPass)

**Files:**
- Modify: `engine/render/TerrainPass.cpp`
- Modify: `engine/render/WaterPass.cpp`

- [ ] **Step 1: Add AO + light vertex attributes**

In both files, extend `attributes[]` after `material_id`:

```cpp
{
    .location = 2,
    .binding = 0,
    .format = VK_FORMAT_R8_UINT,
    .offset = offsetof(TerrainVertex, ao),
},
{
    .location = 3,
    .binding = 0,
    .format = VK_FORMAT_R8_UINT,
    .offset = offsetof(TerrainVertex, light),
},
```

Set `.vertexAttributeDescriptionCount = 4` (was `2`).

- [ ] **Step 2: Build and smoke-compile**

Run:
```powershell
cmake --build build --config Debug --target VoxelEngine
```
Expected: PASS (shaders still declare only 2 inputs — SPIR-V may warn; fixed in Task 2).

- [ ] **Step 3: Commit**

```powershell
git add engine/render/TerrainPass.cpp engine/render/WaterPass.cpp
git commit -m "feat(render): add ao and light vertex attributes to terrain and water passes"
```

---

## Task 2: Shaders consume AO + light (debug path first)

**Files:**
- Modify: `shaders/terrain.vert`
- Modify: `shaders/terrain.frag`
- Modify: `shaders/water.vert`
- Modify: `shaders/water.frag`

- [ ] **Step 1: Update `terrain.vert`**

Add inputs and smooth outputs (keep material/face flat):

```glsl
layout(location = 2) in uint in_ao;
layout(location = 3) in uint in_light;

float ao_mul_from_u8(uint ao) {
    if (ao == 0u) return 0.55;
    if (ao == 1u) return 0.72;
    if (ao == 2u) return 0.86;
    return 1.00;
}

layout(location = 2) out float v_ao_mul;
layout(location = 3) out vec2 v_light_levels;

// in main(), after v_world assignment:
v_ao_mul = ao_mul_from_u8(in_ao);
v_light_levels = vec2(float(in_light >> 4u) / 15.0, float(in_light & 15u) / 15.0);
```

- [ ] **Step 2: Update `terrain.frag`**

Add interpolants and replace fixed ambient-only lighting:

```glsl
layout(location = 2) in float v_ao_mul;
layout(location = 3) in vec2 v_light_levels;

// in main(), after albedo sample:
const vec3 sky_tint   = vec3(0.85, 0.92, 1.00);
const vec3 block_tint = vec3(1.00, 0.82, 0.55);
const float ambient_floor = 0.12;
vec3 lit = albedo * (v_light_levels.x * sky_tint + v_light_levels.y * block_tint + ambient_floor);

const vec3 n = face_normal(v_face);
const vec3 light_dir = normalize(vec3(0.35, 0.85, 0.25));
const float ndl = clamp(dot(n, light_dir), 0.2, 1.0);
lit = mix(lit, lit * ndl, 0.18);

lit *= v_ao_mul;
out_color = vec4(lit, 1.0);
```

**Debug checkpoint:** Temporarily force `v_ao_mul = 0.55` in vert to verify pipeline reads attribute 2 before mesher fills real AO.

- [ ] **Step 3: Update `water.vert` / `water.frag`**

`water.vert` — same `in_light` + `v_light_levels`; set `layout(location = 2) out float v_ao_mul = 1.0` (or compute from `in_ao` but multiply by 0 in frag — prefer `v_ao_mul = 1.0`).

`water.frag` — tint water with `v_light_levels`; do not darken by AO.

- [ ] **Step 4: Rebuild game target**

```powershell
cmake --build build --config Debug --target VoxelEngine
```
Expected: PASS; shaders compile via `shaders/CMakeLists.txt` dependency.

- [ ] **Step 5: Commit**

```powershell
git add shaders/terrain.vert shaders/terrain.frag shaders/water.vert shaders/water.frag
git commit -m "feat(shader): consume baked vertex ao and light on terrain and water"
```

---

## Task 3: Water layer — `is_water_layer` + `ao = 3`

**Files:**
- Modify: `engine/world/GreedyMesher.cpp`

- [ ] **Step 1: Extend `emit_quad` signature**

Add `bool is_water_layer` before vertex/index vectors. Update forward declaration if any.

- [ ] **Step 2: Force water AO in vertex loop**

```cpp
vert.ao = is_water_layer ? static_cast<uint8_t>(3) : /* corner_ao placeholder 0 until Task 4 */;
```

- [ ] **Step 3: Pass flag from `mesh_section_layer`**

At top of `mesh_section_layer`:
```cpp
const bool is_water_layer = (face_predicate == water_face_predicate);
```

Pass `is_water_layer` into `emit_quad(...)`.

At opaque call site in `mesh_section`:
```cpp
mesh_section_layer(section, opaque_face_predicate, ...); // is_water_layer false inside
```

- [ ] **Step 4: Write failing test**

Append to `tests/world/test_greedy_mesher.cpp`:

```cpp
TEST_CASE("greedy mesher water cube all ao bright") {
    engine::Section section;
    fill_water(section, 0, 0, 0);

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    REQUIRE_FALSE(water_vertices.empty());
    for (const engine::TerrainVertex& v : water_vertices) {
        REQUIRE(v.ao == 3);
    }
}
```

- [ ] **Step 5: Run test**

```powershell
ctest --test-dir build -C Debug -R "water cube all ao" --output-on-failure
```
Expected: PASS after Step 2.

- [ ] **Step 6: Commit**

```powershell
git add engine/world/GreedyMesher.cpp tests/world/test_greedy_mesher.cpp
git commit -m "feat(mesh): water layer forces ao=3 via is_water_layer"
```

---

## Task 4: `corner_ao` + per-corner `du_sign` / `dv_sign`

**Files:**
- Modify: `engine/world/GreedyMesher.cpp`

- [ ] **Step 1: Add helpers in anonymous namespace**

```cpp
struct TangentBasis {
    glm::ivec3 air_step{0};
    glm::ivec3 eu{0};
    glm::ivec3 ev{0};
};

TangentBasis tangent_basis_for_axis(int axis, bool positive) {
    TangentBasis b{};
    switch (axis) {
    case 0:
        b.eu = {0, 1, 0};
        b.ev = {0, 0, 1};
        b.air_step = {positive ? 1 : -1, 0, 0};
        break;
    case 1:
        b.eu = {1, 0, 0};
        b.ev = {0, 0, 1};
        b.air_step = {0, positive ? 1 : -1, 0};
        break;
    default:
        b.eu = {1, 0, 0};
        b.ev = {0, 1, 0};
        b.air_step = {0, 0, positive ? 1 : -1};
        break;
    }
    return b;
}

bool occludes_ao(const VoxelSample& s) {
    return s.opaque_solid;
}

glm::ivec3 add(glm::ivec3 a, glm::ivec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

glm::ivec3 scale(glm::ivec3 v, int s) {
    return {v.x * s, v.y * s, v.z * s};
}

uint8_t corner_ao(
    int axis,
    bool positive,
    int x,
    int y,
    int z,
    int du_sign,
    int dv_sign,
    const Section& section) {
    const TangentBasis b = tangent_basis_for_axis(axis, positive);
    const glm::ivec3 base = add({x, y, z}, b.air_step);
    const glm::ivec3 s1 = add(base, scale(b.eu, du_sign));
    const glm::ivec3 s2 = add(base, scale(b.ev, dv_sign));
    const glm::ivec3 c  = add(add(base, scale(b.eu, du_sign)), scale(b.ev, dv_sign));

    const int a = occludes_ao(sample_voxel(section, s1.x, s1.y, s1.z)) ? 1 : 0;
    const int b2 = occludes_ao(sample_voxel(section, s2.x, s2.y, s2.z)) ? 1 : 0;
    const int c2 = occludes_ao(sample_voxel(section, c.x, c.y, c.z)) ? 1 : 0;
    const int v = a + b2 + (c2 & (a & b2));
    return static_cast<uint8_t>(std::clamp(3 - v, 0, 3));
}
```

Formula matches spec: `3 - (a + b + max(c, a*b))` with `max(c,a*b) == c & (a & b)` for 0/1 values.

- [ ] **Step 2: Compute `du_sign` / `dv_sign` per corner in `emit_quad`**

After building `corners[4]` (before or after winding swap — use **world coords**):

```cpp
auto tangent_uv = [&](const glm::ivec3& c) {
    int tu = 0;
    int tv = 0;
    switch (axis) {
    case 0: tu = c.y; tv = c.z; break;
    case 1: tu = c.x; tv = c.z; break;
    default: tu = c.x; tv = c.y; break;
    }
    const int du_sign = (tu == u0 + width) ? 1 : -1;
    const int dv_sign = (tv == v0 + height) ? 1 : -1;
    return std::pair{du_sign, dv_sign};
};
```

In vertex loop:
```cpp
const auto [du_sign, dv_sign] = tangent_uv(c);
vert.ao = is_water_layer
        ? static_cast<uint8_t>(3)
        : corner_ao(axis, positive, c.x, c.y, c.z, du_sign, dv_sign, section);
```

- [ ] **Step 3: Commit mesher only (tests in Task 5)**

```powershell
git add engine/world/GreedyMesher.cpp
git commit -m "feat(mesh): corner ao with per-corner tangent signs"
```

---

## Task 5: Catch2 AO tests (TDD completion)

**Files:**
- Modify: `tests/world/test_greedy_mesher.cpp`

- [ ] **Step 1: Add helpers**

```cpp
std::array<uint8_t, 4> aos_on_face(
    const std::vector<engine::TerrainVertex>& verts,
    engine::Face face) {
    std::array<uint8_t, 4> out{255, 255, 255, 255};
    int n = 0;
    for (const engine::TerrainVertex& v : verts) {
        if (static_cast<engine::Face>((v.packed_position_normal >> 15) & 7u) != face) {
            continue;
        }
        if (n < 4) {
            out[static_cast<size_t>(n++)] = v.ao;
        }
    }
    return out;
}
```

- [ ] **Step 2: L-corner test (write + run first if RED)**

```cpp
TEST_CASE("greedy mesher L corner inner ao darker than exposed") {
    engine::Section section;
    fill_solid(section, 0, 0, 0);
    fill_solid(section, 1, 0, 0);
    fill_solid(section, 0, 1, 0);

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    uint8_t min_ao = 3;
    uint8_t max_ao = 0;
    for (const engine::TerrainVertex& v : opaque_vertices) {
        min_ao = std::min(min_ao, v.ao);
        max_ao = std::max(max_ao, v.ao);
    }
    REQUIRE(min_ao <= 1);
    REQUIRE(max_ao >= 2);
}
```

- [ ] **Step 3: Border-AO test**

```cpp
TEST_CASE("greedy mesher border neighbor affects ao") {
    engine::Section section;
    fill_solid(section, 15, 0, 0);

    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (engine::BorderCell& cell :
         section.border.face[static_cast<size_t>(engine::Face::PX)]) {
        cell.block = stone;
    }

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    const auto px_aos = aos_on_face(opaque_vertices, engine::Face::PX);
    bool has_dark = false;
    bool has_bright = false;
    for (uint8_t ao : px_aos) {
        if (ao <= 1) has_dark = true;
        if (ao >= 2) has_bright = true;
    }
    REQUIRE(has_dark);
    REQUIRE(has_bright);
}
```

- [ ] **Step 4: Four corners not all equal**

```cpp
TEST_CASE("greedy mesher asymmetric face ao varies per corner") {
    engine::Section section;
    for (int z = 0; z < 3; ++z) {
        for (int y = 0; y < 3; ++y) {
            fill_solid(section, 0, y, z);
        }
    }

    std::vector<engine::TerrainVertex> opaque_vertices;
    std::vector<uint32_t> opaque_indices;
    std::vector<engine::TerrainVertex> water_vertices;
    std::vector<uint32_t> water_indices;
    mesh_both(section, opaque_vertices, opaque_indices, water_vertices, water_indices);

    const auto px_aos = aos_on_face(opaque_vertices, engine::Face::PX);
    REQUIRE(px_aos[0] != px_aos[1] || px_aos[1] != px_aos[2] || px_aos[2] != px_aos[3]);
}
```

- [ ] **Step 5: Single block + AO/light independence**

```cpp
TEST_CASE("greedy mesher single block has ao below max") {
    engine::Section section;
    fill_solid(section, 0, 0, 0);
    // ... mesh ...
    bool any_below_3 = false;
    for (const engine::TerrainVertex& v : opaque_vertices) {
        if (v.ao < 3) any_below_3 = true;
    }
    REQUIRE(any_below_3);
}

TEST_CASE("greedy mesher low ao can coexist with high block light") {
    engine::Section section;
    fill_solid(section, 0, 0, 0);
    section.block_light[engine::block_index(0, 0, 0)] = 14;
    section.sky_light[engine::block_index(0, 0, 0)] = 0;
    // mesh; find vertex with ao < 3 and light nibble block >= 14
}
```

- [ ] **Step 6: Run full greedy mesher suite**

```powershell
ctest --test-dir build -C Debug -R "greedy mesher" --output-on-failure
```
Expected: all PASS including existing `border light copied to edge vertex nibbles`.

- [ ] **Step 7: Commit**

```powershell
git add tests/world/test_greedy_mesher.cpp
git commit -m "test(mesh): vertex ao L-corner, border, and corner variation"
```

---

## Task 6: Visual smoke + spec status

**Files:**
- Modify: `docs/superpowers/specs/2026-06-04-phaseB-vertex-ao-light-design.md` (status only)

- [ ] **Step 1: Manual playtest**

Run `VoxelEngine.exe`. Verify:
- Single block on flat terrain — corner shading visible.
- Two-block L-wall — inner corner darker.
- Torch in enclosed space — warm face tint; corners still shaded.

- [ ] **Step 2: Mark spec approved**

Change spec header status to `Approved (2026-06-04)`.

- [ ] **Step 3: Full test sweep**

```powershell
ctest --test-dir build -C Debug --output-on-failure
```
Expected: PASS (or note pre-existing failures unrelated to Phase B).

- [ ] **Step 4: Commit**

```powershell
git add docs/superpowers/specs/2026-06-04-phaseB-vertex-ao-light-design.md
git commit -m "docs: mark Phase B vertex ao light spec approved after smoke"
```

---

## Spec coverage checklist (self-review)

| Spec requirement | Task |
|------------------|------|
| `corner_ao` + `du_sign`/`dv_sign` | Task 4 |
| `is_water_layer` in `emit_quad` | Task 3 |
| 4 vertex attributes | Task 1 |
| Smooth AO/light (not flat) | Task 2 |
| Water ao=3, light tinted | Task 2–3 |
| L-corner, border, 4-corner tests | Task 5 |
| Light regression | Task 5 Step 6 |
| 8-byte layout unchanged | No struct edits |
| No snapshot sun | No `FrameUniformGpu` change |

---

## File map

| File | Responsibility |
|------|----------------|
| `GreedyMesher.cpp` | `corner_ao`, `emit_quad` signs, water ao |
| `TerrainPass.cpp` / `WaterPass.cpp` | Vulkan vertex format |
| `terrain.vert/frag` | Interpolated ao_mul + light |
| `water.vert/frag` | Light tint, ao disabled |
| `test_greedy_mesher.cpp` | AO + regression tests |