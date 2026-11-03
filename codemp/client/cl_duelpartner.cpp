/*
 * Client-side sparring driver. GPLv2; see LICENSE.txt.
 * Sends ordinary player commands from a second client, without server AI changes.
 */
#include "client.h"
#include "cl_cgameapi.h"
#include "qcommon/cm_public.h"

extern vec3_t cl_sendAngles, cl_lastViewAngles;

namespace {
enum Phase { PrepareFirst, AttackFirst, ForwardFirst, PrepareSecond, AttackSecond, ForwardSecond, PhaseCount };
cvar_t *waitFps, *firstAttackFrames, *firstForwardFrames, *prepareFrames,
       *secondAttackFrames, *secondForwardFrames;

// Player-provided movement bind at 142 FPS, in its command order. The first
// and second attacks each have a right-only lead-in. Each wait
// uses the engine's integer frame step (1000 / FPS), not fractional milliseconds.
struct PhaseInput { int forward, right, buttons; cvar_t **frames; };
const PhaseInput phases[] = {
	{0, 127, 0, &prepareFrames},
	{0, 127, BUTTON_ATTACK, &firstAttackFrames},
	{127, 0, 0, &firstForwardFrames},
	{0, 127, 0, &prepareFrames},
	{0, 127, BUTTON_ATTACK, &secondAttackFrames},
	{127, 0, 0, &secondForwardFrames}
};

const char *TargetInfo(int target) {
	return cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + target];
}

const entityState_t *TargetEntity(int target) {
	for (int i = 0; i < cl.snap.numEntities; ++i) {
		const entityState_t *ent = &cl.parseEntities[
			(cl.snap.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];
		if (ent->number == target && ent->eType == ET_PLAYER && !(ent->eFlags & EF_DEAD))
			return ent;
	}
	return NULL;
}

void Stop(const char *reason) {
	cl.duelPartner.active = qfalse;
	cl.duelPartner.release = qtrue;
	if (reason) Com_Printf("Duel partner: %s\n", reason);
}

bool CanTrain() {
	return cls.state == CA_ACTIVE && cl.snap.valid && !clc.demoplaying &&
		!(cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE) &&
		cl.snap.ps.pm_type == PM_NORMAL && cl.snap.ps.stats[STAT_HEALTH] > 0 &&
		cl.snap.ps.persistant[PERS_TEAM] != TEAM_SPECTATOR &&
		cl.snap.ps.weapon == WP_SABER && !cl.snap.ps.saberHolstered &&
		!cl.snap.ps.m_iVehicleNum;
}

// Snapshot positions only: no hidden-entity lookup or prediction through walls.
bool TargetDirection(const entityState_t *target, vec3_t direction) {
	vec3_t eye, point;
	VectorCopy(cl.snap.ps.origin, eye);
	eye[2] += cl.snap.ps.viewheight;
	VectorCopy(target->pos.trBase, point);
	point[2] += 24.0f; // standing torso aim point, independent of model assets
	VectorSubtract(point, eye, direction);
	trace_t trace;
	CM_BoxTrace(&trace, eye, point, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, qfalse);
	return !trace.startsolid && !trace.allsolid && trace.fraction == 1.0f;
}

int Delay(cvar_t *setting) {
	const int fps = (int)Com_Clamp(1, 1000, waitFps->integer);
	const int frames = (int)Com_Clamp(1, 10000, setting->integer);
	return frames * (1000 / fps);
}

void Start() {
	Stop(NULL); // invalid acquisition must also stop the previous run
	if (!CanTrain()) {
		Com_Printf("Duel partner: join play with an ignited saber first.\n");
		return;
	}
	const int target = CGVM_CrosshairPlayer();
	if (target < 0 || target >= MAX_CLIENTS || target == cl.snap.ps.clientNum ||
		!*TargetInfo(target)) {
		Com_Printf("Duel partner: aim at a living player and try again.\n");
		return;
	}
	const entityState_t *ent = TargetEntity(target);
	vec3_t direction, view, forward;
	if (!ent || !TargetDirection(ent, direction)) {
		Com_Printf("Duel partner: target is not visible.\n");
		return;
	}
	for (int i = 0; i < 3; ++i)
		view[i] = cl.viewangles[i] + SHORT2ANGLE(cl.snap.ps.delta_angles[i]);
	AngleVectors(view, forward, NULL, NULL);
	VectorNormalize(direction);
	// CGVM_CrosshairPlayer may retain a recent target: require current alignment.
	if (DotProduct(forward, direction) < 0.98f) {
		Com_Printf("Duel partner: center the crosshair on the player's torso.\n");
		return;
	}
	cl.duelPartner.target = target;
	cl.duelPartner.phase = PrepareFirst;
	cl.duelPartner.started = qfalse;
	cl.duelPartner.phaseTime = cl.serverTime;
	cl.duelPartner.serverId = cl.serverId;
	cl.duelPartner.snapFlags = cl.snap.snapFlags & SNAPFLAG_SERVERCOUNT;
	Q_strncpyz(cl.duelPartner.targetInfo, TargetInfo(target), sizeof(cl.duelPartner.targetInfo));
	cl.duelPartner.release = qfalse;
	cl.duelPartner.active = qtrue;
	Com_Printf("Duel partner: tracking client %d. duelpartner stop to release.\n", target);
}

void Command() {
	if (Cmd_Argc() == 2 && !Q_stricmp(Cmd_Argv(1), "start")) Start();
	else if (Cmd_Argc() == 2 && !Q_stricmp(Cmd_Argv(1), "stop")) Stop("stopped.");
	else Com_Printf("Usage: duelpartner start|stop\n");
}
} // namespace

