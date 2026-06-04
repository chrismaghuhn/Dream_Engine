# Phase B — Vertex AO + Baked Light — Design

**Status:** Implemented (2026-06-04, commit `fce273b`; design rev. 2)
**Roadmap:** `2026-06-04-rendering-performance-roadmap-design.md` (Phase B)
**Bezug:** `2026-06-03-voxel-engine-design.md` §10.5 (Vertices), §16 (Lighting), §18 (Water)

## Ziel

Terrain wirkt räumlich lesbar: **Ecken dunkler** (Vertex-AO), **Höhlen und Fackeln** sichtbar über die bereits gebakten **sky/block-Light-Nibbles** in `TerrainVertex.light`. Heute ist `vert.ao` immer `0` und der Terrain-Shader ignoriert `ao` sowie `light` — Beleuchtung ist ein festes Directional + Ambient.

Phase B schließt den Prototyp-Lighting-Loop auf Terrain (§16) ohne das 8-Byte-Vertex-Layout zu ändern.

**Erfolgskriterien (manuell):**

1. Einzelner 1³-Stein auf offener Fläche — mindestens eine Ecke heller als eine Innenecke an Boden/Wand.
2. L-förmige Steinwand — gemeinsame Innenecke spürbar dunkler als exponierte Flächen.
3. Fackel in Höhle — warme Aufhellung auf angrenzenden Faces (block light), AO bleibt in Ecken wirksam.

---

## Scope

### In Scope

- `corner_ao()` mit **pro-Ecke-Tangentenrichtung** (`du_sign`, `dv_sign`) → `vert.ao` ∈ {0, 1, 2, 3}
- `vert.light` unverändert befüllt (`corner_light` + Border); Shader konsumiert beide Felder **interpoliert**
- `TerrainPass` + `WaterPass`: vier Vertex-Attribute (pos, material, ao, light)
- Shader: `terrain.vert/frag`, `water.vert/frag`
- Wasser: Vertex-Light ja; `vert.ao = 3` via explizitem `is_water_layer` (§18)
- Catch2-Tests für AO-Geometrie; bestehende Light-Regression bleibt grün

### Out of Scope

- `SectionRenderMeta` / Phase A
- `WorldRenderSnapshot::sun_dir_intensity` / `ambient_fog` → Uniform (Day/Night noch nicht befüllt)
- Änderungen an Block-Light-Flood oder Dirty-Propagation
- GI, `TerrainVertexCompact`, Character-Pass, Sky-Pass
- Observability-Zähler (optional Debug, nicht Pflicht)

---

## B1. AO-Algorithmus (Mesher)

### Formel

Für jede Quad-Ecke drei Nachbarvoxel in der **Luft-Hälfte** der Fläche:

```
a = opaque(side1) ? 1 : 0
b = opaque(side2) ? 1 : 0
c = opaque(corner) ? 1 : 0
ao = clamp(3 - (a + b + max(c, a * b)), 0, 3)
```

`opaque(sample)` ⇔ `sample.opaque_solid`. **Wasser, Torch, Luft** occluden nicht.

### API (Pflicht)

`corner_ao` darf **nicht** nur `axis` + `positive` verwenden — alle vier Ecken eines Faces teilen sonst denselben AO-Kontext oder spiegeln auf negativen Faces falsch.

```cpp
uint8_t corner_ao(
    int axis,
    bool positive,
    int x,
    int y,
    int z,
    int du_sign,   // -1 oder +1: tangential U-Richtung dieser Ecke
    int dv_sign,   // -1 oder +1: tangential V-Richtung dieser Ecke
    const Section& section);
```

`du_sign` / `dv_sign` kommen aus `emit_quad` pro Ecke (siehe unten). Optional später: `QuadCorner{position, du_sign, dv_sign}` statt einzelner ints.

### Tangenten-Basis aus `axis`

Gleiche Achsen-Zuordnung wie `emit_quad` / `sample_axis`:

| axis | U-Achse (eu) | V-Achse (ev) | Normal (Luft-Seite) |
|------|--------------|--------------|---------------------|
| 0 (X) | Y | Z | ±X je nach `positive` |
| 1 (Y) | X | Z | ±Y |
| 2 (Z) | X | Y | ±Z |

