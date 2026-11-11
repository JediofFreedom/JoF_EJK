#include "tr_local.h"
#include "vk_runtime_profile.h"

RuntimeProfileCounters vkRuntimeProfile;
static cvar_t *runtimeProfileMode;

void R_UpdateRuntimeProfile() {
    vkRuntimeProfile.setLevel(runtimeProfileMode ? static_cast<unsigned>(runtimeProfileMode->integer) : 0);
}
void R_InitRuntimeProfile() {
    runtimeProfileMode = ri.Cvar_Get("r_runtimeProfile", "0", CVAR_NONE,
        "Collect owning-thread Vulkan runtime stages: 0 off, 1 coarse, 2 finer; use r_runtimeProfileReset/Report");
    ri.Cvar_CheckRange(runtimeProfileMode, 0, 2, qtrue);
    vkRuntimeProfile.reset(); R_UpdateRuntimeProfile();
}
void R_ResetRuntimeProfile() { vkRuntimeProfile.reset(); R_UpdateRuntimeProfile(); }
void R_ReportRuntimeProfile() {
    if (!vkRuntimeProfile.ownsThread()) return;
    const uint64_t frames = vkRuntimeProfile.frameCount();
    if (!frames && !vkRuntimeProfile.get(RuntimeStage::Scene).calls && !vkRuntimeProfile.get(RuntimeStage::Backend).calls) return;
    static const char *names[] = {"scene build", "world traversal", "Ghoul2 frontend", "Ghoul2 skeleton",
        "backend commands", "GPU idle/fence wait", "swapchain acquire", "queue present", "Ghoul2 bolt", "Ghoul2 bone transform", "shader iterator", "leaf visibility"};
    static_assert(ARRAY_LEN(names) == static_cast<size_t>(RuntimeStage::Count), "Runtime stage names must match counters");
    ri.Printf(PRINT_ALL, "RUNTIMEPROFILE Vulkan: %llu frames, %.3f ms interval; owning-thread elapsed, nested totals overlap; instrumentation changes timing\n",
        static_cast<unsigned long long>(frames), vkRuntimeProfile.elapsed() / 1000.0);
    for (size_t i = 0; i < ARRAY_LEN(names); ++i) {
        const auto &counter = vkRuntimeProfile.get(static_cast<RuntimeStage>(i));
        if (!counter.calls) continue;
        ri.Printf(PRINT_ALL, "RUNTIMEPROFILE %s: %llu calls, %.3f ms total, %.6f ms/call, %.6f ms/frame\n", names[i],
            static_cast<unsigned long long>(counter.calls), counter.microseconds / 1000.0,
            counter.microseconds / (1000.0 * counter.calls), frames ? counter.microseconds / (1000.0 * frames) : 0.0);
    }
}
void R_ShutdownRuntimeProfile() { R_ReportRuntimeProfile(); vkRuntimeProfile.setLevel(0); }
