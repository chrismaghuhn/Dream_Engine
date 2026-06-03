add_executable(VoxelEngine main.cpp)
target_link_libraries(VoxelEngine PRIVATE engine_core)
target_compile_definitions(VoxelEngine PRIVATE ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
