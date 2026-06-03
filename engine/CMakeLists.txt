set(ENGINE_CORE_SOURCES
    core/Log.cpp
    core/HardwareProbe.cpp
    core/EngineConfig.cpp
    core/JobSystem.cpp
    Engine.cpp
)

if(WIN32)
    list(APPEND ENGINE_CORE_SOURCES core/CrashHandlerWin32.cpp)
endif()

add_library(engine_platform STATIC platform/Platform.cpp)
target_include_directories(engine_platform PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_platform PUBLIC glfw)

add_library(engine_core STATIC ${ENGINE_CORE_SOURCES})
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC
    spdlog::spdlog
    glm::glm
    tomlplusplus::tomlplusplus
    engine_platform
    flecs_static
    enkiTS
)
if(DEFINED enkits_SOURCE_DIR)
    target_include_directories(engine_core PUBLIC ${enkits_SOURCE_DIR}/src)
endif()

if(WIN32)
    target_link_libraries(engine_core PUBLIC dbghelp)
endif()
