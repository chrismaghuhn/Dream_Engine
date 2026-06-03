# Movement Foundation v1 — self-contained movement prototype.
#
# Layering:
#   engine_movement_core        : pure logic, NO Vulkan/GLFW/ImGui/flecs.
#   engine_movement_debug_render : Vulkan debug-line pass (links engine_render).
#   movement_test               : executable wiring the SimClock loop + platform.

set(ENGINE_MOVEMENT_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/movement/core/EntityWorld.cpp
    ${CMAKE_SOURCE_DIR}/engine/movement/core/MovementWorld.cpp
    ${CMAKE_SOURCE_DIR}/engine/movement/core/TextParser.cpp
    ${CMAKE_SOURCE_DIR}/engine/movement/core/ArenaLoader.cpp
    ${CMAKE_SOURCE_DIR}/engine/movement/core/SaveService.cpp
    ${CMAKE_SOURCE_DIR}/engine/movement/core/CollisionWorld.cpp
    ${CMAKE_SOURCE_DIR}/engine/movement/core/PlayerMovement.cpp
    ${CMAKE_SOURCE_DIR}/engine/movement/core/CameraSystem.cpp
)

add_library(engine_movement_core STATIC ${ENGINE_MOVEMENT_CORE_SOURCES})
target_include_directories(engine_movement_core PUBLIC ${CMAKE_SOURCE_DIR})
# SimClock.hpp is header-only (engine/core/SimClock.hpp); we only need the
# include path, NOT a link to engine_core (which transitively pulls Vulkan,
# GLFW, flecs, audio). Keep this target dependency-light on purpose.
target_link_libraries(engine_movement_core PUBLIC
    glm::glm
    spdlog::spdlog
)
target_compile_definitions(engine_movement_core PUBLIC
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Vulkan debug-render layer + executable are added once their sources exist.
if(EXISTS "${CMAKE_SOURCE_DIR}/engine/movement/debug_render/DebugDrawPass.cpp")
    add_library(engine_movement_debug_render STATIC
        ${CMAKE_SOURCE_DIR}/engine/movement/debug_render/DebugDrawPass.cpp
        ${CMAKE_SOURCE_DIR}/engine/movement/debug_render/ImGuiOverlay.cpp
    )
    target_include_directories(engine_movement_debug_render PUBLIC ${CMAKE_SOURCE_DIR})
    target_compile_definitions(engine_movement_debug_render PUBLIC VK_NO_PROTOTYPES)
    target_link_libraries(engine_movement_debug_render PUBLIC
        engine_movement_core
        engine_render
    )
    add_dependencies(engine_movement_debug_render shaders_spv)

    add_executable(movement_test
        ${CMAKE_CURRENT_SOURCE_DIR}/MovementApp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    )
    target_include_directories(movement_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(movement_test PRIVATE
        ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        VK_NO_PROTOTYPES
    )
    target_link_libraries(movement_test PRIVATE
        engine_movement_debug_render
        engine_movement_core
        engine_platform
        engine_render
        imgui_lib
        engine_character_render
        engine_character_core
    )
endif()
