set(ENGINE_CORE_SOURCES core/Log.cpp)

if(WIN32)
    list(APPEND ENGINE_CORE_SOURCES core/CrashHandlerWin32.cpp)
endif()

add_library(engine_core STATIC ${ENGINE_CORE_SOURCES})
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC spdlog::spdlog glm::glm)

if(WIN32)
    target_link_libraries(engine_core PUBLIC dbghelp)
endif()
