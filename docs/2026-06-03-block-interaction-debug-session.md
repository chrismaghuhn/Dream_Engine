# Block Interaction Debug Session - 2026-06-03

Kurze Zusammenfassung der Debug-Session zu Abbauen, Platzieren, weissem Flash und fehlerhaftem Terrain-Streaming.

## Symptome

- Beim Abbauen oder Platzieren flashte der Screen kurz weiss.
- Abbauen und Platzieren funktionierten nur selten oder wirkten unzuverlaessig.
- Nachgeladene Weltbereiche wirkten flach und bestanden ueberwiegend aus Stein.
- Es bestand der Verdacht, dass Mausposition oder Mausklicks falsch ausgewertet werden.

## Befunde

- Die Maus-Buttons waren nicht die Hauptursache: Logs zeigten `break applied` und `place applied`.
- Der Raycast traf Bloecke und mutierte den `ChunkStore`.
- Dirty-Chunk-Events wurden nach Blockmutationen ausgeloest.
- Die Sichtbarkeit hing danach am Remesh-/GPU-Upload-/Snapshot-Pfad.
- Die flachen Steinbereiche kamen vom Streaming-Load-Budget: bei begrenztem Budget wurden zuerst niedrige Chunk-Y-Layer geladen, nicht die Player-Hoehe.

## Aenderungen

### Block Interaction

Datei: `engine/gameplay/BlockInteraction.cpp`

- Fehlende Chunk-Entities werden beim Dirty-Markieren lazy erstellt, wenn ein Chunk im Store existiert, aber keine Flecs-Entity mehr zugeordnet ist.
- Der Block-Raycast laeuft jetzt bis zur echten Reichweite statt mit einer zu kleinen Step-Grenze. Dadurch funktionieren diagonale Treffer innerhalb der Reichweite.
- Diagnose-Logs fuer Raycast, Break und Place bleiben erhalten, sind aber auf `SPDLOG_DEBUG` gesetzt.

### Terrain Streaming

Datei: `engine/world/StreamingSystem.cpp`

- Streaming priorisiert bei begrenztem Load-Budget jetzt Chunks nahe der Player-Y-Hoehe.
- Danach wird nach XZ-Distanz und deterministischen Tie-Breakern sortiert.
- Dadurch werden sichtbare/spielnahe Hoehenlayer zuerst geladen, statt tiefe Steinlayer zu bevorzugen.

### Terrain Remesh / Rendering

Datei: `engine/render/StreamingTerrainSystem.cpp`

- Beim Remesh bleiben bestaetigte stale GPU-Slots erhalten, solange noch kein aktiver Ersatzslot verfuegbar ist.
- Das verhindert sichtbare Luecken bzw. weisse Flash-Frames waehrend neue Meshes gebaut und hochgeladen werden.
- Diagnose-Logs fuer Dirty-Chunk, Mesh-Completion, Upload-Queue und Upload-Complete bleiben erhalten, sind aber auf `SPDLOG_DEBUG` gesetzt.

## Tests

Neue bzw. relevante Regressionstests:

- `tests/render/test_streaming_terrain_events.cpp`
  - `mesh completion keeps stale slot when no active replacement exists`
- `tests/world/test_streaming.cpp`
  - `update_streaming prioritizes player height layer when load budget is limited`
- `tests/gameplay/test_block_events.cpp`
  - `break_block_at creates missing chunk entity before marking dirty`
  - `raycast reaches diagonal block within reach`

Verifikation am Ende der Session:

```powershell
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Release --output-on-failure
```

Ergebnis: `112/112` Tests bestanden.

Release-Exe neu gebaut:

```text
C:\Users\chris\Documents\Engine\build\game\Release\VoxelEngine.exe
```

## Aktueller Stand

- Abbauen und Platzieren funktionieren im getesteten Build wieder.
- Diagnose-Logs sind nicht entfernt, sondern auf Debug-Level reduziert.
- Bei Bedarf kann in `assets/default.toml` das Log-Level auf `debug` gestellt werden, um die Block-/Remesh-Kette erneut zu verfolgen.
