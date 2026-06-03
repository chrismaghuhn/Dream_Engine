#define MINIAUDIO_IMPLEMENTATION
#include "engine/audio/AudioEngine.hpp"

#include "engine/world/WorldEvents.hpp"

#include <spdlog/spdlog.h>

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace engine {

namespace {

constexpr float kMasterVolume = 1.f;
constexpr float kSfxBusVolume = 0.85f;

struct BeepBuffer {
    ma_audio_buffer buffer{};
    std::vector<std::uint8_t> pcm{};
};

[[nodiscard]] std::unique_ptr<BeepBuffer> make_beep_buffer(
    const ma_format format,
    const std::uint32_t channels,
    const std::uint32_t sample_rate,
    const float frequency_hz,
    const float duration_sec) {
    const std::uint32_t frame_count = static_cast<std::uint32_t>(
        (std::max)(duration_sec, 0.01f) * static_cast<float>(sample_rate));
    auto beep = std::make_unique<BeepBuffer>();
    beep->pcm.resize(static_cast<size_t>(frame_count) * channels * ma_get_bytes_per_frame(format, channels));

    float* samples = reinterpret_cast<float*>(beep->pcm.data());
    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(sample_rate);
        const float envelope = (std::min)(1.f, (std::min)(t * 40.f, (duration_sec - t) * 40.f));
        const float sample = std::sin(2.f * 3.14159265f * frequency_hz * t) * envelope * 0.35f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            samples[frame * channels + channel] = sample;
        }
    }

    const ma_audio_buffer_config config = ma_audio_buffer_config_init(
        format, channels, frame_count, beep->pcm.data(), nullptr);
    if (ma_audio_buffer_init(&config, &beep->buffer) != MA_SUCCESS) {
        return nullptr;
    }

    return beep;
}

} // namespace

struct AudioEngine::Impl {
    ma_engine engine{};
    ma_sound_group master_group{};
    ma_sound_group sfx_group{};
    std::vector<std::unique_ptr<BeepBuffer>> retained_buffers{};
    std::vector<ma_sound> active_sounds{};
};

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init(flecs::world& world, ChunkStore& store, const EngineConfig& config) {
    store_ = &store;
    occlusion_.init(store_, config.occlusion_grid_radius_chunks());

    impl_ = std::make_unique<Impl>();
    const ma_engine_config engine_config = ma_engine_config_init();
    if (ma_engine_init(&engine_config, &impl_->engine) != MA_SUCCESS) {
        impl_.reset();
        SPDLOG_WARN("AudioEngine: miniaudio device unavailable — running without audio output");
        register_observers(world);
        return true;
    }

    ma_sound_group_config master_config = ma_sound_group_config_init_2(&impl_->engine);
    if (ma_sound_group_init_ex(&impl_->engine, &master_config, &impl_->master_group) != MA_SUCCESS) {
        ma_engine_uninit(&impl_->engine);
        impl_.reset();
        SPDLOG_WARN("AudioEngine: failed to create master bus — running without audio output");
        register_observers(world);
        return true;
    }
    ma_sound_group_set_volume(&impl_->master_group, kMasterVolume);

    ma_sound_group_config sfx_config = ma_sound_group_config_init_2(&impl_->engine);
    sfx_config.pInitialAttachment = &impl_->master_group;
    if (ma_sound_group_init_ex(&impl_->engine, &sfx_config, &impl_->sfx_group) != MA_SUCCESS) {
        ma_sound_group_uninit(&impl_->master_group);
        ma_engine_uninit(&impl_->engine);
        impl_.reset();
        SPDLOG_WARN("AudioEngine: failed to create SFX bus — running without audio output");
        register_observers(world);
        return true;
    }
    ma_sound_group_set_volume(&impl_->sfx_group, kSfxBusVolume);

    active_ = true;
    register_observers(world);

    SPDLOG_INFO(
        "AudioEngine init: master + SFX buses, occlusion radius {} chunks",
        config.occlusion_grid_radius_chunks());
    return true;
}

void AudioEngine::shutdown() {
    if (impl_ != nullptr) {
        for (ma_sound& sound : impl_->active_sounds) {
            ma_sound_uninit(&sound);
        }
        impl_->active_sounds.clear();
        impl_->retained_buffers.clear();
        ma_sound_group_uninit(&impl_->sfx_group);
        ma_sound_group_uninit(&impl_->master_group);
        ma_engine_uninit(&impl_->engine);
        impl_.reset();
    }
    active_ = false;
    store_ = nullptr;
}

