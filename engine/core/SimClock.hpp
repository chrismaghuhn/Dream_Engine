#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace engine {

class SimClock {
public:
    static constexpr double fixed_dt = 1.0 / 60.0;
    static constexpr double max_accumulator = 4.0 * fixed_dt;

    void advance(double frame_delta) {
        if (frame_delta > 0.0) {
            accumulator_ += frame_delta;
        }
        accumulator_ = std::min(accumulator_, max_accumulator);
    }

    template <typename TickFn>
    std::uint32_t step(TickFn&& tick_fn) {
        std::uint32_t ticks = 0;
        while (accumulator_ >= fixed_dt) {
            tick_fn();
            accumulator_ -= fixed_dt;
            ++ticks;
        }
        return ticks;
    }

    [[nodiscard]] double alpha() const {
        return accumulator_ / fixed_dt;
    }

    [[nodiscard]] double accumulator() const { return accumulator_; }

    void reset() { accumulator_ = 0.0; }

private:
    double accumulator_ = 0.0;
};

} // namespace engine
