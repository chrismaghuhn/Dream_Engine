# Phase B — Vertex AO + Baked Light — Design

**Status:** Ready for user review (2026-06-04)
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

- `corner_ao()` in `GreedyMesher` → `vert.ao` ∈ {0, 1, 2, 3}
- `vert.light` unverändert befüllt (`corner_light` + Border); Shader konsumiert beide Felder
- `TerrainPass` + `WaterPass`: vier Vertex-Attribute (pos, material, ao, light)
- Shader: `terrain.vert/frag`, `water.vert/frag`
- Wasser: Vertex-Light ja; `vert.ao = 3` immer (§18 — Wasser castet kein AO)
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

Für jede Quad-Ecke drei Nachbarvoxel in der **Luft-Hälfte** der Fläche (Seite des Vertex, die zur Luft zeigt):

```
a = opaque(side1) ? 1 : 0
b = opaque(side2) ? 1 : 0
c = opaque(corner) ? 1 : 0
ao = clamp(3 - (a + b + max(c, a * b)), 0, 3)
```

`opaque(sample)` ⇔ `sample.opaque_solid` (wie `opaque_face_predicate`). **Wasser, Torch, Luft** occluden nicht.

### Sampling

`sample_voxel(section, x, y, z)` — identischer Pfad wie `corner_light` inkl. `SectionBorderCache` an Section-Grenzen.

### Offset-Tabelle (section-local, relativ zur Ecke)

Offsets hängen von `axis` (0=X, 1=Y, 2=Z) und `positive` (0=negative Normal, 1=positive Normal) ab — dieselben Parameter wie `emit_quad`. Alle Samples liegen auf der **Luft-Seite** der Fläche (ein Schritt vom Solid weg in Normalenrichtung, dann tangentiale ±1):

| axis | positive | side1 Δ | side2 Δ | corner Δ |
|------|----------|---------|---------|----------|
| 0 | 1 (+X) | (0,-1,0) | (0,0,-1) | (0,-1,-1) |
| 0 | 0 (-X) | (0,-1,0) | (0,0,-1) | (0,-1,-1) |
| 1 | 1 (+Y) | (-1,0,0) | (0,0,-1) | (-1,0,-1) |
| 1 | 0 (-Y) | (-1,0,0) | (0,0,-1) | (-1,0,-1) |
| 2 | 1 (+Z) | (-1,0,0) | (0,-1,0) | (-1,-1,0) |
| 2 | 0 (-Z) | (-1,0,0) | (0,-1,0) | (-1,-1,0) |

Vor dem Sampling: Ecke `(x,y,z)` um **einen Schritt in Normalenrichtung** verschieben (von der Solid-Seite auf die Luft-Seite der Fläche), dann die Δ-Offsets addieren. Implementierung als `corner_ao(axis, positive, x, y, z, section)` in `GreedyMesher.cpp`.

### Integration in `emit_quad`

```cpp
vert.ao = (face_predicate == water_face_predicate)
        ? 3u
        : corner_ao(axis, positive, c.x, c.y, c.z, section);
vert.light = corner_light(c.x, c.y, c.z, face_side, section);
```

`emit_quad` erhält `face_predicate` (bereits über `mesh_section_layer` vorhanden) oder ein explizites `bool is_water_layer`.

Greedy-Merge bleibt unverändert — jede Ecke hat eigene Koordinaten und eigene AO.

---

## B2. Shader-Light-Modell

### Vertex-Input (Vulkan)

| Location | Format | `TerrainVertex`-Offset |
|----------|--------|-------------------------|
| 0 | `VK_FORMAT_R32_UINT` | `packed_position_normal` |
| 1 | `VK_FORMAT_R16_UINT` | `material_id` |
| 2 | `VK_FORMAT_R8_UINT` | `ao` |
| 3 | `VK_FORMAT_R8_UINT` | `light` |

