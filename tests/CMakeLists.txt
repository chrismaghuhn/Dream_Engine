add_executable(engine_tests
    core/test_smoke.cpp
    core/test_math.cpp
    core/test_sim_clock.cpp
    core/test_engine_config.cpp
    core/test_default_toml.cpp
    core/test_job_system.cpp
    world/test_indexing.cpp
    world/test_world_config.cpp
    world/test_world_events.cpp
    world/test_origin_rebase.cpp
    render/test_snapshot_ring.cpp
)
target_link_libraries(engine_tests PRIVATE engine_core Catch2::Catch2WithMain)
target_compile_definitions(engine_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
include(Catch)
catch_discover_tests(engine_tests)
