// Exercise the production driver with synthetic snapshots and engine boundaries.
#include "../../codemp/client/cl_duelpartner.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

clientActive_t cl;
clientConnection_t clc;
clientStatic_t cls;
vec3_t cl_sendAngles, cl_lastViewAngles;
static bool blocked;
static int catcher, crosshair = 1;
static cvar_t settings[6];
static int settingCount;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

void QDECL Com_Printf(const char *, ...) {}
int Key_GetCatcher(void) { return catcher; }
int CGVM_CrosshairPlayer(void) { return crosshair; }
void Cmd_AddCommand(const char *, xcommand_t, const char *) {}
void Cmd_RemoveCommand(const char *) {}
int Cmd_Argc(void) { return 0; }
char *Cmd_Argv(int) { static char empty[] = ""; return empty; }
cvar_t *Cvar_Get(const char *, const char *value, uint32_t, const char *) {
	cvar_t *setting = &settings[settingCount++];
	setting->integer = std::atoi(value);
	return setting;
}
void Q_strncpyz(char *dest, const char *src, int size) {
	std::snprintf(dest, size, "%s", src);
}
int Q_stricmp(const char *a, const char *b) { return std::strcmp(a, b); }
void CM_BoxTrace(trace_t *trace, const vec3_t, const vec3_t, const vec3_t,
	const vec3_t, clipHandle_t, int, int) {
	std::memset(trace, 0, sizeof(*trace));
	trace->fraction = blocked ? 0.5f : 1.0f;
}

static void Reset(float distance = 80) {
	std::memset(&cl, 0, sizeof(cl));
	std::memset(&clc, 0, sizeof(clc));
	cls.state = CA_ACTIVE;
	blocked = false; catcher = 0; crosshair = 1;
	cl.snap.valid = qtrue;
	cl.snap.ps.pm_type = PM_NORMAL;
	cl.snap.ps.stats[STAT_HEALTH] = 100;
	cl.snap.ps.weapon = WP_SABER;
	cl.snap.ps.viewheight = 24;
	cl.snap.numEntities = 1;
	cl.parseEntities[0].number = 1;
	cl.parseEntities[0].eType = ET_PLAYER;
	cl.parseEntities[0].pos.trBase[0] = distance;
	cl.gameState.stringOffsets[CS_PLAYERS + 1] = 1;
	std::strcpy(cl.gameState.stringData + 1, "\\n\\Target");
}

static usercmd_t Tick(int step) {
	cl.serverTime += step;
	cl.snap.serverTime = cl.serverTime;
	usercmd_t cmd = {};
	CL_DuelPartnerMove(&cmd);
	return cmd;
}

int main() {
	CL_InitDuelPartner();
	const int steps[] = {8, 7, 4, 3};
	for (int step : steps) {
		Reset(); Start(); CHECK(cl.duelPartner.active);
		// User's final bind at integer 142 FPS (7 ms): right-only 2 frames,
		// attack+right 2, forward 350, right-only 2, attack+right 10, forward 500.
		const int holds[] = {14, 14, 2450, 14, 70, 3500};
		const int expectedForward[] = {0, 0, 127, 0, 0, 127};
		const int expectedRight[] = {127, 127, 0, 127, 127, 0};
		const int expectedAttack[] = {0, BUTTON_ATTACK, 0, 0, BUTTON_ATTACK, 0};
		int expectedPhase = 0, entered = step, transitions = 0, attacks = 0;
		bool wasAttack = false;
		for (int t = step; transitions < 18; t += step) {
			if (t - entered >= holds[expectedPhase]) {
				expectedPhase = (expectedPhase + 1) % 6;
				entered = t;
				++transitions;
			}
			usercmd_t cmd = Tick(step);
			CHECK(cmd.forwardmove == expectedForward[expectedPhase]);
			CHECK(cmd.rightmove == expectedRight[expectedPhase]);
			CHECK(cmd.buttons == expectedAttack[expectedPhase]);
			CHECK(cmd.upmove == 0 && cmd.generic_cmd == 0);
			bool attack = (cmd.buttons & BUTTON_ATTACK) != 0;
			if (attack && !wasAttack) ++attacks;
			wasAttack = attack;
		}
		CHECK(attacks == 6); // three full pairs, then the next right-only lead-in
	}
	// Stop neutralizes every phase without holding any engine key state.
	for (int phase = PrepareFirst; phase < PhaseCount; ++phase) {
		Reset(); Start(); cl.duelPartner.phase = phase;
		Tick(8); Stop(NULL);
		usercmd_t cmd = {}; cmd.forwardmove = 127; cmd.buttons = BUTTON_ATTACK;
		CL_DuelPartnerMove(&cmd);
		CHECK(!cl.duelPartner.active && !cmd.forwardmove && !cmd.buttons);
	}
	Reset(300); Start(); CHECK(Tick(8).rightmove == 127);
	cl.parseEntities[0].pos.trBase[0] = 0;
	cl.parseEntities[0].pos.trBase[1] = 300;
	Tick(8);
	CHECK(std::fabs(AngleNormalize180(cl.viewangles[YAW]) - 90) < 0.1f);
	// Real view angles include server delta angles, unlike encoded usercmd angles.
	Reset(); cl.snap.ps.delta_angles[YAW] = ANGLE2SHORT(90); cl.viewangles[YAW] = -90;
	Start(); CHECK(cl.duelPartner.active); usercmd_t cmd = Tick(8);
	CHECK(std::fabs(AngleNormalize180(SHORT2ANGLE(cmd.angles[YAW]) + 90)) < 0.1f);
	for (int fault = 0; fault < 8; ++fault) {
		Reset(); Start();
		switch (fault) {
		case 0: cl.parseEntities[0].eFlags |= EF_DEAD; break;
		case 1: cl.snap.ps.stats[STAT_HEALTH] = 0; break;
		case 2: cl.snap.numEntities = 0; break;
		case 3: blocked = true; break;
		case 4: catcher = 1; break;
		case 5: ++cl.serverId; break;
		case 6: cl.snap.snapFlags ^= SNAPFLAG_SERVERCOUNT; break;
		case 7: cl.gameState.stringData[2] = 'x'; break;
		}
		cmd = Tick(8); CHECK(!cl.duelPartner.active && !cmd.buttons && !cmd.forwardmove);
	}
	Reset(); cl.viewangles[YAW] = 90; Start(); CHECK(!cl.duelPartner.active);
	Reset(); clc.demoplaying = qtrue; Start(); CHECK(!cl.duelPartner.active);
	Reset(); Start(); cl.serverTime = 501; cmd = {}; CL_DuelPartnerMove(&cmd);
	CHECK(!cl.duelPartner.active);
	Reset(); Start(); Tick(8); std::memset(&cl, 0, sizeof(cl)); CHECK(!cl.duelPartner.active);
	std::puts("PASS: 8/7/4/3 ms input sequences, tracking/delta angles, stop phases and lifecycle guards");
}
