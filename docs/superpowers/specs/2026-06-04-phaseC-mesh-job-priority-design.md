# Phase C — Mesh Job Priority — Design

**Status:** Implemented (2026-06-04)
**Roadmap:** `2026-06-04-rendering-performance-roadmap-design.md` (Phase C)

## Ziel

Nahe Sections bekommen Mesh-Jobs, GPU-Slots und Uploads **vor** entfernten — unabhängig von Chunk-Index-Reihenfolge (0..7).

## Ist (vor C)

- `process_mesh_backlog`: Chunks nach Distanz sortiert, dann Sections 0→7 → ferne Chunk-Section-0 kann vor naher Chunk-Section-7 laufen, wenn Budget zwischen Chunks verteilt ist.
- GPU-Alloc / Upload: nur **Chunk-Mitte**-Distanz, alle 8 Sections eines Chunks gleich priorisiert.

## Soll

1. **Einheitliche Metrik:** `section_mesh_distance_sq(coord, section_index, focus)` in `SectionIndexing.hpp`.
2. **Mesh-Backlog:** Alle pending Sections sammeln → sortieren (Distanz, Tie-break `needs_remesh`) → bis `kMaxPendingMeshJobs` schedulen.
3. **GPU-Alloc + Upload:** Kandidaten mit Section-Distanz sortieren (bestehende per-Frame Caps unverändert).

## Nicht in Scope

- enkiTS Task-Prioritäten pro Thread-Pool
- Cross-Queue-Priorität (Collision/Light/Persist) — bleibt Haupt-Spec §16
- Frustum-basierte Mesh-Priorität (kommt ggf. mit Phase E)

## Tests

- `section_mesh_distance_sq` Unit-Tests (Indexing)
- Bestehende Streaming-/Mesh-Tests bleiben grün