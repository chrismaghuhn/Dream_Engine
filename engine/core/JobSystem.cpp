#include "engine/core/JobSystem.hpp"

#include "TaskScheduler.h"

#include <algorithm>

namespace engine {
namespace {

class LambdaTaskSet final : public enki::ITaskSet {
public:
    explicit LambdaTaskSet(std::function<void()> fn)
        : ITaskSet(1)
        , fn_(std::move(fn)) {}

    void ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/) override {
        if (fn_) {
            fn_();
        }
    }

private:
    std::function<void()> fn_;
};

void init_scheduler(enki::TaskScheduler& scheduler, int thread_count) {
    enki::TaskSchedulerConfig config{};
    config.numTaskThreadsToCreate = static_cast<uint32_t>(std::max(1, thread_count));
    scheduler.Initialize(config);
}

} // namespace

JobSystem::JobSystem() = default;

JobSystem::~JobSystem() {
    shutdown();
}

void JobSystem::init(const ThreadConfig& threads) {
    if (initialized_) {
        return;
    }

    worker_ = std::make_unique<enki::TaskScheduler>();
    io_ = std::make_unique<enki::TaskScheduler>();
    meshing_ = std::make_unique<enki::TaskScheduler>();

    init_scheduler(*worker_, threads.worker_threads);
    init_scheduler(*io_, threads.io_threads);
    init_scheduler(*meshing_, threads.meshing_threads);

    initialized_ = true;
}

void JobSystem::shutdown() {
    if (!initialized_) {
        return;
    }

    wait_all();
    meshing_.reset();
    io_.reset();
    worker_.reset();
    initialized_ = false;
}

void JobSystem::run_worker(std::function<void()> fn) {
    auto task = std::make_unique<LambdaTaskSet>(std::move(fn));
    const enki::ITaskSet* raw = task.get();
    pending_.push_back(std::move(task));
    worker_->AddTaskSetToPipe(const_cast<enki::ITaskSet*>(raw));
}

void JobSystem::run_io(std::function<void()> fn) {
    auto task = std::make_unique<LambdaTaskSet>(std::move(fn));
    const enki::ITaskSet* raw = task.get();
    pending_.push_back(std::move(task));
    io_->AddTaskSetToPipe(const_cast<enki::ITaskSet*>(raw));
}

void JobSystem::run_meshing(std::function<void()> fn) {
    auto task = std::make_unique<LambdaTaskSet>(std::move(fn));
    const enki::ITaskSet* raw = task.get();
    pending_.push_back(std::move(task));
    meshing_->AddTaskSetToPipe(const_cast<enki::ITaskSet*>(raw));
}

void JobSystem::wait_all() {
    if (worker_) {
        worker_->WaitforAll();
    }
    if (io_) {
        io_->WaitforAll();
    }
    if (meshing_) {
        meshing_->WaitforAll();
    }
    pending_.clear();
}

} // namespace engine
