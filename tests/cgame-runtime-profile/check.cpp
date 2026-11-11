#include "cg_runtime_profile_state.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
extern "C" int c_profile_adapter_disabled(void);
extern "C" int c_profile_fine_adapter_disabled(void);
extern "C" int c_profile_player_wrapper(int exitAfter);
extern "C" int c_profile_animation_adapter(void);
extern "C" int c_profile_selector_adapter(void);
static uint64_t now,clocks;
static uint64_t clockNs() { ++clocks; return now; }
static std::vector<std::string> output;
static void print(const char *format,...) {
    char text[1024]; va_list args; va_start(args,format); std::vsnprintf(text,sizeof(text),format,args); va_end(args); output.emplace_back(text);
}
int main() {
    CgameProfile::Counters counters(clockNs);
    CHECK(clocks == 0);
    counters.initialize();
    CHECK(!counters.beginFrame(true,true).active && clocks == 0);
    counters.ready();
    for (int i = 0; i < 1000; ++i) {
        CHECK(!counters.beginFrame(false,true).active && !counters.beginStage().active);
    }
    CHECK(clocks == 0 && !counters.beginFrame(true,false).active);
    // Initializing and its loading pacifiers on a worker must not bind later
    // active gameplay-frame ownership to that worker.
    std::thread initialize([&]() {
        counters.initialize(); CHECK(!counters.beginFrame(true,true).active); counters.ready();
    });
    initialize.join(); CHECK(clocks == 0);
    now = 10; auto frame = counters.beginFrame(true,true); CHECK(frame.active);
    now = 20; auto outer = counters.beginStage();
    now = 30; auto inner = counters.beginStage();
    now = 40; counters.endStage(CGP_RENDER_SCENE,inner);
    now = 50; counters.endStage(CGP_DRAW_ACTIVE,outer);
    CHECK(!counters.beginFrame(true,true).active); // Recursive screen update.
    const auto before = clocks;
    std::thread background([&]() {
        CHECK(!counters.beginStage().active && !counters.beginFrame(true,true).active);
        counters.endStage(CGP_ENTITIES,outer); counters.reset();
    });
    background.join(); CHECK(clocks == before && counters.frameOpen());
    now = 60; counters.endFrame(frame,true,24,40);
    CHECK(counters.get(CGP_FRAME).nanoseconds == 50 && counters.get(CGP_DRAW_ACTIVE).nanoseconds == 30);
    CHECK(counters.get(CGP_RENDER_SCENE).nanoseconds == 10 && counters.frames() == 1);
    CHECK(counters.minPlayers() == 24 && counters.meanPlayers() == 24 && counters.maxEntities() == 40);
    now = 70; frame = counters.beginFrame(true,true);
    now = 80; counters.endFrame(frame,false,-1,-1);
    CHECK(counters.frames() == 1 && counters.early() == 1 && !counters.frameOpen());
    now = 90; frame = counters.beginFrame(true,true);
    now = 100; counters.endFrame(frame,true,20,30);
    CHECK(counters.meanPlayers() == 22 && counters.minPlayers() == 20 && counters.maxPlayers() == 24);
    CHECK(counters.meanEntities() == 35 && counters.samples() == 2);
    // Error/reset recovery drops stale marks and leaves the next frame usable.
    now = 110; frame = counters.beginFrame(true,true); outer = counters.beginStage();
    counters.reset(); now = 120; counters.endStage(CGP_ENTITIES,outer); counters.endFrame(frame,true,24,40);
    CHECK(!counters.get(CGP_FRAME).calls && !counters.frameOpen() && !counters.samples());
    frame = counters.beginFrame(true,true); now = 130; counters.endFrame(frame,true,1,2);
    CHECK(counters.frames() == 1 && counters.get(CGP_FRAME).nanoseconds == 10);
    // Real C translation unit, linked to the actual C++ profiler module.
    CG_RuntimeProfileInit(print); CHECK(c_profile_adapter_disabled() && c_profile_fine_adapter_disabled());
    CG_RuntimeProfileReport(); CHECK(output.empty());
    CHECK(!CG_RuntimeProfileFrameBegin(1,1).active && !cgRuntimeProfileActive);
    std::thread load([]() { CG_RuntimeProfileInit(print); CG_RuntimeProfileReady(); }); load.join();
    frame = CG_RuntimeProfileFrameBegin(1,1); CHECK(frame.active && cgRuntimeProfileActive);
    CHECK(c_profile_fine_adapter_disabled()); // Coarse mode never enters fine clocks/scopes.
    CHECK(c_profile_player_wrapper(2) == 3); // Body behavior is unchanged with fine profiling off.
    CHECK(c_profile_animation_adapter());
    CHECK(c_profile_selector_adapter());
    outer = CG_ProfileBegin(); CG_ProfileEnd(CGP_PREDICTION,outer);
    CG_RuntimeProfileFrameEnd(frame,1,24,48); CHECK(!cgRuntimeProfileActive);
    CG_RuntimeProfileReport(); CHECK(!output.empty() && output[0].find("1 completed frames") != std::string::npos);
    bool population = false,stage = false;
    for (const auto &line : output) {
        population |= line.find("players min/mean/max 24/24.00/24") != std::string::npos;
        stage |= line.find("CGPROFILE prediction:") != std::string::npos;
    }
    CHECK(population && stage);
    for (const auto &line : output) CHECK(line.find("CGPROFILE fine ") == std::string::npos);
    CG_RuntimeProfileReset(); output.clear();
    frame = CG_RuntimeProfileFrameBegin(2,1); CHECK(frame.active && cgRuntimeProfileActive == 2);
    CHECK(c_profile_player_wrapper(0) == 1); // Return from initial segment.
    CHECK(c_profile_player_wrapper(1) == 2); // Return after first transition.
    CHECK(c_profile_player_wrapper(2) == 3); // Normal completion after two transitions.
    CHECK(c_profile_animation_adapter());
    CHECK(c_profile_selector_adapter());
    CG_RuntimeProfileFrameEnd(frame,1,24,25); CG_RuntimeProfileReport();
    bool total = false,setup = false,model = false,tail = false;
    bool triggers = false,playback = false,bones = false,events = false;
    bool indexed = false,linear = false,dispatch = false;
    for (const auto &line : output) {
        total |= line.find("fine player inclusive: 3 calls") != std::string::npos;
        setup |= line.find("fine player transform/client setup: 7 calls") != std::string::npos;
        model |= line.find("fine player model validation: 2 calls") != std::string::npos;
        tail |= line.find("fine player weapon/powerup/effects tail: 1 calls") != std::string::npos;
        triggers |= line.find("fine animation trigger sounds inclusive: 4 calls") != std::string::npos;
        playback |= line.find("fine animation playback: 4 calls") != std::string::npos;
        bones |= line.find("fine animation bone frame queries: 7 calls") != std::string::npos;
        events |= line.find("fine animation event scans/dispatch: 5 calls") != std::string::npos;
        indexed |= line.find("fine animation indexed selector inclusive: 2 calls") != std::string::npos;
        linear |= line.find("fine animation linear selector inclusive: 2 calls") != std::string::npos;
        dispatch |= line.find("fine animation event dispatch: 4 calls") != std::string::npos;
    }
    CHECK(total && setup && model && tail && !cgRuntimeProfileActive);
    CHECK(triggers && playback && bones && events);
    CHECK(indexed && linear && dispatch);
    // Shutdown may run on the joined load worker after the frame owner stops.
    std::thread shutdown([]() { CG_RuntimeProfileShutdown(); }); shutdown.join();
    CHECK(!cgRuntimeProfileActive && c_profile_adapter_disabled());
    std::puts("Cgame default-off clocks, coarse/fine scopes, nested counters, async lifecycle, player early returns/reset and C adapter checks passed.");
}
