#include <catch2/catch_test_macros.hpp>

#include "engine/core/EngineConfig.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/core/JobSystem.hpp"

#include <atomic>
#include <string>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

TEST_CASE("job pools run one task each") {
    const auto cpu = engine::HardwareProbe::run_cpu();
    engine::EngineConfig cfg;
    cfg.load_toml(std::string(ENGINE_SOURCE_DIR) + "/assets/default.toml");
    cfg.finalize_cpu(cpu);

    engine::JobSystem js;
    js.init(cfg.threads());

    std::atomic<int> worker_done{0};
    std::atomic<int> mesh_done{0};
    js.run_worker([&] { worker_done = 1; });
    js.run_meshing([&] { mesh_done = 1; });
    js.wait_all();

    REQUIRE(worker_done == 1);
    REQUIRE(mesh_done == 1);
    REQUIRE(cfg.threads().meshing_threads <= cfg.threads().worker_threads);
}
