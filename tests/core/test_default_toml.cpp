#include <catch2/catch_test_macros.hpp>

#include "engine/core/EngineConfig.hpp"
#include "engine/core/HardwareProbe.hpp"

#include <string>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

TEST_CASE("default.toml loads end-to-end") {
    engine::EngineConfig cfg;
    const std::string path = std::string(ENGINE_SOURCE_DIR) + "/assets/default.toml";
    REQUIRE_NOTHROW(cfg.load_toml(path));
    cfg.finalize_cpu(engine::HardwareProbe::run_cpu());
    REQUIRE(cfg.world().sea_level == 64);
    REQUIRE(cfg.memory().gpu_mesh_vram == 0);
}
