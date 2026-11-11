#include "cg_runtime_profile_state.h"
#include <chrono>

namespace {
uint64_t nowNs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
CgameProfile::Counters counters(nowNs);
cgProfileOutput_t print;
void report() {
    if (!print || !counters.get(CGP_FRAME).calls) return;
    static const char *names[] = {"whole active-frame call","deferred/input/setup","cvars/radial menu",
        "snapshots/scene reset","prediction","view/camera","sky portal","packet entities",
        "marks/local effects/trails","view weapon","scheduled FX","audio/refdef",
        "draw-active inclusive","renderer scene submission","HUD2D","automap","async/net-userinfo tail",
        "fine entity interpolation","fine entity automatic effects","fine player inclusive",
        "fine player transform/client setup","fine player model validation","fine player equipment/appearance",
        "fine player shadow/water/seeker","fine player pose/bone angles","fine player skeleton/hand bolt",
        "fine player attachments/view/trails","fine player animation/sounds","fine player sprites/force FX",
        "fine player saber","fine player submission/style","fine player holsters/cosmetics",
        "fine player weapon/powerup/effects tail","fine player spec-HUD trace",
        "fine shadow world trace","fine shadow impact mark","fine water point contents"};
    static_assert(sizeof(names)/sizeof(names[0]) == CGP_COUNT,"Cgame stage names must match counters");
    print("CGPROFILE cgame: %llu completed frames, %llu early returns, %.3f ms sampled interval; owning-thread elapsed, nested totals overlap; instrumentation changes timing\n",
        static_cast<unsigned long long>(counters.frames()),static_cast<unsigned long long>(counters.early()),counters.interval()/1000000.0);
    if (counters.samples())
        print("CGPROFILE snapshot population: %llu samples, players min/mean/max %u/%.2f/%u, entities mean/max %.2f/%u (snapshot presence, not screen visibility)\n",
            static_cast<unsigned long long>(counters.samples()),counters.minPlayers(),counters.meanPlayers(),counters.maxPlayers(),counters.meanEntities(),counters.maxEntities());
    for (int i = 0; i < CGP_COUNT; ++i) {
        const auto &counter = counters.get(static_cast<cgProfileStage_t>(i));
        if (!counter.calls) continue;
        print("CGPROFILE %s: %llu calls, %.3f ms total, %.6f ms/call, %.6f ms/completed-frame\n",names[i],
            static_cast<unsigned long long>(counter.calls),counter.nanoseconds/1000000.0,
            counter.nanoseconds/(1000000.0*counter.calls),counters.frames() ? counter.nanoseconds/(1000000.0*counters.frames()) : 0.0);
    }
}
}
extern "C" {
int cgRuntimeProfileActive;
void CG_RuntimeProfileInit(cgProfileOutput_t output) { cgRuntimeProfileActive = 0; print = output; counters.initialize(); }
void CG_RuntimeProfileReady(void) { counters.ready(); }
void CG_RuntimeProfileShutdown(void) { report(); counters.initialize(); cgRuntimeProfileActive = 0; }
void CG_RuntimeProfileReset(void) { if (counters.ownsThread()) { counters.reset(); cgRuntimeProfileActive = 0; } }
void CG_RuntimeProfileReport(void) { if (counters.ownsThread()) report(); }
cgProfileMark_t CG_RuntimeProfileFrameBegin(int enabled,int eligible) {
    const auto mark = counters.beginFrame(enabled != 0,eligible != 0);
    if (mark.active) cgRuntimeProfileActive = enabled >= 2 ? 2 : 1;
    return mark;
}
void CG_RuntimeProfileFrameEnd(cgProfileMark_t mark,int complete,int players,int entities) {
    if (!mark.active) return;
    counters.endFrame(mark,complete != 0,players,entities);
    if (counters.ownsThread() && !counters.frameOpen()) cgRuntimeProfileActive = 0;
}
cgProfileMark_t CG_RuntimeProfileStageBegin(void) { return counters.beginStage(); }
void CG_RuntimeProfileStageEnd(cgProfileStage_t stage,cgProfileMark_t mark) { counters.endStage(stage,mark); }
}
