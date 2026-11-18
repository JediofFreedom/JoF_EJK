#ifndef VK_RUNTIME_PROFILE_H
#define VK_RUNTIME_PROFILE_H
#include "vk_load_profile.h"
#include <array>
#include <atomic>
#include <thread>

enum class RuntimeStage {
    Scene, World, GhoulFrontend, Skeleton, Backend, GpuWait, Acquire, Present,
    Bolt, BoneTransform, ShaderIterator, Visibility, Count
};

// Native Vulkan currently builds and executes render commands on the owning
// engine thread. Ignore background callers explicitly; CPU texture workers have
// separate loading counters. Reset never changes ownership or logs per frame.
class RuntimeProfileCounters {
public:
    struct Counter { uint64_t calls = 0, microseconds = 0; };
    struct Mark { uint64_t started = 0, epoch = 0; bool active = false; };
    using Clock = uint64_t (*)();
private:
    std::array<Counter, static_cast<size_t>(RuntimeStage::Count)> counters;
    std::atomic<unsigned> level{0};
    const std::thread::id owner = std::this_thread::get_id();
    Clock clock;
    uint64_t epoch = 0, started = 0, frames = 0;
public:
    explicit RuntimeProfileCounters(Clock source = ImageTimeUs) : clock(source) { reset(); }
    void setLevel(unsigned value) { level.store(value, std::memory_order_relaxed); }
    unsigned getLevel() const { return level.load(std::memory_order_relaxed); }
    bool ownsThread() const { return owner == std::this_thread::get_id(); }
    void reset() { if (!ownsThread()) return; counters = {}; frames = 0; ++epoch; started = clock(); }
    Mark begin(unsigned minimum = 1) const {
        if (getLevel() < minimum || !ownsThread()) return {};
        Mark result; result.started = clock(); result.epoch = epoch; result.active = true; return result;
    }
    void end(RuntimeStage stage, Mark mark) {
        if (!mark.active || !ownsThread() || mark.epoch != epoch) return;
        auto &counter = counters[static_cast<size_t>(stage)];
        ++counter.calls; counter.microseconds += clock() - mark.started;
    }
    void frame() { if (getLevel() && ownsThread()) ++frames; }
    uint64_t frameCount() const { return frames; }
    uint64_t elapsed() const { return clock() - started; }
    const Counter &get(RuntimeStage stage) const { return counters[static_cast<size_t>(stage)]; }
};

extern RuntimeProfileCounters vkRuntimeProfile;
class RuntimeProfileScope {
    RuntimeStage stage;
    RuntimeProfileCounters::Mark mark;
public:
    explicit RuntimeProfileScope(RuntimeStage value, unsigned minimum = 1) : stage(value), mark(vkRuntimeProfile.begin(minimum)) {}
    ~RuntimeProfileScope() { vkRuntimeProfile.end(stage, mark); }
};
void R_InitRuntimeProfile();
void R_UpdateRuntimeProfile();
void R_ResetRuntimeProfile();
void R_ReportRuntimeProfile();
void R_ShutdownRuntimeProfile();
#endif
