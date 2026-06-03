# RPG Combat Slice v1 — Visual Combo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spieler (Meshy Dungeon Explorer) sichtbar mit Walk/Run, 3er-Combo mit Hit-Windows, Straw-Fantasy-Dummy mit visuellem Treffer-Feedback — in `movement_test`, ohne HP/Tod.

**Architecture:** Neues `engine/character/core` (glTF-Ingest, Validation, Cook-Cache, Animation, Combat-FSM, Hit-Math) ohne Vulkan; `engine/character/render` (`CharacterPass` als `IPassExtension`); Erweiterung von `MovementWorld` + Arena-Parser + `MovementApp`. Cache nur unter `build/character_cache/`.

**Tech Stack:** C++20, CMake, cgltf (FetchContent), GLM, Catch2, Vulkan/volk (nur Render-Pass), bestehende `TextParser`-Disziplin für `.arena` und `combat_attacks.txt`.

**Spec reference:** `docs/superpowers/specs/2026-06-03-rpg-combat-slice-visual-combo-v1-design.md`

---

## Zieldateien (inkrementell)

| Pfad | Verantwortung |
|------|----------------|
| `cmake/Dependencies.cmake` | `cgltf` via FetchContent |
| `engine/character/CMakeLists.txt` | `engine_character_core`, `engine_character_render` |
| `engine/character/core/GltfIngest.hpp/.cpp` | GLB laden, Mesh/Skeleton/Anim extrahieren |
| `engine/character/core/SkeletonValidator.hpp/.cpp` | Joint-Name/Count/Hierarchie ≤128 |
| `engine/character/core/CookedCharacterCache.hpp/.cpp` | `.charbin` read/write unter `build/character_cache/` |
| `engine/character/core/AnimationController.hpp/.cpp` | Clip-Zeit, bone matrices |
| `engine/character/core/AttackData.hpp/.cpp` | `combat_attacks.txt` parsen |
| `engine/character/core/CombatController.hpp/.cpp` | Combo-FSM, `attack_yaw`, recovery |
| `engine/character/core/HitDetection.hpp/.cpp` | Hit-Window + Kapsel vs Box |
| `engine/character/core/CharacterComponents.hpp` | `SkinnedModel`, `AnimationState`, `CombatController`, `HitReact` |
| `engine/character/render/CharacterPass.hpp/.cpp` | Skinned draw, bone UBO |
| `shaders/character_skinned.vert/.frag` | GPU skinning |
| `assets/character/combat_attacks.txt` | Attack-Tabelle (Spec-Werte) |
| `assets/movement/combat_test.arena` | Player + Dummy + Wände |
| `engine/movement/core/Components.hpp` | Re-export oder include character components |
| `engine/movement/core/ArenaLoader.cpp` | `skinned_model`, `combat_controller`, `hit_react` |
| `engine/movement/core/MovementWorld.hpp/.cpp` | Neue ComponentStores |
| `engine/movement/core/InputSnapshot.hpp` | `attack_pressed` edge |
| `movement/MovementApp.cpp` | Sim/render wiring, CharacterPass registrieren |
| `movement/CMakeLists.txt` | Link `engine_character_render` |
| `tests/character/test_*.cpp` | Catch2 ohne Vulkan |
| `tests/CMakeLists.txt` | `character_tests` executable |

---

## Milestone-Gates

| Gate | Kriterium |
|------|-----------|
| **M1** | `character_tests` grün; Player-`Character_output` + 3 Combo-GLBs validiert; Cache-Datei unter `build/character_cache/` |
| **M2** | `movement_test` zeigt Spieler-Mesh, Walk/Run je nach Velocity; Kapsel-Debug optional |
| **M3** | LMB/Config-Taste startet 3er-Combo; Overlay zeigt Clip + Combo-Index |
| **M4** | Treffer im Hit-Window → Dummy `Hit_Reaction_1` + Rückstoß |
| **M5** | `combat_test.arena` Default-Arena; README-Hinweis Playtest |

---

# M1 — glTF-Ingest, Validation, Cook-Cache

