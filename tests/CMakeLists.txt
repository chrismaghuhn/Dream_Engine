add_executable(engine_tests
    core/test_smoke.cpp
    core/test_math.cpp
    core/test_engine_config.cpp
    world/test_indexing.cpp
)
target_link_libraries(engine_tests PRIVATE engine_core Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(engine_tests)