### Sample-Positionen

1. `base` = Ecke `(x,y,z)` + **ein Schritt in Normalenrichtung** (von Solid auf Luft-Seite der Fläche).
2. `side1` = `base + du_sign * eu`
3. `side2` = `base + dv_sign * ev`
4. `corner` = `base + du_sign * eu + dv_sign * ev`

Alle Koordinaten über `sample_voxel(section, …)` (inkl. `SectionBorderCache`).

### `du_sign` / `dv_sign` in `emit_quad`

Beim Quad-Aufbau kennt `emit_quad` bereits `u0`, `v0`, `width`, `height` und die vier Eck-Koordinaten. Pro Ecke `i`:

- Ecke liegt auf **max-U-Seite** des Quads (`u == u0 + width` in tangent space) → `du_sign = +1`, sonst `-1`.
- Ecke liegt auf **max-V-Seite** (`v == v0 + height`) → `dv_sign = +1`, sonst `-1`.

Die Zuordnung U/V → `(x,y,z)` folgt derselben `switch (axis)`-Logik wie die Corner-Berechnung heute (axis 0: U=y, V=z; axis 1: U=x, V=z; axis 2: U=x, V=y).

**Test-Invariante:** Auf einem asymmetrischen Face (z. B. nur eine Seite offen) dürfen **nicht** alle vier Vertices dieselbe `ao` haben.

### Integration in `emit_quad`

Signatur erweitern:

```cpp
void emit_quad(
    int axis,
    bool positive,
    int slice,
    int u0,
    int v0,
    int width,
    int height,
    const VoxelSample& face_side,
    const Section& section,
    bool is_water_layer,
    std::vector<TerrainVertex>& vertices,
    std::vector<uint32_t>& indices);
```

Pro Ecke:

```cpp
vert.ao = is_water_layer
        ? 3u
        : corner_ao(axis, positive, c.x, c.y, c.z, du_sign, dv_sign, section);
vert.light = corner_light(c.x, c.y, c.z, face_side, section);
```

`mesh_section_layer` übergibt `is_water_layer = (face_predicate == water_face_predicate)` **einmal** beim Aufruf — **kein** Funktionspointer-Vergleich innerhalb von `emit_quad`.

Greedy-Merge unverändert — jede Ecke eigene Koordinaten + eigene Tangentenzeichen + eigene AO.

---

## B2. Shader-Light-Modell

### Vertex-Input (Vulkan)

| Location | Format | `TerrainVertex`-Offset |
|----------|--------|-------------------------|
| 0 | `VK_FORMAT_R32_UINT` | `packed_position_normal` |
| 1 | `VK_FORMAT_R16_UINT` | `material_id` |
| 2 | `VK_FORMAT_R8_UINT` | `ao` |
| 3 | `VK_FORMAT_R8_UINT` | `light` |

`TerrainPass.cpp` und `WaterPass.cpp`: `vertexAttributeDescriptionCount = 4`, stride `sizeof(TerrainVertex)`.

### Interpolation (Pflicht)

**AO und Light werden interpoliert**, nicht `flat`. Vertex-AO lebt vom Gradienten zwischen Ecken; `flat` würde pro Triangle einen konstanten Wert erzwingen und Kanten falsch hart machen.

Nur **Material-ID und Face-ID** bleiben `flat` (wie heute `v_material` / `v_face`).

### `terrain.vert`

```glsl
float ao_mul_from_u8(uint ao) {
    if (ao == 0u) return 0.55;
    if (ao == 1u) return 0.72;
    if (ao == 2u) return 0.86;
    return 1.00;
}

// ...
layout(location = 2) out float v_ao_mul;      // smooth
layout(location = 3) out vec2 v_light_levels; // .x = sky/15, .y = block/15 — smooth

v_ao_mul = ao_mul_from_u8(in_ao);
v_light_levels = vec2(float(in_light >> 4u) / 15.0, float(in_light & 15u) / 15.0);
```

### `terrain.frag`

```glsl
const vec3 sky_tint   = vec3(0.85, 0.92, 1.00);
const vec3 block_tint = vec3(1.00, 0.82, 0.55);
// v_ao_mul, v_light_levels interpoliert vom Rasterizer
```