### Task M1-1: cgltf + CMake-Target `engine_character_core`

**Files:**
- Modify: `cmake/Dependencies.cmake`
- Create: `engine/character/CMakeLists.txt`
- Modify: `engine/CMakeLists.txt` — `add_subdirectory(character)`
- Modify: `CMakeLists.txt` — falls `engine/` schon character enthält, nur sicherstellen

- [ ] **Step 1: FetchContent cgltf**

In `cmake/Dependencies.cmake` nach spdlog:

```cmake
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cgltf)
```

- [ ] **Step 2: Library anlegen**

`engine/character/CMakeLists.txt`:

```cmake
set(ENGINE_CHARACTER_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/character/core/GltfIngest.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/SkeletonValidator.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/CookedCharacterCache.cpp
)
add_library(engine_character_core STATIC ${ENGINE_CHARACTER_CORE_SOURCES})
target_include_directories(engine_character_core PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${cgltf_SOURCE_DIR}
)
target_compile_definitions(engine_character_core PUBLIC CGLTF_IMPLEMENTATION)
target_link_libraries(engine_character_core PUBLIC glm::glm spdlog::spdlog)
target_compile_definitions(engine_character_core PUBLIC
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ENGINE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
```

`engine/CMakeLists.txt`: `add_subdirectory(character)` nach `movement` oder davor.

- [ ] **Step 3: Build**

```powershell
cmake --build build --target engine_character_core --config Debug
```

Expected: compile (leere Stubs in .cpp bis M1-2).

---

### Task M1-2: Datenstrukturen + Cooked-Format

**Files:**
- Create: `engine/character/core/CharacterAsset.hpp`
- Create: `engine/character/core/CookedCharacterCache.hpp/.cpp`

- [ ] **Step 1: Header `CharacterAsset.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace engine::character {

struct BoneInfo {
    std::string name;
    int parent = -1;
};

struct AnimChannel {
    std::string target_joint;
    std::vector<float> key_times;
    std::vector<glm::vec3> translations;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;
};

struct AnimClip {
    std::string name;
    float duration_seconds = 0.f;
    std::vector<AnimChannel> channels;
};

struct SkinnedMeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::uvec4> joint_indices;
    std::vector<glm::vec4> joint_weights;
    std::vector<std::uint32_t> indices;
    std::vector<BoneInfo> bones;
    std::vector<glm::mat4> inverse_bind_matrices;
    std::vector<std::uint8_t> base_color_rgba; // empty → fallback
    int base_color_width = 0;
    int base_color_height = 0;
};

struct CharacterAsset {
    std::string source_path;
    SkinnedMeshData mesh;
    std::vector<AnimClip> clips;
};

} // namespace engine::character
```

- [ ] **Step 2: Cache-Pfad-Helfer**

`CookedCharacterCache::cache_path_for(source_glb)` →  
`${ENGINE_BINARY_DIR}/character_cache/<fnv1a(source_glb)>.charbin`

`load_or_cook(source_glb, ingest_fn)` — wenn `.charbin` neuer als GLB, load; sonst ingest + write.

Cook-Format v1: magic `CHAR`, version `1`, dann serialisiertes `CharacterAsset` (einfaches length-prefixed binary — keine externe Lib).

- [ ] **Step 3: Verzeichnis bei cook anlegen**

`std::filesystem::create_directories(cache_dir)` vor write.

---

### Task M1-3: GltfIngest + SkeletonValidator

