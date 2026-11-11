#ifndef VK_LOAD_PROFILE_H
#define VK_LOAD_PROFILE_H
#include <chrono>
#include <cstdint>

inline uint64_t ImageTimeUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
// All stage counters are main-thread elapsed time. A parent stage can exclude
// a nested submit/wait stage so the reporting does not count it twice.
class ImagePhaseTimer {
    uint64_t &counter;
    const uint64_t *excluded;
    uint64_t started, exclusionStarted;
public:
    explicit ImagePhaseTimer(uint64_t &destination, const uint64_t *nested = nullptr)
        : counter(destination), excluded(nested), started(ImageTimeUs()), exclusionStarted(nested ? *nested : 0) {}
    ~ImagePhaseTimer() {
        const uint64_t elapsed = ImageTimeUs() - started;
        const uint64_t nested = excluded ? *excluded - exclusionStarted : 0;
        counter += elapsed > nested ? elapsed - nested : 0;
    }
};
#endif
