# Diagnostic CLI tools. These link only Vulkan-free core libraries so they can
# run headless (no GPU / window) for analysis and tuning workflows.

add_executable(anim_inspect anim_inspect/main.cpp)
target_link_libraries(anim_inspect PRIVATE engine_character_core)
target_compile_definitions(anim_inspect PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ENGINE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
