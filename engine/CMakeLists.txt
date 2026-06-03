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
    world/StreamingConfig.cpp
    world/StreamingSystem.cpp
    world/GreedyMesher.cpp
    render/ThinTerrainPreview.cpp
    gameplay/Camera.cpp
    gameplay/CameraSystem.cpp
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
    render/TerrainPass.cpp
    render/PerFrameGpuWrites.cpp
    render/HostMemory.cpp
    render/StagingRing.cpp
    render/Renderer.cpp
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
)
target_compile_definitions(engine_render PUBLIC
    ENGINE_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
)
add_dependencies(engine_render shaders_spv)
target_include_directories(engine_render PUBLIC ${VulkanMemoryAllocator_SOURCE_DIR}/include)

add_library(engine_core STATIC ${ENGINE_CORE_SOURCES})
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC
    spdlog::spdlog
    glm::glm
    tomlplusplus::tomlplusplus
    engine_platform
    engine_render
    flecs_static
    enkiTS
)
if(DEFINED enkits_SOURCE_DIR)
    target_include_directories(engine_core PUBLIC ${enkits_SOURCE_DIR}/src)
endif()

if(WIN32)
    target_link_libraries(engine_core PUBLIC dbghelp)
endif()
