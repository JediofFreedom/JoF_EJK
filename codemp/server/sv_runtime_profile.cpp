// SPDX-License-Identifier: GPL-2.0-or-later
#include "server.h"
#include "sv_runtime_profile.h"

namespace {
struct SVRuntimeSample {
	std::uint64_t calls, totalNs, maxNs;
};
SVRuntimeSample samples[static_cast<unsigned>(SVRuntimeStage::Count)] = {};
std::uint64_t generation = 0;
cvar_t *sv_runtimeProfile = nullptr;

bool Enabled() {
	return sv_runtimeProfile && sv_runtimeProfile->integer != 0;
}

bool HasSamples() {
	for (const auto &sample : samples) {
		if (sample.calls) return true;
	}
	return false;
}
}

SVRuntimeProfileScope::SVRuntimeProfileScope(SVRuntimeStage stage)
	: stage(stage), active(Enabled()), generation(::generation), start() {
	if (active) start = SVRuntimeClock::now();
}

SVRuntimeProfileScope::~SVRuntimeProfileScope() {
	if (!active || generation != ::generation || !Enabled()) return;
	const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(SVRuntimeClock::now() - start).count();
	SVRuntimeSample &sample = samples[static_cast<unsigned>(stage)];
	const auto ns = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0;
	++sample.calls;
	sample.totalNs += ns;
	if (ns > sample.maxNs) sample.maxNs = ns;
}

void SV_RuntimeProfileReset() {
	++generation;
	for (auto &sample : samples) sample = {};
}

void SV_RuntimeProfileReport() {
	static const char *names[] = { "sv-frame", "bot-ai-call", "game-tick", "client-messages" };
	Com_Printf("SERVERPROFILE enabled=%d (nested stages; totals are not additive)\n", Enabled() ? 1 : 0);
	for (unsigned i = 0; i < static_cast<unsigned>(SVRuntimeStage::Count); ++i) {
		const auto &s = samples[i];
		Com_Printf("SERVERPROFILE %s calls=%llu total_ms=%.6f mean_us=%.3f max_us=%.3f\n", names[i],
			static_cast<unsigned long long>(s.calls), s.totalNs / 1000000.0,
			s.calls ? s.totalNs / (1000.0 * s.calls) : 0.0, s.maxNs / 1000.0);
	}
}

void SV_RuntimeProfileBoundary() {
	if (HasSamples()) SV_RuntimeProfileReport();
	SV_RuntimeProfileReset();
}

void SV_RuntimeProfileInit() {
	sv_runtimeProfile = Cvar_Get("sv_runtimeProfile", "0", 0, "Aggregate server frame, bot AI, game tick and client-message timings");
	Cvar_CheckRange(sv_runtimeProfile, 0, 1, qtrue);
	static bool commandsRegistered = false;
	if (!commandsRegistered) {
		Cmd_AddCommand("sv_runtimeProfileReset", SV_RuntimeProfileReset, "Reset aggregate server timings");
		Cmd_AddCommand("sv_runtimeProfileReport", SV_RuntimeProfileReport, "Report aggregate server timings");
		commandsRegistered = true;
	}
	SV_RuntimeProfileReset();
}