`TerrainPass.cpp` und `WaterPass.cpp`: `vertexAttributeDescriptionCount = 4`, stride weiterhin `sizeof(TerrainVertex)`.

### `terrain.vert`

Weiterreichen: `v_ao`, `v_light` (flat oder smooth — flat reicht).

### `terrain.frag`

```glsl
float sky_lvl   = float(v_light >> 4u) / 15.0;
float block_lvl = float(v_light & 15u) / 15.0;
const vec3 sky_tint   = vec3(0.85, 0.92, 1.00);
const vec3 block_tint = vec3(1.00, 0.82, 0.55);
const float ao_mul[4] = float[4](0.55, 0.72, 0.86, 1.00);
```

- **Basis:** `albedo * (sky_lvl * sky_tint + block_lvl * block_tint + ambient_floor)` mit `ambient_floor ≈ 0.12`
- **AO:** `*= ao_mul[v_ao]`
- **Relief:** schwacher fixer Directional (`ndl` clamp 0.2–1.0, Gewicht ~15–20 %) — ersetzt nicht Vertex-Light

`WorldRenderSnapshot::sun_dir_intensity` wird **nicht** angebunden; Kommentar im Shader als Hook für Day/Night später.

### Wasser

- `water.vert`: `v_light` durchreichen
- `water.frag`: sky/block wie oben auf Wasserfarbe; **kein** AO-Darkening (§18)
- Mesher setzt `ao = 3` für Wasser-Quads

---

## B3. Pipeline & Invalidierung

| Bereich | Dateien |
|---------|---------|
| Mesher | `engine/world/GreedyMesher.cpp` |
| Vulkan | `engine/render/TerrainPass.cpp`, `WaterPass.cpp` |
| Shader | `shaders/terrain.vert`, `terrain.frag`, `water.vert`, `water.frag` |

**Unverändert:** `TerrainVertex`-Layout (`static_assert` 8 bytes), `pack_vertex`, Upload-Queue, `WorldRenderSnapshot`-Vertrag, indirect draw.

**Invalidierung:** Block-Light-Änderungen markieren Sections dirty → Remesh liefert neue `light`-Werte. Kein neuer Event-Typ.

---

## B4. Tests (Catch2)

In `tests/world/test_greedy_mesher.cpp` (oder `test_vertex_ao.cpp`):

1. **Einzelblock 1³** — mindestens ein Vertex mit `ao < 3`.
2. **L-Tunnel** — zwei orthogonale Solids; Vertex an innerer Kante hat `ao <= 1` (typisch `0`).
3. **Wasser-only 1³** — alle `vert.ao == 3`.
4. **Border-AO** — Solid `x=15`, Border-Nachbar solid auf `PX`; Ecke an exponierter Fläche vs. verdeckter Seite unterschiedliche AO.
5. **Light regression** — `greedy mesher border light copied to edge vertex nibbles` bleibt grün.
6. **AO unabhängig von Light** — Ecke mit niedrigem AO kann hohes `block_light` (Fackel-Szenario konstruiert).

Shader: kein GPU-Unit-Test in Phase 1; visuell über Erfolgskriterien oben.

---

## B5. Abhängigkeiten

- **Unabhängig von Phase A** — kann parallel implementiert werden.
- **Roadmap Phase C–H** profitieren nicht direkt von Vertex-AO; Metadaten aus Phase A bleiben separat.

---

## B6. Nicht-Ziele

- Kein Remesh nur wegen Shader-Konstanten-Tweak.
- Keine AO auf `CharacterPass`.
- Keine Änderung an `GreedyMesher`-Mask-Keys (AO hängt nicht an Light-Merge-Key).

---

## Ansatz-Entscheidung

Gewählt: **Minecraft-Style Corner-AO + Vertex-Light im Shader** (ohne Snapshot-Sun-Uniform). Alternativen verworfen: Snapshot-Sun in Phase B (Felder nie befüllt), runtime light SSBO (widerspricht Bake-Vertrag).