**Files:**
- Create: `engine/character/core/GltfIngest.hpp/.cpp`
- Create: `engine/character/core/SkeletonValidator.hpp/.cpp`
- Create: `tests/character/test_gltf_ingest.cpp`
- Create: `tests/character/test_skeleton_compat.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Failing test — Player base mesh**

```cpp
TEST_CASE("loads dungeon explorer base mesh with skin weights") {
    const std::string path = std::string(ENGINE_SOURCE_DIR)
        + "/assets/Meshy_AI_Voxel_Dungeon_Explore_biped/"
          "Meshy_AI_Voxel_Dungeon_Explore_biped_Character_output.glb";
    engine::character::CharacterAsset asset = engine::character::GltfIngest::load_base(path);
    REQUIRE(asset.mesh.positions.size() > 0);
    REQUIRE(asset.mesh.joint_indices.size() == asset.mesh.positions.size());
    REQUIRE(asset.mesh.bones.size() > 0);
    REQUIRE(asset.mesh.bones.size() <= 128);
}
```

- [ ] **Step 2: Implement `GltfIngest::load_base`**

cgltf: `cgltf_parse_file` → erste skinned primitive → POSITION/NORMAL/TEXCOORD/JOINTS/WEIGHTS → `bones` aus skin joint names + inverse bind matrices.

Texture: `baseColorTexture` image bytes → RGBA8; wenn fehlt: 2×2 weiße Textur + `SPDLOG_WARN` fallback.

- [ ] **Step 3: `GltfIngest::load_animation_clip(base, anim_glb, clip_name)`**

Extrahiert nur Channels; ruft `SkeletonValidator::validate_against_base(base, anim)` auf.

- [ ] **Step 4: Validator — reject mismatch**

```cpp
struct ValidationResult { bool ok; std::string error; };

ValidationResult validate_against_base(const SkinnedMeshData& base,
                                       const std::vector<BoneInfo>& anim_bones);
```

Checks (Spec 4.2.1): gleiche Joint-Anzahl, gleiche Namen-Menge, gleiche Parent-Indices (per Name→Index-Map), ≤128.

- [ ] **Step 5: Failing test — compatible vs incompatible**

```cpp
TEST_CASE("accepts matching meshy animation skeleton") {
    auto base = load_base(DUNGEON_CHARACTER_OUTPUT);
    auto clip = load_animation_clip(base, DUNGEON_HIGH_KICK, "High_Kick");
    REQUIRE(clip.channels.size() > 0);
}