- **Basis:** `albedo * (v_light_levels.x * sky_tint + v_light_levels.y * block_tint + ambient_floor)` mit `ambient_floor ≈ 0.12`
- **AO:** `color *= v_ao_mul` (kein `ao_mul[v_ao]` mit uint — Multiplier kommt bereits als float aus VS)
- **Relief:** schwacher fixer Directional (~15–20 %) — ersetzt nicht Vertex-Light

`WorldRenderSnapshot::sun_dir_intensity` wird **nicht** angebunden; Kommentar-Hook für Day/Night später.

### Wasser

- `water.vert`: `v_light_levels` interpoliert; `v_ao_mul = 1.0` (oder AO-Attribut ignorieren)
- `water.frag`: sky/block auf Wasserfarbe; **kein** Ecken-Darkening
- Mesher: `is_water_layer` → `vert.ao = 3`

---

## B3. Pipeline & Invalidierung

| Bereich | Dateien |
|---------|---------|
| Mesher | `engine/world/GreedyMesher.cpp`, `GreedyMesher.hpp` (`emit_quad` Signatur) |
| Vulkan | `engine/render/TerrainPass.cpp`, `WaterPass.cpp` |
| Shader | `shaders/terrain.vert`, `terrain.frag`, `water.vert`, `water.frag` |

**Unverändert:** `TerrainVertex`-Layout (8 bytes), `pack_vertex`, Upload-Queue, `WorldRenderSnapshot`-Vertrag.

**Invalidierung:** Block-Light dirty → Remesh → neue `light`-Nibbles.

---

## B4. Tests (Catch2)

In `tests/world/test_greedy_mesher.cpp` (oder `test_vertex_ao.cpp`):

1. **L-Corner** (zuerst) — zwei orthogonale Solids; innere Kante `ao <= 1`, exponierte Ecke höher.
2. **Border-AO** (zuerst) — Solid an Section-Kante + Border-Nachbar; AO unterscheidet sich von offener Seite.
3. **Vier Ecken eines Faces** — asymmetrisches Setup: nicht alle vier `ao` identisch.
4. **Einzelblock 1³** — mindestens ein Vertex `ao < 3`.
5. **Wasser-only 1³** — alle `vert.ao == 3`.
6. **Light regression** — `border light copied to edge vertex nibbles` bleibt grün.
7. **AO unabhängig von Light** — niedriges AO + hohes `block_light` möglich.

Shader: kein GPU-Unit-Test; visuell über Erfolgskriterien.

---

## B5. Empfohlene Implementierungs-Reihenfolge

Für den späteren Plan (`writing-plans`):

1. **Vertex-Attribute** in `TerrainPass` / `WaterPass` — Locations 2/3 korrekt; kurzer Readback-/Dummy-Draw optional.
2. **Shader** konsumiert `ao` + `light` — zuerst Dummy (z. B. AO hart auf 0.5 mul), sichtbar machen dass Attribute ankommen.
3. **Wasser** — `is_water_layer`, `ao = 3`, Light interpoliert, kein AO-Darkening.
4. **`corner_ao`** mit `du_sign` / `dv_sign` — echte Werte; `emit_quad`-Signatur + Tangentenzeichen pro Ecke.
5. **Catch2** — L-Corner und Border-AO zuerst, dann Vier-Ecken-Invariante.
6. **Visueller Smoke** — Einzelblock, L-Wand, Höhle + Fackel; Shader-Kurven finalisieren.

---

## B6. Abhängigkeiten

- **Unabhängig von Phase A** — parallel möglich.
- Vertex-AO rein visuell; keine Kopplung an LOD/Occlusion-Metadaten.

---

## B7. Nicht-Ziele

- Kein Remesh nur für Shader-Konstanten.
- Keine AO auf `CharacterPass`.
- Kein `face_predicate`-Vergleich in `emit_quad`.
- Keine Änderung an Greedy-Mask-Keys.

---

## Ansatz-Entscheidung

**Minecraft-Style Corner-AO + interpoliertes Vertex-Light** (ohne Snapshot-Sun). Rev. 2: pro-Ecke `du_sign`/`dv_sign`, explizites `is_water_layer`, AO/Light smooth im Rasterizer.