#pragma once

#include "engine/audio/OcclusionGrid.hpp"
#include "engine/core/EngineConfig.hpp"
#include "engine/world/ChunkStore.hpp"

#include <flecs.h>
#include <glm/glm.hpp>

#include <memory>

namespace engine {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] bool init(flecs::world& world, ChunkStore& store, const EngineConfig& config);
    void shutdown();

    void update_listener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
    void tick();

    [[nodiscard]] bool is_active() const { return active_; }
    [[nodiscard]] const OcclusionGrid& occlusion_grid() const { return occlusion_; }

private:
    struct Impl;

    void register_observers(flecs::world& world);
    void play_sfx_at(glm::vec3 world_position, float frequency_hz, float duration_sec, float volume);

    std::unique_ptr<Impl> impl_;
    ChunkStore* store_ = nullptr;
    OcclusionGrid occlusion_{};
    bool active_ = false;
    glm::vec3 listener_position_{0.f};
};

} // namespace engine
