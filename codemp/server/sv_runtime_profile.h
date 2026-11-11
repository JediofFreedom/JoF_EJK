// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

using SVRuntimeClock = std::chrono::steady_clock;

enum class SVRuntimeStage { Frame, BotAI, Game, ClientMessages, Count };

// Server/main-thread diagnostics only. Disabled scopes do not read the clock.
class SVRuntimeProfileScope {
public:
	explicit SVRuntimeProfileScope(SVRuntimeStage stage);
	~SVRuntimeProfileScope();
	SVRuntimeProfileScope(const SVRuntimeProfileScope &) = delete;
	SVRuntimeProfileScope &operator=(const SVRuntimeProfileScope &) = delete;
private:
	SVRuntimeStage stage;
	bool active;
	std::uint64_t generation;
	SVRuntimeClock::time_point start;
};

void SV_RuntimeProfileInit();
void SV_RuntimeProfileReset();
void SV_RuntimeProfileReport();
void SV_RuntimeProfileBoundary();
