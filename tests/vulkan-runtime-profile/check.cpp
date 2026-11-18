#include "vk_runtime_profile.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
static uint64_t now, clockCalls;
static uint64_t clockNow() { ++clockCalls; return now; }
int main() {
    RuntimeProfileCounters profile(clockNow);
    clockCalls = 0;
    for (int i = 0; i < 1000; ++i) {
        profile.end(RuntimeStage::Scene,profile.begin()); profile.frame();
    }
    CHECK(clockCalls == 0 && profile.frameCount() == 0 && profile.get(RuntimeStage::Scene).calls == 0);
    profile.setLevel(1);
    CHECK(!profile.begin(2).active);
    now = 10; const auto outer = profile.begin();
    now = 20; const auto inner = profile.begin();
    now = 30; profile.end(RuntimeStage::World,inner);
    now = 50; profile.end(RuntimeStage::Scene,outer); profile.frame();
    CHECK(profile.get(RuntimeStage::Scene).calls == 1 && profile.get(RuntimeStage::Scene).microseconds == 40);
    CHECK(profile.get(RuntimeStage::World).calls == 1 && profile.get(RuntimeStage::World).microseconds == 10);
    CHECK(profile.frameCount() == 1); // Nested totals intentionally overlap.
    const auto stale = profile.begin();
    profile.reset();
    now = 60; profile.end(RuntimeStage::Scene,stale);
    CHECK(profile.get(RuntimeStage::Scene).calls == 0 && profile.frameCount() == 0);
    const auto ownerMark = profile.begin();
    const auto beforeClock = clockCalls;
    std::thread background([&]() {
        CHECK(!profile.ownsThread() && !profile.begin().active);
        profile.frame(); profile.reset(); profile.end(RuntimeStage::Scene,ownerMark);
    });
    background.join();
    CHECK(clockCalls == beforeClock && profile.frameCount() == 0 && profile.get(RuntimeStage::Scene).calls == 0);
    now = 70; profile.end(RuntimeStage::Scene,ownerMark);
    CHECK(profile.get(RuntimeStage::Scene).calls == 1 && profile.get(RuntimeStage::Scene).microseconds == 10);
    auto acquire = profile.begin(); now = 75;
    profile.end(RuntimeStage::Acquire,acquire);
    auto present = profile.begin(); now = 92;
    profile.end(RuntimeStage::Present,present);
    CHECK(profile.get(RuntimeStage::Acquire).calls == 1 && profile.get(RuntimeStage::Acquire).microseconds == 5);
    CHECK(profile.get(RuntimeStage::Present).calls == 1 && profile.get(RuntimeStage::Present).microseconds == 17);
    profile.setLevel(2); CHECK(profile.begin(2).active);
    now = 80; CHECK(profile.elapsed() == 30);
    std::puts("Runtime profile disabled clocks, nesting, reset epochs and owning-thread checks passed.");
}