void AudioEngine::update_listener(
    const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    listener_position_ = position;
    occlusion_.set_listener_origin(glm::ivec3(glm::floor(position)));

    if (!active_ || impl_ == nullptr) {
        return;
    }

    ma_engine_listener_set_position(&impl_->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&impl_->engine, 0, up.x, up.y, up.z);
}

void AudioEngine::tick() {
    occlusion_.process_pending();
}

void AudioEngine::register_observers(flecs::world& world) {
    world.observer<EvtBlockBroken>()
        .event<EvtBlockBroken>()
        .with<ChunkCoord>()
        .each([this](flecs::iter&, size_t, EvtBlockBroken& evt) {
            const glm::ivec3 world_blocks = glm::ivec3(evt.coord) * 32 + evt.block_local;
            play_sfx_at(glm::vec3(world_blocks) + glm::vec3(0.5f), 220.f, 0.08f, 0.9f);
        });

    world.observer<EvtBlockPlaced>()
        .event<EvtBlockPlaced>()
        .with<ChunkCoord>()
        .each([this](flecs::iter&, size_t, EvtBlockPlaced& evt) {
            const glm::ivec3 world_blocks = glm::ivec3(evt.coord) * 32 + evt.block_local;
            play_sfx_at(glm::vec3(world_blocks) + glm::vec3(0.5f), 440.f, 0.06f, 0.75f);
        });

    world.observer<EvtPlayerFootstep>()
        .event<EvtPlayerFootstep>()
        .with<WorldRoot>()
        .each([this](flecs::iter&, size_t, EvtPlayerFootstep& evt) {
            play_sfx_at(evt.world_position, 120.f, 0.05f, 0.55f);
        });

    world.observer<ChunkDirty>()
        .event(flecs::OnAdd)
        .each([this](flecs::entity entity, ChunkDirty) {
            const ChunkCoord* coord = entity.get<ChunkCoord>();
            if (coord == nullptr) {
                return;
            }
            occlusion_.queue_chunk_dirty(*coord);
        });
}

void AudioEngine::play_sfx_at(
    const glm::vec3 world_position,
    const float frequency_hz,
    const float duration_sec,
    const float volume) {
    const float occlusion = occlusion_.occlusion_factor(listener_position_, world_position);
    const float occluded_volume = volume * (1.f - occlusion * 0.75f);

    if (!active_ || impl_ == nullptr) {
        SPDLOG_DEBUG(
            "AudioEngine stub SFX {:.0f} Hz at ({:.1f},{:.1f},{:.1f}) vol={:.2f} occ={:.2f}",
            frequency_hz,
            world_position.x,
            world_position.y,
            world_position.z,
            occluded_volume,
            occlusion);
        return;
    }

    const std::uint32_t sample_rate = ma_engine_get_sample_rate(&impl_->engine);
    std::unique_ptr<BeepBuffer> beep =
        make_beep_buffer(ma_format_f32, 1, sample_rate, frequency_hz, duration_sec);
    if (beep == nullptr) {
        SPDLOG_WARN("AudioEngine: failed to synthesize SFX buffer");
        return;
    }

    ma_sound sound{};
    ma_sound_config sound_config = ma_sound_config_init_2(&impl_->engine);
    sound_config.pDataSource = &beep->buffer;
    sound_config.pInitialAttachment = &impl_->sfx_group;
    if (ma_sound_init_ex(&impl_->engine, &sound_config, &sound) != MA_SUCCESS) {
        ma_audio_buffer_uninit(&beep->buffer);
        SPDLOG_WARN("AudioEngine: failed to play SFX");
        return;
    }

    ma_sound_set_volume(&sound, occluded_volume);
    ma_sound_set_spatialization_enabled(&sound, MA_TRUE);
    ma_sound_set_position(&sound, world_position.x, world_position.y, world_position.z);
    ma_sound_set_min_distance(&sound, 1.f);
    ma_sound_set_max_distance(&sound, 48.f);
    ma_sound_start(&sound);

    impl_->retained_buffers.push_back(std::move(beep));
    impl_->active_sounds.push_back(sound);
}

} // namespace engine
