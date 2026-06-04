set(ENGINE_CORE_SOURCES
    core/Log.cpp
    core/HardwareProbe.cpp
    core/EngineConfig.cpp
    core/JobSystem.cpp
    world/WorldModule.cpp
    world/OriginRebase.cpp
    world/ChunkStore.cpp
    world/ChunkLifecycle.cpp
    procgen/HeightmapWorldgen.cpp
    procgen/TerrainGraph.cpp
    world/StreamingConfig.cpp
    world/StreamingSystem.cpp
    world/BlockLight.cpp
    world/GreedyMesher.cpp
    world/TerrainLod.cpp
    world/ChunkLodMesher.cpp
    world/ChunkImpostor.cpp
    render/ThinTerrainPreview.cpp
    render/StreamingTerrainSystem.cpp
    render/SectionVisibility.cpp
    gameplay/Camera.cpp
    gameplay/CameraSystem.cpp
    gameplay/PlayerMotor.cpp
    gameplay/PlayerSpawnReadyGate.cpp
    gameplay/BlockInteraction.cpp
    persist/AtomicFile.cpp
    persist/PlayerDat.cpp
    persist/ChunkBinCodec.cpp
    persist/WorldMeta.cpp
    persist/Quarantine.cpp
    persist/MinimalSaveBackend.cpp
    persist/RegionFileSaveBackend.cpp
    Engine.cpp
)

if(WIN32)
    list(APPEND ENGINE_CORE_SOURCES core/CrashHandlerWin32.cpp)
endif()

add_library(engine_platform STATIC platform/Platform.cpp platform/Input.cpp)
target_include_directories(engine_platform PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_platform PUBLIC glfw)

set(ENGINE_RENDER_SOURCES
    render/VulkanContext.cpp
    render/VulkanDebug.cpp
    render/SnapshotRing.cpp
    render/GpuDeferredFreeQueue.cpp
    render/GpuMeshPool.cpp
    render/MeshUploadQueue.cpp
    render/ShaderManager.cpp
    render/TerrainPass.cpp
    render/TerrainImpostorPass.cpp
    render/BlockTextureArray.cpp
    render/SkyPass.cpp
    render/WaterPass.cpp
    render/PerFrameGpuWrites.cpp
    render/HostMemory.cpp
    render/StagingRing.cpp
    render/Renderer.cpp
    ui/UiHost.cpp
)

add_library(engine_render STATIC ${ENGINE_RENDER_SOURCES})
target_include_directories(engine_render PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(engine_render PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(engine_render PUBLIC
    engine_platform
    spdlog::spdlog
    glm::glm
    volk
    Vulkan::Vulkan
    imgui_lib
    stb_image
)
target_compile_definitions(engine_render PUBLIC
    ENGINE_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    ENGINE_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/shaders"
    ENGINE_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
)
add_dependencies(engine_render shaders_spv)
target_include_directories(engine_render PUBLIC ${VulkanMemoryAllocator_SOURCE_DIR}/include)

set(ENGINE_PHYSICS_SOURCES
    physics/PhysicsSystem.cpp
    physics/DebrisSystem.cpp
    physics/VoxelCapsuleResolver.cpp
    physics/ObjectLayerMatrix.cpp
    physics/CollisionRemeshQueue.cpp
)

add_library(engine_physics STATIC ${ENGINE_PHYSICS_SOURCES})
target_include_directories(engine_physics PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_physics PUBLIC glm::glm spdlog::spdlog flecs_static)
if(ENGINE_JOLT)
    if(TARGET Jolt)
        target_link_libraries(engine_physics PUBLIC Jolt)
        target_compile_definitions(engine_physics PUBLIC ENGINE_HAS_JOLT=1)
    else()
        message(STATUS "engine_physics: Jolt BLOCKED (target not built; voxel resolver only)")
    endif()
endif()

set(ENGINE_AUDIO_SOURCES
    audio/AudioEngine.cpp
    audio/OcclusionGrid.cpp
)

add_library(engine_audio STATIC ${ENGINE_AUDIO_SOURCES})
target_include_directories(engine_audio PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_audio PUBLIC
    glm::glm
    spdlog::spdlog
    flecs_static
    miniaudio
)

add_subdirectory(${CMAKE_SOURCE_DIR}/engine/character ${CMAKE_BINARY_DIR}/engine/character)

add_library(engine_core STATIC ${ENGINE_CORE_SOURCES})
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(engine_core PUBLIC ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(engine_core PUBLIC
    spdlog::spdlog
    glm::glm
    tomlplusplus::tomlplusplus
    engine_platform
    engine_render
    engine_physics
    engine_audio
    flecs_static
    enkiTS
    libzstd_static
    FastNoise
)
if(DEFINED enkits_SOURCE_DIR)
    target_include_directories(engine_core PUBLIC ${enkits_SOURCE_DIR}/src)
endif()

if(WIN32)
    target_link_libraries(engine_core PUBLIC dbghelp)
endif()
