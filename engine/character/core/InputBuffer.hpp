#pragma once

#include <array>

namespace engine::character {

struct BufferedInput {
    enum class Kind { None, Light, Heavy, Kick, Special, Dodge };
    Kind kind = Kind::None;
    int ttl_frames = 0;
};

class InputBuffer {
public:
    static constexpr int kCapacity = 10;
    static constexpr int kDefaultTTL = 10;

    void push(BufferedInput::Kind kind, int ttl = kDefaultTTL);
    [[nodiscard]] BufferedInput::Kind peek() const;
    void consume();
    void tick();
    void clear();

private:
    std::array<BufferedInput, kCapacity> entries_{};
    int head_ = 0;
    int size_ = 0;
};

} // namespace engine::character
