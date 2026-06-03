#include "engine/core/CrashHandlerWin32.hpp"
#include "engine/core/FrameTimer.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/SimClock.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <spdlog/spdlog.h>

int main() {
    engine::crash_handler_install();
    engine::log_init();
    SPDLOG_INFO("VoxelEngine starting");

    engine::SimClock sim;
    engine::FrameTimer frame_timer;

    const auto run_start = std::chrono::steady_clock::now();
    auto prev = run_start;
    std::uint32_t total_ticks = 0;

    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count() < 2.0) {
        frame_timer.begin_frame();

        frame_timer.begin_stage("poll");
        const auto now = std::chrono::steady_clock::now();
        const double frame_delta = std::chrono::duration<double>(now - prev).count();
        prev = now;
        frame_timer.end_stage();

        sim.advance(frame_delta);

        frame_timer.begin_stage("sim");
        total_ticks += sim.step([] {});
        frame_timer.end_stage();

        frame_timer.end_frame();
    }

    SPDLOG_INFO("Sim ticks over ~2s wall time: {} (expected ~120 @ 60 Hz)", total_ticks);

    const double expected = 120.0;
    const double tolerance = 10.0;
    if (std::abs(static_cast<double>(total_ticks) - expected) > tolerance) {
        SPDLOG_WARN("Sim tick count outside expected range [{:.0f}, {:.0f}]",
                    expected - tolerance,
                    expected + tolerance);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
