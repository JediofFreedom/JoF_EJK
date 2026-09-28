/*
 * Force Destruction: a server-authoritative, Legends-inspired dark-side blast.
 * Distributed under the GNU General Public License, version 2 or later.
 */
#include "g_local.h"

extern qboolean gSiegeRoundBegun;

#define DESTRUCTION_RECOVERY 650
#define DESTRUCTION_LIFETIME 3000

static int DestructionClamp(int value, int low, int high)
{
	return value < low ? low : (value > high ? high : value);
}

static qboolean DestructionGranted(gentity_t *self)
{
	const unsigned int realPowers = (1u << NUM_FORCE_POWERS) - 1;
	unsigned int disabled = g_forcePowerDisable.integer;
	playerState_t *ps;

	if (!self || !self->inuse || !self->client || self->s.number >= MAX_CLIENTS ||
		self->client->pers.connected != CON_CONNECTED || !g_forceDestruction.integer)
		return qfalse;

	ps = &self->client->ps;
	if (!ps->duelInProgress)
		disabled |= g_forcePowerDisableFFA.integer;
	return self->health > 0 && ps->stats[STAT_HEALTH] > 0 &&
		ps->fd.forceSide == FORCE_DARKSIDE && ps->pm_type == PM_NORMAL &&
		!(ps->eFlags & EF_DEAD) && !(ps->pm_flags & PMF_FOLLOW) &&
		self->client->sess.sessionTeam != TEAM_SPECTATOR &&
		self->client->tempSpectate < level.time && !self->client->sess.raceMode &&
		!self->client->noclip && !ps->forceRestricted && !ps->trueNonJedi &&
		g_maxForceRank.integer > 0 && !(disabled & DESTRUCTION_KNOWN_FLAG) &&
		(disabled & realPowers) != realPowers;
}

void G_UpdateForceDestruction(gentity_t *self)
{
	if (!self || !self->client)
		return;
	if (DestructionGranted(self))
		self->client->ps.fd.forcePowersKnown |= DESTRUCTION_KNOWN_FLAG;
	else
		self->client->ps.fd.forcePowersKnown &= ~DESTRUCTION_KNOWN_FLAG;
}

void ForceDestruction(gentity_t *self)
{
	playerState_t *ps;
	gentity_t *missile;
	trace_t trace;
	vec3_t start, forward;
	vec3_t mins = {-5, -5, -5}, maxs = {5, 5, 5};
	int cost = DestructionClamp(g_forceDestructionCost.integer, 1, 100);
	int cooldown = DestructionClamp(g_forceDestructionCooldown.integer, 500, 30000);

	// Recheck entitlement here: neither a stale snapshot nor a forged command is authority.
	if (!DestructionGranted(self) || level.intermissiontime || level.intermissionQueued ||
		level.pause.state != PAUSE_NONE || (level.gametype == GT_SIEGE && !gSiegeRoundBegun))
		return;

	ps = &self->client->ps;
	// Borrow only the shared offensive-power restrictions, not Lightning's rank/cost.
	if (!BG_CanUseFPNow(level.gametype, ps, level.time, FP_LIGHTNING) ||
		ps->fd.forceDeactivateAll || ps->fd.forceGripCripple ||
		ps->forceHandExtend != HANDEXTEND_NONE || ps->weaponTime > 0 ||
		(ps->pm_flags & PMF_STUCK_TO_WALL) || ps->heldByClient ||
		BG_InGrappleMove(ps->legsAnim) || BG_InGrappleMove(ps->torsoAnim) ||
		(ps->fd.forcePowersActive & ((1 << FP_GRIP) | (1 << FP_LIGHTNING) | (1 << FP_DRAIN))) ||
		self->client->forceDestructionCooldown > level.time || ps->fd.forcePower < cost)
		return;

	if (!ps->saberHolstered &&
		((g_saberRestrictForce.integer &&
			((self->client->saber[0].saberFlags & SFL_TWO_HANDED) || self->client->saber[1].model[0])) ||
		((self->client->saber[0].forceRestrictions | self->client->saber[1].forceRestrictions) & (1 << FP_LIGHTNING))))
		return;

	AngleVectors(ps->viewangles, forward, NULL, NULL);
	VectorCopy(ps->origin, start);
	start[2] += ps->viewheight;
	// Trace the entire spawn offset; never create an orb beyond a wall or low ceiling.
	JP_Trace(&trace, ps->origin, mins, maxs, start, self->s.number,
		MASK_SOLID | CONTENTS_SHOTCLIP, qfalse, 0, 0);
	if (trace.startsolid || trace.allsolid)
		return;
	VectorCopy(trace.endpos, start);

	missile = CreateMissileNew(start, forward,
		DestructionClamp(g_forceDestructionSpeed.integer, 100, 3000),
		DESTRUCTION_LIFETIME, self, qfalse, qfalse, qfalse);
	missile->classname = "force_destruction";
	missile->s.weapon = WP_CONCUSSION; // compatible fallback for older clients
	missile->mass = 10;
	missile->s.generic1 = DESTRUCTION_MISSILE_TAG;
	missile->s.bolt1 = self->s.bolt1;
	missile->s.pos.trTime = level.time; // no projectile prestep across the spawn trace
	missile->s.otherEntityNum2 = G_EffectIndex("jof/destruction/projectile");
	missile->s.emplacedOwner = G_EffectIndex("jof/destruction/impact");
	missile->damage = DestructionClamp(g_forceDestructionDamage.integer, 1, 500);
	missile->splashDamage = missile->damage;
	missile->splashRadius = DestructionClamp(g_forceDestructionRadius.integer, 16, 512);
	missile->methodOfDeath = missile->splashMethodOfDeath = MOD_FORCE_DARK;
	missile->clipmask = MASK_SHOT; // an energy blast is not a saber-deflectable blaster bolt
	missile->count = cost; // snapshot the spent energy for Absorb, even if cvars change in flight
	VectorCopy(mins, missile->r.mins);
	VectorCopy(maxs, missile->r.maxs);
	trap->LinkEntity((sharedEntity_t *)missile);

	ps->fd.forcePower -= cost;
	ps->fd.forcePowerRegenDebounceTime = level.time + 1000;
	self->client->forceDestructionCooldown = level.time + cooldown;
	ps->forceHandExtend = HANDEXTEND_FORCEPUSH;
	ps->forceHandExtendTime = level.time + DESTRUCTION_RECOVERY;
	ps->weaponTime = DESTRUCTION_RECOVERY;
	self->client->dangerTime = level.time;
	ps->eFlags &= ~EF_INVULNERABLE;
	self->client->invulnerableTimer = 0;
	G_Sound(self, CHAN_BODY, G_SoundIndex("sound/weapons/force/push.wav"));
}

