#pragma once

namespace engine {

class PhysicsSystem {
public:
    [[nodiscard]] bool init();
    void shutdown();

    [[nodiscard]] bool is_active() const { return active_; }

private:
    bool active_ = false;
};

} // namespace engine