void CL_InitDuelPartner(void) {
	waitFps = Cvar_Get("cl_duelPartnerWaitFps", "142", 0);
	firstAttackFrames = Cvar_Get("cl_duelPartnerFirstAttackFrames", "2", 0);
	firstForwardFrames = Cvar_Get("cl_duelPartnerFirstForwardFrames", "350", 0);
	prepareFrames = Cvar_Get("cl_duelPartnerPrepareFrames", "2", 0);
	secondAttackFrames = Cvar_Get("cl_duelPartnerSecondAttackFrames", "10", 0);
	secondForwardFrames = Cvar_Get("cl_duelPartnerSecondForwardFrames", "500", 0);
	Cmd_AddCommand("duelpartner", Command, "Second-client sparring: start at crosshair target, or stop");
}

void CL_ShutdownDuelPartner(void) {
	Stop(NULL);
	Cmd_RemoveCommand("duelpartner");
}

void CL_DuelPartnerMove(usercmd_t *cmd) {
	if (cl.duelPartner.active) {
		const int target = cl.duelPartner.target;
		if (!CanTrain() || Key_GetCatcher() || cl.serverId != cl.duelPartner.serverId ||
			(cl.snap.snapFlags & SNAPFLAG_SERVERCOUNT) != cl.duelPartner.snapFlags ||
			cl.serverTime < cl.duelPartner.phaseTime ||
			cl.serverTime - cl.snap.serverTime > 500 ||
			strcmp(TargetInfo(target), cl.duelPartner.targetInfo)) {
			Stop("session, player or target changed.");
		}
	}
	if (!cl.duelPartner.active) {
		if (cl.duelPartner.release) {
			cmd->forwardmove = cmd->rightmove = cmd->upmove = 0;
			cmd->buttons = cmd->generic_cmd = 0;
			cl.duelPartner.release = qfalse;
		}
		return;
	}

	const entityState_t *ent = TargetEntity(cl.duelPartner.target);
	vec3_t direction, angles;
	if (!ent || !TargetDirection(ent, direction)) {
		Stop("target lost.");
		CL_DuelPartnerMove(cmd); // consume the one neutral release command
		return;
	}

	vectoangles(direction, angles);
	for (int i = 0; i < 3; ++i) {
		cl.viewangles[i] = AngleNormalize180(angles[i] - SHORT2ANGLE(cl.snap.ps.delta_angles[i]));
		cmd->angles[i] = ANGLE2SHORT(cl.viewangles[i]);
	}
	VectorCopy(cl.viewangles, cl_sendAngles);
	VectorCopy(cl.viewangles, cl_lastViewAngles);
	cmd->forwardmove = cmd->rightmove = cmd->upmove = 0;
	cmd->buttons = cmd->generic_cmd = 0;
	cmd->weapon = WP_SABER;

	// Begin timing with the first emitted input, rather than the F9 event. Advance
	// at most once per command so a stall cannot skip an attack or forward release.
	if (!cl.duelPartner.started) {
		cl.duelPartner.started = qtrue;
		cl.duelPartner.phaseTime = cl.serverTime;
	}
	else if (cl.serverTime - cl.duelPartner.phaseTime >=
		Delay(*phases[cl.duelPartner.phase].frames)) {
		cl.duelPartner.phase = (cl.duelPartner.phase + 1) % PhaseCount;
		cl.duelPartner.phaseTime = cl.serverTime;
	}
	const PhaseInput &input = phases[cl.duelPartner.phase];
	cmd->forwardmove = input.forward;
	cmd->rightmove = input.right;
	cmd->buttons = input.buttons;
}