static void DestructionDamage(gentity_t *missile, gentity_t *target,
	vec3_t origin, vec3_t direction, int damage, qboolean splash)
{
	gentity_t *attacker = missile->parent;
	int remaining;

	if (!attacker || !attacker->inuse || !target->inuse || !target->takedamage || damage <= 0)
		return;
	if (attacker->s.bolt1 != missile->s.bolt1 || (attacker->client &&
		(attacker->client->pers.connected != CON_CONNECTED || attacker->client->noclip || attacker->client->sess.raceMode)))
		return;
	if (target->client)
	{
		// Do these before Absorb or knockback: protected bystanders must be untouched.
		if (target->s.bolt1 != missile->s.bolt1 || target->client->noclip ||
			target->client->sess.sessionTeam == TEAM_SPECTATOR || target->health <= 0 ||
			target->client->tempSpectate >= level.time ||
			(target->client->sess.raceMode && !target->client->ps.duelInProgress) ||
			(target->flags & FL_GODMODE) || (target->client->ps.eFlags & EF_INVULNERABLE) ||
			BG_HasYsalamiri(level.gametype, &target->client->ps))
			return;
		if (attacker->client &&
			((target->client->ps.duelInProgress && target->client->ps.duelIndex != attacker->s.number) ||
			(attacker->client->ps.duelInProgress && attacker->client->ps.duelIndex != target->s.number)))
			return;
		if (target != attacker && OnSameTeam(target, attacker) && g_friendlyFire.value <= 0)
			return;

		if (target != attacker && (target->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)))
		{
			// Treat the blast as level-three Force energy. Use Push's conversion path
			// so Lightning-specific server tweaks cannot alter Destruction's defenses.
			remaining = WP_AbsorbConversion(target, target->client->ps.fd.forcePowerLevel[FP_ABSORB],
				attacker, FP_PUSH, FORCE_LEVEL_3, missile->count * damage / missile->damage);
			if (remaining >= 0)
				damage = damage * remaining / FORCE_LEVEL_3;
			if (damage <= 0)
				return;
		}
	}
	// Keep normal armor, Protect, friendly-fire scaling, damage attribution and knockback.
	G_Damage(target, missile, attacker, direction, origin, damage,
		splash ? DAMAGE_RADIUS : 0, MOD_FORCE_DARK);
}

void G_ForceDestructionImpact(gentity_t *missile, trace_t *trace)
{
	gentity_t *direct = &g_entities[trace->entityNum];
	int entities[MAX_GENTITIES], count, i, axis;
	vec3_t mins, maxs, delta, direction;
	float distance;

	VectorCopy(missile->s.pos.trDelta, direction);
	DestructionDamage(missile, direct, trace->endpos, direction, missile->damage, qfalse);
	for (axis = 0; axis < 3; ++axis)
	{
		mins[axis] = trace->endpos[axis] - missile->splashRadius;
		maxs[axis] = trace->endpos[axis] + missile->splashRadius;
	}
	count = trap->EntitiesInBox(mins, maxs, entities, MAX_GENTITIES);
	for (i = 0; i < count; ++i)
	{
		gentity_t *target = &g_entities[entities[i]];
		if (target == direct || !target->takedamage)
			continue; // never stack direct damage with splash on the same target
		for (axis = 0; axis < 3; ++axis)
		{
			delta[axis] = trace->endpos[axis] < target->r.absmin[axis] ?
				target->r.absmin[axis] - trace->endpos[axis] :
				(trace->endpos[axis] > target->r.absmax[axis] ? trace->endpos[axis] - target->r.absmax[axis] : 0);
		}
		distance = VectorLength(delta);
		if (distance >= missile->splashRadius || !CanDamage(target, trace->endpos))
			continue;
		VectorSubtract(target->r.currentOrigin, trace->endpos, direction);
		direction[2] += 24;
		DestructionDamage(missile, target, trace->endpos, direction,
			(int)(missile->splashDamage * (1.0f - distance / missile->splashRadius)), qtrue);
	}

	G_AddEvent(missile, EV_MISSILE_MISS, DirToByte(trace->plane.normal));
	missile->freeAfterEvent = qtrue;
	missile->s.eType = ET_GENERAL;
	missile->takedamage = qfalse;
	G_SetOrigin(missile, trace->endpos);
	trap->LinkEntity((sharedEntity_t *)missile);
}
