add_library(engine_core STATIC core/Log.cpp)
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC spdlog::spdlog glm::glm)
