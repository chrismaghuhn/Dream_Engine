#pragma once

#include "engine/core/Log.hpp"

#include <chrono>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace engine {

class FrameTimer {
public:
    static constexpr double kTargetFrameMs = 1000.0 / 60.0;

    void begin_frame() {
        stages_.clear();
        frame_start_ = Clock::now();
        stage_start_ = frame_start_;
        current_stage_.clear();
    }

    void begin_stage(const char* name) {
        end_stage();
        current_stage_ = name ? name : "";
        stage_start_ = Clock::now();
    }

    void end_stage() {
        if (current_stage_.empty()) {
            return;
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - stage_start_);
        stages_.push_back({current_stage_, elapsed.count()});
        current_stage_.clear();
    }

    void end_frame() {
        end_stage();
        const auto total_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - frame_start_).count();

        auto logger = module_logger("frame");
        for (const auto& stage : stages_) {
            logger->debug("{}: {:.3f} ms", stage.name, stage.duration_ms);
        }
        logger->debug("total: {:.3f} ms", total_ms);

        if (total_ms > kTargetFrameMs) {
            logger->warn("frame exceeded {:.1f} ms budget ({:.3f} ms)", kTargetFrameMs, total_ms);
        }
    }

    [[nodiscard]] double last_frame_ms() const {
        double total = 0.0;
        for (const auto& stage : stages_) {
            total += stage.duration_ms;
        }
        return total;
    }

private:
    using Clock = std::chrono::steady_clock;

    struct StageRecord {
        std::string name;
        double duration_ms = 0.0;
    };

    Clock::time_point frame_start_{};
    Clock::time_point stage_start_{};
    std::string current_stage_;
    std::vector<StageRecord> stages_;
};

} // namespace engine
