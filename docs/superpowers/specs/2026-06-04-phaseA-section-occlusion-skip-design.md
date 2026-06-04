# Phase A — Section-Occlusion-Skip — Design

**Status:** Approved (2026-06-04)
**Roadmap:** `2026-06-04-rendering-performance-roadmap-design.md` (Phase A)
**Bezug:** `2026-06-03-voxel-engine-design.md` §5 (Snapshot), §10.5 (Indexing), §12 (Meshing/Borders), §13 (Renderer)

## Ziel

Sections, die **gar keine sichtbare Geometrie** erzeugen, weder meshen noch zeichnen — und dabei wiederverwendbare Occluder-Metadaten (`SectionRenderMeta`) anlegen, die spätere Phasen (D LOD, E Occlusion, F GPU-Culling) konsumieren.

Zwei Fälle:
1. **Leere Section** (nur Luft / nichts Renderbares) → nie meshen.
2. **Vollständig vergrabene opake Section** (alle Zellen opak, alle 6 Nachbar-Randflächen opak) → produziert ohnehin 0 Faces; wir sparen den **Mesh-Job**.

Heute wird zwar eine leere/0-Index-Mesh in `build_snapshot` nicht gezeichnet, aber der **Mesh-Job läuft trotzdem**. Phase A vermeidet diese Arbeit und legt die Foundation.

## A1. Datenstruktur

Intrinsisch pro Section (keine Nachbar-Info), lebt in `Section` neben `occupancy`/`border`.

```cpp
// engine/world/Section.hpp
struct SectionRenderMeta {
    bool    is_empty        = true;   // kein renderbarer Block (opak ODER Wasser)
    bool    is_opaque_full  = false;  // alle SECTION_DIM^3 Zellen opak-solide
    uint8_t face_solid_mask = 0;      // 1 Bit pro Face in Face-Reihenfolge (PX,NX,PY,NY,PZ,NZ):
                                      // die 16x16-Randschicht an dieser Fläche ist komplett opak
};
```

Helper:
```cpp
inline bool face_solid(const SectionRenderMeta& m, Face f) {
    return (m.face_solid_mask >> static_cast<uint32_t>(f)) & 1u;
}
```

`Face`-Enum/Reihenfolge wie in `SectionIndexing.hpp` (`PX=0,NX=1,PY=2,NY=3,PZ=4,NZ=5`).

## A2. Berechnung — mit O(1)-Fast-Paths

`Section::recompute_render_meta()`. Aufruf an denselben Stellen wie `sync_occupancy_from_blocks` (nach Worldgen-Fill, nach Block-Write).

Reihenfolge der Prüfungen:
1. **Palette == nur `air`** → `is_empty=true`, `is_opaque_full=false`, `face_solid_mask=0`. Fertig. (Häufigster Fall: Luft über Grund.)
2. **Palette == genau ein opaker Block** → `is_empty=false`, `is_opaque_full=true`, `face_solid_mask=0x3F` (alle 6). Fertig. (Häufigster Fall: Stein unter Tage.)
3. **Sonst** — voller Pfad:
   - `is_empty`: true nur, wenn jede Zelle weder opak noch Wasser ist (= nichts Renderbares).
   - `is_opaque_full`: true nur, wenn jede Zelle opak (`is_solid`/`opaque` laut `BlockRegistry`).
   - **`face_solid_mask` immer wenn nicht leer** (unabhängig von `is_opaque_full`): für jede der 6 Flächen die 16×16-Randschicht scannen; Bit setzen, wenn alle opak. Wird von `section_fully_occluded` auf Nachbarn gelesen — Nachbar-Sections können hohl sein, solange die angrenzende Face-Schicht solid ist.

**Opak vs. Wasser:** Wasser zählt als renderbar (verhindert fälschliches `is_empty` bei Wasser-only Sections), aber **nicht** als opak (kein `face_solid`) — sonst würden Wasseroberflächen weggecullt.

Hinweis: `SectionOccupancy` trackt `is_solid`. Für `face_solid`/`is_opaque_full` ist „opak" maßgeblich; in Phase 1 sind alle soliden Blöcke außer Wasser opak (Wasser ist nicht solid). Implementierung nutzt `is_solid && !is_water` bzw. die `opaque`-Flag aus `BLOCK_REGISTRY`, konsistent zum Mesher (`opaque_face_predicate`).

