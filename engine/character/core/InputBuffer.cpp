#include "engine/character/core/InputBuffer.hpp"

namespace engine::character {

void InputBuffer::push(BufferedInput::Kind kind, int ttl) {
    if (kind == BufferedInput::Kind::None || ttl <= 0) {
        return;
    }

    if (size_ == kCapacity) {
        head_ = (head_ + 1) % kCapacity;
        --size_;
    }

    const int tail = (head_ + size_) % kCapacity;
    entries_[tail] = BufferedInput{.kind = kind, .ttl_frames = ttl};
    ++size_;
}

BufferedInput::Kind InputBuffer::peek() const {
    if (size_ == 0) {
        return BufferedInput::Kind::None;
    }
    return entries_[head_].kind;
}

void InputBuffer::consume() {
    if (size_ == 0) {
        return;
    }
    head_ = (head_ + 1) % kCapacity;
    --size_;
}

void InputBuffer::tick() {
    int write = 0;
    for (int read = 0; read < size_; ++read) {
        const int read_idx = (head_ + read) % kCapacity;
        BufferedInput entry = entries_[read_idx];
        --entry.ttl_frames;
        if (entry.ttl_frames <= 0) {
            continue;
        }

        const int write_idx = (head_ + write) % kCapacity;
        entries_[write_idx] = entry;
        ++write;
    }
    size_ = write;
}

void InputBuffer::clear() {
    head_ = 0;
    size_ = 0;
}

} // namespace engine::character
