#pragma once

#include "engine/core/EngineConfig.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace enki {
class ITaskSet;
class TaskScheduler;
} // namespace enki

namespace engine {

class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void init(const ThreadConfig& threads);
    void shutdown();

    void run_worker(std::function<void()> fn);
    void run_io(std::function<void()> fn);
    void run_meshing(std::function<void()> fn);
    void wait_all();

private:
    std::unique_ptr<enki::TaskScheduler> worker_;
    std::unique_ptr<enki::TaskScheduler> io_;
    std::unique_ptr<enki::TaskScheduler> meshing_;
    std::vector<std::unique_ptr<enki::ITaskSet>> pending_;
    bool initialized_ = false;
};

} // namespace engine
