# VoxelEngine

C++ / Vulkan voxel survival prototype — infinite procedural terrain, break/place, walk physics, inventory, and region-based saves.

## Prerequisites

- **Visual Studio 2022** with the *Desktop development with C++* workload (MSVC toolset)
- **Vulkan SDK 1.4.x** — [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (volk is pinned to `vulkan-sdk-1.4.350.0`)
- **CMake 3.24+**

Set the `VULKAN_SDK` environment variable to your SDK install root (the Vulkan SDK installer does this automatically). CMake uses it via `find_package(Vulkan REQUIRED)`.

## Configure, build, and test

From a **Developer PowerShell for VS 2022**:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

Run the game:

```powershell
.\build\game\Release\VoxelEngine.exe
```

Optional Tracy profiling: add `-DENGINE_TRACY=ON` to the configure step.

## Default controls


| Action                   | Input                                     |
| ------------------------ | ----------------------------------------- |
| Move                     | `W` / `A` / `S` / `D`                     |
| Look                     | Mouse (captured when inventory is closed) |
| Jump (walk mode)         | `Space`                                   |
| Fly up / down (fly mode) | `Space` / `Ctrl`                          |
| Toggle walk / fly        | `F`                                       |
| Break block              | Left mouse or `Q`                         |
| Place block              | Right mouse or `E`                        |
| Hotbar slots             | `1`–`9`                                   |
| Inventory                | `I`                                       |
| Save world               | `F5`                                      |
| Load world               | `F9`                                      |


Walk mode is the default after spawn; press `F` to switch to fly navigation.

## Save location

Worlds are written under `<repo>/saves/<world_name>/` (default world name: `default`). A production save includes `world.meta`, `player.dat`, and region files `r.<rx>.<ry>.<rz>.vwr`. The `saves/` directory is gitignored.

## Documentation

- Design spec (v9): `[docs/superpowers/specs/2026-06-03-voxel-engine-design.md](docs/superpowers/specs/2026-06-03-voxel-engine-design.md)`
- Phase 1 implementation plan: `[docs/superpowers/plans/2026-06-03-voxel-engine-phase1.md](docs/superpowers/plans/2026-06-03-voxel-engine-phase1.md)`