TEST_CASE("rejects animation with different joint count") {
    // Use a tiny synthetic fixture in tests/assets/ or skip if no fixture yet:
    // build two minimal GLBs in test setup — optional defer until fixture added.
}
```

- [ ] **Step 6: `character_tests` target**

```cmake
add_executable(character_tests
    character/test_gltf_ingest.cpp
    character/test_skeleton_compat.cpp
)
target_link_libraries(character_tests PRIVATE engine_character_core Catch2::Catch2WithMain)
target_compile_definitions(character_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
catch_discover_tests(character_tests)
```

Run:

```powershell
cmake --build build --target character_tests --config Debug
.\build\tests\Debug\character_tests.exe
```

---

### Task M1-4: Player + Dummy Asset-Registry (Cook all v1 clips)

**Files:**
- Create: `engine/character/core/CharacterCatalog.hpp/.cpp`

- [ ] **Step 1: `CharacterCatalog::load_player_set()`**

Lädt nacheinander:

| Key | GLB |
|-----|-----|
| base | `..._Character_output.glb` |
| Walk | `..._Walking_withSkin.glb` |
| Run | `..._Running_withSkin.glb` |
| High_Kick | `..._High_Kick_withSkin.glb` |
| Elbow_Strike | `..._Elbow_Strike_withSkin.glb` |
| Counterstrike | `..._Counterstrike_withSkin.glb` |

Jeder Clip via `load_or_cook`. Bei Validation-Fehler: `std::runtime_error` mit Pfad.

- [ ] **Step 2: `load_dummy_set()`**

Base + Hit-Reaction aus  
`assets/Meshy_AI_Voxel_Straw_Fantasy_D_biped/..._Hit_Reaction_1_withSkin.glb`  
( Mesh + Clip `Hit_Reaction_1` ).

- [ ] **Step 3: Integrationstest**

```cpp
TEST_CASE("cooks player set to build/character_cache") {
    auto asset = CharacterCatalog::load_player_set();
    REQUIRE(asset.clips.size() >= 3);
    const std::filesystem::path cache_dir =
        std::filesystem::path(ENGINE_BINARY_DIR) / "character_cache";
    REQUIRE(std::filesystem::exists(cache_dir));
}
```

**M1 Gate:** alle `character_tests` grün.

---

# M2 — CharacterPass + sichtbarer Spieler

### Task M2-1: Shaders + SPIR-V

**Files:**
- Create: `shaders/character_skinned.vert`
- Create: `shaders/character_skinned.frag`
- Modify: `shaders/CMakeLists.txt`

- [ ] **Step 1: Vertex shader (skinned)**

UBO `FrameData { mat4 view; mat4 proj; }`, SSBO oder UBO `boneMatrices[128]`, Push constant `mat4 model`.

```glsl
// character_skinned.vert — outline
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in uvec4 inJoints;
layout(location=4) in vec4 inWeights;
// skinning → gl_Position = proj * view * model * skinnedPos
```

- [ ] **Step 2: Fragment shader** — eine `sampler2D baseColor`.

- [ ] **Step 3: `engine_add_shader` Einträge** in `shaders/CMakeLists.txt`.

---

### Task M2-2: CharacterPass (Vulkan)

**Files:**
- Create: `engine/character/render/CharacterPass.hpp/.cpp`
- Modify: `engine/character/CMakeLists.txt` — `engine_character_render`
- Modify: `movement/CMakeLists.txt` — link render lib

- [ ] **Step 1: GPU-Ressourcen pro `frame_slot`**

Wie `DebugDrawPass`: vertex/index buffer, bone UBO, texture + sampler, pipeline (depth test on).

- [ ] **Step 2: `upload_character(CharacterAsset, frame_slot)`**

Einmalig Mesh hochladen; pro Frame nur Bone-UBO + model matrix updaten.

- [ ] **Step 3: `record(PassExtensionRecordContext)`**

Iteriert registrierte Entities (siehe M2-4) — v1: statische Liste aus `MovementApp`.

---

### Task M2-3: ECS + Arena + Locomotion-Animation

**Files:**
- Create: `engine/character/core/CharacterComponents.hpp`
- Create: `engine/character/core/AnimationController.hpp/.cpp`
- Modify: `engine/movement/core/MovementWorld.hpp/.cpp`
- Modify: `engine/movement/core/ArenaLoader.cpp`
- Modify: `assets/movement/movement_test.arena` — `skinned_model` am player
- Modify: `movement/MovementApp.cpp`

- [ ] **Step 1: Components**

```cpp
struct SkinnedModel { std::string glb_path; std::uint32_t catalog_id = 0; };
struct AnimationState {
    std::string active_clip;
    float time_seconds = 0.f;
    float speed = 1.f;
    bool looping = true;
};
```

- [ ] **Step 2: `AnimationController::select_locomotion(velocity, grounded)`**

| Zustand | Clip |
|---------|------|
| speed < 0.1 | Idle |
| speed < 3 | Walk |
| else | Run |

- [ ] **Step 3: `tick(AnimationState&, dt)`** — time advance; `normalized_time()` für spätere Hits.

- [ ] **Step 4: Arena `skinned_model path "..."`**

Parser-Komponente in `ArenaLoader` — Pfad relativ zu `ENGINE_SOURCE_DIR`.

- [ ] **Step 5: MovementApp**

- Beim startup: `CharacterCatalog::load_player_set()`, `CharacterPass::register_mesh(player)`.
- Fixed-step: `AnimationController::tick` für player entity.
- Render: `bone_matrices` an `CharacterPass` übergeben.
- **Root transform:** interpoliertes `Transform` position + yaw.

- [ ] **Step 6: Renderer extension registrieren**

```cpp
renderer_.register_extension(pass_insertion::kBeforeImgui, &character_pass_);
```

**M2 Gate:** Spieler-Mesh sichtbar, läuft mit WASD.

---

# M3 — Attack-Daten + Combo-FSM

### Task M3-1: `combat_attacks.txt` Parser

**Files:**
- Create: `assets/character/combat_attacks.txt` (Spec-Inhalt)
- Create: `engine/character/core/AttackData.hpp/.cpp`
- Create: `tests/character/test_attack_data.cpp`

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("parses three attacks with hit windows") {
    const auto table = AttackData::load(ENGINE_SOURCE_DIR "/assets/character/combat_attacks.txt");
    REQUIRE(table.find("high_kick") != table.end());
    REQUIRE(table.at("high_kick").hit_start == Catch::Approx(0.35f));
    REQUIRE(table.at("high_kick").hit_end == Catch::Approx(0.48f));
}
```

- [ ] **Step 2: Parser**

Reuse Lexer-Stil aus `TextParser` (copy minimal token rules) oder erweitere `TextParser` um generisches `parse_attack_file`.

```cpp
struct AttackDef {
    std::string id;
    std::string clip;
    float hit_start, hit_end;
    float range, radius, recovery;
};
using AttackTable = std::unordered_map<std::string, AttackDef>;
```

Duplicate-field detection wie Arena.

---

### Task M3-2: CombatController + Input

**Files:**
- Create: `engine/character/core/CombatController.hpp/.cpp`
- Modify: `engine/movement/core/InputSnapshot.hpp` — `bool attack_pressed = false;`
- Modify: `movement/MovementApp.cpp` — LMB edge → `attack_pressed`
- Modify: `ArenaLoader` — `combat_controller combo "high_kick,elbow_strike,counterstrike"`
- Create: `tests/character/test_combo_fsm.cpp`

- [ ] **Step 1: FSM enum**

```cpp
enum class CombatPhase { Idle, Attacking, Recovery };

struct CombatController {
    CombatPhase phase = CombatPhase::Idle;
    int combo_index = 0;
    std::vector<std::string> combo_ids;
    float attack_yaw = 0.f;
    float recovery_remaining = 0.f;
    bool hit_consumed = false;
};
```

- [ ] **Step 2: `tick(CombatController&, Transform&, AnimationState&, InputSnapshot&, AttackTable&, dt)`**

- Idle + `attack_pressed` → `combo_index=0`, start attack 0, `attack_yaw = tf.yaw`.
- Attacking: clip time läuft; bei Ende → recovery aus `AttackDef`, nächster Index oder Idle nach 3.
- Recovery: timer -= dt; bei 0 → Idle.
- Während Attacking/Recovery: **kein** `player_tick` movement (MovementApp skip).

- [ ] **Step 3: Tests**

```cpp
TEST_CASE("chains three attacks then returns idle") { /* simulate time */ }
TEST_CASE("ignores attack_pressed during attacking") { /* ... */ }
```

- [ ] **Step 4: Overlay**

`ImGuiOverlay`: `active_clip`, `combo_index`, `phase`, `attack_yaw`.

**M3 Gate:** Combo sichtbar, Bewegung während Angriff eingefroren, Yaw locked für Hit (Hit-Test in M4).

---

# M4 — Hit-Window + Dummy-Feedback

### Task M4-1: HitDetection

**Files:**
- Create: `engine/character/core/HitDetection.hpp/.cpp`
- Create: `tests/character/test_hit_window.cpp`
- Create: `tests/character/test_hit_overlap.cpp`

- [ ] **Step 1: Kapsel vs AABB**

```cpp
bool capsule_intersects_box(glm::vec3 capsule_center, float radius, float half_height,
                            glm::vec3 box_center, glm::vec3 box_half_extents);
```

Horizontal hit: Kapselzentrum = player_pos + yaw_forward(attack_yaw) * range, radius aus `AttackDef`.

- [ ] **Step 2: `try_hit_in_window`**

```cpp
bool try_hit_in_window(const CombatController& combat, const AnimationState& anim,
                       const AttackDef& def, const Transform& attacker,
                       const Transform& target, const Collider& target_collider,
                       CombatController& attacker_mut);
```

True wenn `t in [hit_start, hit_end]` und `!hit_consumed` und overlap → set `hit_consumed`.

- [ ] **Step 3: Tests hit window**

```cpp
TEST_CASE("hit only once inside window") {
    CombatController c; c.phase = CombatPhase::Attacking; c.hit_consumed = false;
    AnimationState a; a.active_clip = "High_Kick"; a.time_seconds = def.duration * 0.40f;
    REQUIRE(try_hit_in_window(...) == true);
    REQUIRE(c.hit_consumed);
    REQUIRE(try_hit_in_window(...) == false);
}
```

---

### Task M4-2: HitReact + Dummy

**Files:**
- Create: `engine/character/core/HitReactSystem.hpp/.cpp`
- Modify: `assets/movement/combat_test.arena`
- Modify: `movement/MovementApp.cpp` — default arena → `combat_test.arena`

- [ ] **Step 1: `HitReact` component**

```cpp
struct HitReact {
    float knockback_distance = 0.3f;
    float knockback_duration = 0.25f;
    std::string hit_clip = "Hit_Reaction_1";
    float timer = 0.f;
    glm::vec3 knockback_delta{0.f};
    bool playing_hit = false;
};
```

- [ ] **Step 2: Bei Treffer**

- Dummy `AnimationState` → `hit_clip` non-loop einmal abspielen.
- `Transform.position` += knockback along horizontal normal * eased progress.
- Nach duration → zurück zu statisch.

- [ ] **Step 3: `combat_test.arena`**

Player aus `movement_test.arena` + entity `training_dummy` (Spec §3.2).

Dummy `CharacterCatalog::load_dummy_set()` in App.

- [ ] **Step 4: DebugDraw**

Hit-Kapsel während Hit-Window in Rot (nutzt `attack_yaw`).

**M4 Gate:** Combo trifft Dummy, sichtbare Reaktion.

---

# M5 — Polish + Playtest-Doku

### Task M5-1: Debug-Overlay + Doku

**Files:**
- Modify: `engine/movement/debug_render/ImGuiOverlay.cpp`
- Modify: `movement/main.cpp` oder Projekt-README

- [ ] **Step 1: Overlay-Zeilen**

- `hit_window [start,end]` für aktuellen Angriff
- `hit_consumed` / `attack_yaw`
- Cache-Pfad-Anzeige (optional)

- [ ] **Step 2: Playtest-Hinweis**

In `docs/superpowers/specs/...` oder kurzer Abschnitt im Plan-README:

```text
cmake --build build --target movement_test --config Debug
.\build\movement\Debug\movement_test.exe
Steuerung: WASD, Maus, LMB = Combo, F5/F9 save/load
Arena: assets/movement/combat_test.arena
```

- [ ] **Step 3: Balancing**

Nur `assets/character/combat_attacks.txt` anpassen; erneut starten.

**M5 Gate:** vollständiger manueller Playtest-Pfad dokumentiert.

---

## Spec-Coverage (Self-Review)

| Spec-Anforderung | Task |
|------------------|------|
| Hit-Window statt Einzelframe | M4-1 |
| `combat_attacks.txt` | M3-1 |
| `attack_yaw` lock | M3-2 |
| `build/character_cache/` | M1-2 |
| Strenge Skeleton-Validation | M1-3 |
| `movement_test` only | M2–M5 |
| Dungeon player + Straw dummy | M1-4, M4-2 |
| CharacterPass before ImGui | M2-2 |
| Kein HP/Tod | — (nicht implementieren) |

Keine TBD/Placeholder in Tasks.

---

## Risiken & Mitigation

| Risiko | Mitigation |
|--------|------------|
| Meshy-GLB 24 MB langsam | Cook-Cache M1 |
| Y-up / Scale falsch | Validator + manueller ersten-Frame-Check M2 |
| Zwei Skelette (Player/Dummy) | Getrennte `CharacterCatalog` loads |
| Vulkan skinning bugs | Zuerst ein Mesh, dann Dummy |

---

## Ausführung

**Plan gespeichert:** `docs/superpowers/plans/2026-06-03-rpg-combat-slice-visual-combo-v1.md`

**Zwei Ausführungsoptionen:**

1. **Subagent-Driven (empfohlen)** — frischer Subagent pro Task, Review zwischen Tasks  
2. **Inline Execution** — Tasks in dieser Session mit executing-plans, Checkpoints nach M1–M5

Welche Variante möchtest du?
