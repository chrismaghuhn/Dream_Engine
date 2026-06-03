include(FetchContent)

# --- P0 dependencies ---

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.11.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

# --- Later milestones (not fetched in P0) ---
# FetchContent_Declare(flecs  GIT_REPOSITORY https://github.com/SanderMertens/flecs.git  GIT_TAG v4.0.0)
# FetchContent_Declare(glfw   GIT_REPOSITORY https://github.com/glfw/glfw.git           GIT_TAG 3.4)
# FetchContent_Declare(volk   GIT_REPOSITORY https://github.com/zeux/volk.git           GIT_TAG sdk-1.4.321)
