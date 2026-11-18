// Optional frontend asset-loading diagnostics. Never poll or journal events.
#pragma once

#include <chrono>
#include <cstdint>

namespace SoundLoadProfile {
enum Stage { Source, InspectMP3, StreamMP3, DecodeMP3, Resample, OpenAL, Total, Count };
struct Counter { uint64_t calls, nanoseconds; };
struct State {
    cvar_t *enabled;
    Counter counters[Count];
    uint64_t epoch;
    uint64_t (*clock)();
};
inline State &Get() { static State state = {}; return state; }
inline uint64_t Now() {
    if (Get().clock) return Get().clock();
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void Reset() {
    State &state = Get();
    for (auto &counter : state.counters) counter = Counter();
    ++state.epoch;
}
class Scope {
    Stage stage;
    uint64_t started, epoch;
    bool active;
public:
    explicit Scope(Stage value) : stage(value), started(0), epoch(Get().epoch),
        active(Get().enabled && Get().enabled->integer) {
        if (active) started = Now();
    }
    ~Scope() {
        if (!active || epoch != Get().epoch) return;
        Counter &counter = Get().counters[stage];
        ++counter.calls;
        counter.nanoseconds += Now() - started;
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};
void Report();
}