## A3. „Vergraben"-Test (Consumer-seitig, cross-chunk)

Neuer Helper in `StreamingTerrainSystem` (nutzt dieselbe Nachbar-Auflösung wie `refresh_section_border_cache`):

```
bool section_fully_occluded(coord, section_index):
  const SectionRenderMeta& m = section.render_meta
  if (m.is_empty)            return true   // nichts zu zeichnen
  if (!m.is_opaque_full)     return false  // hat innere/teilweise Oberflächen
  for each Face f in {PX,NX,PY,NY,PZ,NZ}:
      neighbor = neighbor_section_across(coord, section_index, f)   // cross-chunk
      if (neighbor missing OR chunk pending unload)  return false   // konservativ rendern
      if (!face_solid(neighbor.render_meta, opposite(f)))  return false
  return true   // rundum von Solid bedeckt
```

**Konservativ:** Fehlender/ungeladener Nachbar ⇒ rendern (kein Loch am Streaming-Rand).

## A4. Integration

### Scheduling
`StreamingTerrainSystem::schedule_section_mesh`:
- Vor dem Job-Dispatch: wenn `is_empty` **oder** `section_fully_occluded(...)` → **kein** Mesh-Job. Section-State als „mesh_ready, 0 Geometrie" markieren (`opaque_draw_index_count=0`, `water_draw_index_count=0`), damit `build_snapshot` sie wie bisher überspringt.
- Flag `occluded_skip=true` im `SectionMeshState` setzen (für Zähler + spätere Re-Eval).

### build_snapshot
- Keine Logikänderung nötig (0-Index-Sections werden bereits übersprungen). Optional: frühes `continue` bei `occluded_skip`, spart die GPU-Slot-Liveness-Checks.

### Invalidierung (Korrektheit)
- **Block-Break/-Place** in Section S: S wird ohnehin neu gemesht → `recompute_render_meta()` für S → zusätzlich die **6 Nachbarn neu bewerten** (`section_fully_occluded` neu): ein vorher vergrabener Nachbar kann jetzt freiliegen und muss (neu) gemesht werden. Hängt sich an den bestehenden Naht-/Soft-Invalidate-Pfad (`heal_seams_after_chunk_loads` / Nachbar-Remesh).
- **Nachbar-Unload:** fehlender Nachbar ⇒ Regel rendert konservativ ⇒ kein Loch.
- **Nachbar-Load:** beim Seam-Heal kann eine jetzt vergrabene Section ihren Mesh droppen (Optimierung). Optional; Korrektheit hat Vorrang — meshen ist nie falsch, nur suboptimal.

## A5. Tests (Catch2, Stil §8)

In `tests/render/` bzw. `tests/world/`:
1. Leere Section → `recompute_render_meta` ⇒ `is_empty`; `schedule_section_mesh` plant keinen Job.
2. Voll-Solid Section, alle 6 Nachbarn voll-solid ⇒ `section_fully_occluded==true` ⇒ keine Geometrie/kein Job.
3. Voll-Solid Section mit einer Luft-Nachbarfläche ⇒ nicht occluded ⇒ gemesht; nur die exponierte Fläche erzeugt Quads.
4. Block in Nachbar gebrochen ⇒ vorher vergrabene Section wird neu bewertet + gemesht.
5. Cross-chunk: Nachbar über Chunk-Grenze wird in `section_fully_occluded` korrekt einbezogen.
6. Wasser-only Section ⇒ `is_empty==false`, wird **nicht** weggecullt.
7. `face_solid_mask`-Roundtrip: gemischte Section, einzelne Fläche voll opak ⇒ nur deren Bit gesetzt.

## A6. Observability

- `StreamingTerrainSystem::count_occluded_sections()` (Sections mit `occluded_skip`), im ImGui-Overlay neben `count_mesh_ready_sections()` anzeigen, um den Gewinn zu verifizieren.

## A7. Nicht-Ziele (bewusst ausgeklammert)

- Konnektivitäts-/Cave-Culling (BFS von Kamera-Section) → **Phase E**.
- LOD-Auswahl → **Phase D**.
- GPU-seitige Auswertung → **Phase F**.
