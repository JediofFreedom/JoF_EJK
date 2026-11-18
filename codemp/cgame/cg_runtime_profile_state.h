#ifndef EJK_CG_RUNTIME_PROFILE_STATE_H
#define EJK_CG_RUNTIME_PROFILE_STATE_H
#include "cg_runtime_profile.h"
#include <array>
#include <algorithm>
#include <thread>

namespace CgameProfile {
class Counters {
public:
    using Clock = uint64_t (*)();
    struct Counter { uint64_t calls = 0,nanoseconds = 0; };
private:
    Clock clock;
    std::array<Counter,CGP_COUNT> counters{};
    std::thread::id owner;
    bool bound = false,initializing = true,open = false,timed = false;
    uint64_t epoch = 0,first = 0,last = 0,completeFrames = 0,earlyFrames = 0;
    uint64_t populationSamples = 0,playerSum = 0,entitySum = 0;
    unsigned playerMin = 0,playerMax = 0,entityMax = 0;
    void clear() {
        counters = {}; open = timed = false; ++epoch;
        first = last = completeFrames = earlyFrames = populationSamples = playerSum = entitySum = 0;
        playerMin = playerMax = entityMax = 0;
    }
    bool valid(cgProfileMark_t mark) const {
        return mark.active && open && mark.epoch == epoch && owner == std::this_thread::get_id();
    }
public:
    explicit Counters(Clock source) : clock(source) {}
    // Init/shutdown occur at the engine's serialized lifecycle handoff, which
    // may run on its load worker. Never bind runtime ownership at DLL/init time.
    void initialize() { clear(); bound = false; owner = {}; initializing = true; }
    void ready() { initializing = false; }
    bool ownsThread() const { return !bound || owner == std::this_thread::get_id(); }
    void reset() { if (ownsThread()) clear(); }
    bool frameOpen() const { return open; }
    cgProfileMark_t beginFrame(bool enabled,bool eligible) {
        if (!enabled || !eligible || initializing || open) return {0,0,0};
        const auto current = std::this_thread::get_id();
        if (bound && owner != current) return {0,0,0};
        owner = current; bound = true; open = true;
        const uint64_t now = clock();
        if (!timed) { first = now; timed = true; }
        return {now,epoch,1};
    }
    cgProfileMark_t beginStage() const {
        if (!open || !ownsThread()) return {0,0,0};
        return {clock(),epoch,1};
    }
    void endStage(cgProfileStage_t stage,cgProfileMark_t mark) {
        if (!valid(mark)) return;
        auto &counter = counters[stage];
        ++counter.calls; counter.nanoseconds += clock()-mark.started;
    }
    void endFrame(cgProfileMark_t mark,bool complete,int players,int entities) {
        if (!valid(mark)) return;
        last = clock();
        ++counters[CGP_FRAME].calls; counters[CGP_FRAME].nanoseconds += last-mark.started;
        open = false;
        if (!complete) { ++earlyFrames; return; }
        ++completeFrames;
        if (players < 0 || entities < 0) return;
        if (!populationSamples) playerMin = unsigned(players);
        playerMin = std::min(playerMin,unsigned(players)); playerMax = std::max(playerMax,unsigned(players));
        entityMax = std::max(entityMax,unsigned(entities));
        playerSum += unsigned(players); entitySum += unsigned(entities); ++populationSamples;
    }
    const Counter &get(cgProfileStage_t stage) const { return counters[stage]; }
    uint64_t frames() const { return completeFrames; }
    uint64_t early() const { return earlyFrames; }
    uint64_t interval() const { return timed ? last-first : 0; }
    uint64_t samples() const { return populationSamples; }
    double meanPlayers() const { return populationSamples ? double(playerSum)/populationSamples : 0; }
    double meanEntities() const { return populationSamples ? double(entitySum)/populationSamples : 0; }
    unsigned minPlayers() const { return playerMin; }
    unsigned maxPlayers() const { return playerMax; }
    unsigned maxEntities() const { return entityMax; }
};
}
#endif
