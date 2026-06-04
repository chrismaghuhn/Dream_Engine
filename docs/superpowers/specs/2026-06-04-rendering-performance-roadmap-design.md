# Rendering Performance Roadmap — Design

**Status:** Approved (decomposition + ordering, 2026-06-04)
**Scope:** Phasenweiser Ausbau der Terrain-Render-/Streaming-Pipeline für weite Sicht (Ziel: **32+ Chunks Horizont** mit aggressiven Far-Impostors).
**Bezug:** Baut auf `2026-06-03-voxel-engine-design.md` (§5 Snapshot, §10.5 Vertices, §13 Renderer, §14 Streaming).

---

## 0. Ausgangslage (Audit, 2026-06-04)

Abgleich der ursprünglich vorgeschlagenen 10 Optimierungen gegen den IST-Zustand der Engine:

| # | Punkt | Status | Beleg |
|---|-------|--------|-------|
| 1 | Face Culling zwischen Chunks | Größtenteils da (Face-Ebene) | `GreedyMesher.cpp` liest `SectionBorderCache`, cullt Faces gegen Nachbarn cross-chunk. **Fehlt:** ganze verdeckte Sections überspringen |
| 2 | Bessere Meshing-Strategie | Greedy da, Transvoxel/DC nein | `GreedyMesher.cpp` (6-Achsen greedy, opaque+water) |
| 3 | Section-Level Culling | **Fertig** | `StreamingTerrainSystem::build_snapshot` cullt pro Section (Frustum + Distanz) |
| 4 | Async Meshing + Priorisierung | Größtenteils da | JobSystem-Meshing-Pool; Backlog/Upload/GPU-Alloc nach Distanz sortiert. **Fehlt:** echte Prioritäts-Queue |
| 5 | Vertex Compression | **Fertig** | `TerrainVertex` = 8 Byte, Pos+Normal in 1× `uint32` (`SectionIndexing.hpp`) |
| 6 | Bindless / Descriptor Indexing | Nein | Ein `sampler2DArray`, einmal gebunden (`TerrainPass`) |
| 7 | GPU-Driven Rendering | Indirect Draw da, GPU-Culling nein | `vkCmdDrawIndexedIndirect`; Culling CPU-seitig in `build_snapshot` |
| 8 | Chunk LOD | **Nein** | — |
| 9 | Occlusion Culling | **Nein** (nur Frustum + Distanz) | `FrustumCull.hpp` |
| 10 | Lighting AO + Block Light | Light gebacken, **AO = 0** | `GreedyMesher.cpp` `emit_quad`: `vert.ao = 0` |

**Echte Lücken mit hohem Nutzen:** #8 (LOD), #9 (Occlusion), #1-Rest (Section-Occlusion-Skip), #10 (AO ist buchstäblich 0).

## 1. Ziel & Leitprinzip

**Ziel:** View Distance **32+ Chunks** durch Kombination aus LOD0 (nah, voll), fernen LOD-Stufen und Far-Impostors, bei stabiler Frame-Zeit.

**Leitprinzip:** Jede frühe Phase erzeugt **wiederverwendbare Metadaten** (z.B. „ist diese Section ein solider Occluder / leer / voll?"), die spätere Phasen (LOD, Occlusion, GPU-Culling) als Input nutzen — keine Doppelberechnung.

## 2. Phasen-Reihenfolge (bestätigt)

Reihenfolge nach Abhängigkeiten + Boost/Aufwand. **Jede Phase bekommt ihre eigene Spec + Plan + Implementierung.**

| Phase | Inhalt (Punkt) | Größe | Liefert für spätere Phasen |
|-------|----------------|-------|----------------------------|
| **A** | Section-Occlusion-Skip (#1-Rest) | Klein | `SectionRenderMeta` (empty/opaque-full/face-solid) |
| **B** | AO korrekt berechnen (#10) | Klein | Korrekte Vertex-AO im 8-Byte-Format |
| **C** | Job-Priorisierung härten (#4-Rest) | Mittel | Prioritäts-Queue für Mesh/Upload |
| **D** | Chunk-LOD (#8) | Groß | LOD-Mesher + Distanz-Auswahl + Far-Impostors |
| **E** | Occlusion Culling (#9) | Mittel-groß | Konnektivitäts-/Software-Occlusion, nutzt A-Flags |
| **F** | GPU-Driven Culling (#7) | Groß | Frustum/Occlusion/LOD auf GPU (Compute) |
| **G** | Bindless / Descriptor Indexing (#6) | Mittel | Vorbereitung für #2 (Material-Vielfalt) |
| **H** | Erweiterte Meshing-Strategie (#2) | Sehr groß | Greedy-Opt bzw. Transvoxel/Dual-Contouring |

## 3. Abhängigkeiten

- **D (LOD)** und **E (Occlusion)** konsumieren die `SectionRenderMeta`-Flags aus **A**.
- **F (GPU-Culling)** setzt **C** (Datenfluss) und idealerweise **D/E** (Culling-Logik existiert CPU-seitig, wird auf GPU portiert) voraus.
- **G** ist Voraussetzung für **H** (mehr Materialien/Texturen sinnvoll handhabbar).
- **B** ist unabhängig, früh gezogen wegen großem optischen Gewinn bei kleinem Risiko.

## 4. Detail-Specs pro Phase

- Phase A: `2026-06-04-phaseA-section-occlusion-skip-design.md` (fertig)
- Phase B: `2026-06-04-phaseB-vertex-ao-light-design.md` (ready for review)
- Phasen C–H: jeweils eigene Spec, sobald die vorige Phase implementiert/abgenommen ist.

## 5. Querschnitt-Entscheidungen

- **Korrektheit vor Optimum:** Bei fehlenden Nachbarn/ungeladenen Chunks immer konservativ rendern (kein Loch am Streaming-Rand).
- **Snapshot-Vertrag bleibt unangetastet** (§5 Haupt-Spec): Culling/LOD-Auswahl füllt nur die `DrawSection`-Listen; keine `ChunkStore`-Pointer im Snapshot.
- **Origin-Rebase ohne Remesh** bleibt erhalten (§9): LOD-Meshes sind ebenfalls section-/chunk-lokal + `render_origin`.
- **Observability:** Jede Phase liefert mind. einen Zähler/Timer fürs ImGui-Overlay, um den Gewinn messbar zu machen.
