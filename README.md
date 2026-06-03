# VoxelEngine

## Build (MSVC)

Requires the [Vulkan SDK](https://vulkan.lunarg.com/) with `glslc` on `PATH` or `VULKAN_SDK` set (CMake fails configure if `glslc` is missing).

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Run tests:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Run the game:

```powershell
.\build\game\Debug\VoxelEngine.exe
```

Engine defaults live in `assets/default.toml` (loaded by `EngineConfig` in M0-4). Logs are written to `%LOCALAPPDATA%/VoxelEngine/logs/engine.log`; crash minidumps go to `%LOCALAPPDATA%/VoxelEngine/crashes/`.