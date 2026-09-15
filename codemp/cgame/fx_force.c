/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Any dedicated force oriented effects

#include "cg_local.h"
#include "fx_local.h"

// A shared time-window budget bounds work even at very high frame rates.
#define LIGHTNING_INTERVAL 40
#define LIGHTNING_EMIT_INTERVAL 10
#define LIGHTNING_TRACE_BUDGET 192
#define LIGHTNING_EFFECT_BUDGET 256
#define LIGHTNING_SOUND_INTERVAL 200
#define LIGHTNING_SOUND_BUDGET 6
static int lightningBudgetTime;
static int lightningTraces;
static int lightningEffects;
static int lightningSoundBudgetTime;
static int lightningSounds;

static qboolean FX_LightningTrace(trace_t *tr, vec3_t start, vec3_t end, int owner) {
	if (lightningTraces >= LIGHTNING_TRACE_BUDGET)
		return qfalse;
	lightningTraces++;
	CG_Trace(tr, start, NULL, NULL, end, owner, MASK_SHOT);
	return !tr->startsolid && !tr->allsolid;
}

static qboolean FX_LightningSurface(const trace_t *tr) {
	if (tr->fraction == 1.0f || tr->startsolid || tr->allsolid ||
		(tr->surfaceFlags & (SURF_SKY | SURF_NOIMPACT | SURF_NODRAW)))
		return qfalse;
	return tr->entityNum == ENTITYNUM_WORLD ||
		(tr->entityNum >= 0 && tr->entityNum < ENTITYNUM_WORLD &&
		 cg_entities[tr->entityNum].currentState.solid == SOLID_BMODEL);
}

static void FX_LightningArc(vec3_t start, vec3_t end, float width, float chaos, qboolean mainBolt) {
	addElectricityArgStruct_t arc;
	vec3_t delta;
	VectorSubtract(end, start, delta);
	if (lightningEffects >= LIGHTNING_EFFECT_BUDGET || VectorLengthSquared(delta) < 1.0f)
		return;
	lightningEffects++;
	memset(&arc, 0, sizeof(arc));
	VectorCopy(start, arc.start);
	VectorCopy(end, arc.end);
	VectorSet(arc.sRGB, 1.0f, 1.0f, 1.0f);
	VectorCopy(arc.sRGB, arc.eRGB);
	arc.size1 = width;
	arc.size2 = mainBolt ? 1.0f : 0.5f;
	arc.alpha1 = 1.0f;
	arc.alpha2 = mainBolt ? 1.0f : 0.0f;
	arc.chaos = chaos;
	arc.killTime = mainBolt ? 65 : 90;
	arc.shader = cgs.media.forceLightningArcShader;
	arc.flags = FX_ALPHA_LINEAR | FX_SIZE_LINEAR;
	if (mainBolt) {
		// Stock electricity's TAPER / BRANCH / GROW flags provide its familiar
		// dense, animated silhouette. Surface arcs stay small and unbranched.
		arc.flags |= 0x01000000 | 0x02000000 | 0x04000000;
	}
	trap->FX_AddElectricity(&arc);
}

static void FX_LightningFlash(vec3_t origin, float size) {
	addspriteArgStruct_t flash;
	if (lightningEffects >= LIGHTNING_EFFECT_BUDGET)
		return;
	lightningEffects++;
	memset(&flash, 0, sizeof(flash));
	VectorCopy(origin, flash.origin);
	flash.scale = size;
	flash.sAlpha = 0.7f;
	flash.life = LIGHTNING_INTERVAL;
	flash.shader = cgs.media.forceLightningFlashShader;
	flash.flags = FX_ALPHA_LINEAR;
	trap->FX_AddSprite(&flash);
}

// Walk along supported surfaces, rather than reflecting a free-floating ray.
// A probe back into the wall at each step keeps the endpoint attached, and the
// final trace prevents the connecting segment from cutting through a corner.
static void FX_LightningSurfaceArcs(const trace_t *hit, vec3_t direction, int owner, float phase) {
	int branch, hop;
	vec3_t contact, start, end, normal, tangent, side, probeStart, probeEnd;
	trace_t sweep, support, link;

	VectorMA(hit->endpos, 2.0f, hit->plane.normal, contact);
	FX_LightningFlash(contact, 5.0f);
	for (branch = 0; branch < 2; branch++) {
		VectorCopy(hit->plane.normal, normal);
		VectorMA(direction, -DotProduct(direction, normal), normal, tangent);
		if (VectorNormalize(tangent) < 0.1f)
			PerpendicularVector(tangent, normal);
		CrossProduct(normal, tangent, side);
		VectorMA(tangent, sinf(phase) * 0.35f, side, tangent);
		VectorNormalize(tangent);
		if (branch)
			VectorScale(tangent, -1.0f, tangent);
		VectorCopy(contact, start);
		for (hop = 0; hop < 2; hop++) {
			VectorMA(start, 48.0f / (hop + 1), tangent, end);
			if (!FX_LightningTrace(&sweep, start, end, owner))
				break;
			if (sweep.fraction < 1.0f) {
				if (!FX_LightningSurface(&sweep))
					break;
				support = sweep;
			} else {
				VectorMA(end, 8.0f, normal, probeStart);
				VectorMA(end, -12.0f, normal, probeEnd);
				if (!FX_LightningTrace(&support, probeStart, probeEnd, owner) ||
					!FX_LightningSurface(&support) ||
					DotProduct(normal, support.plane.normal) < 0.5f)
					break;
			}
			VectorMA(support.endpos, 2.0f, support.plane.normal, end);
			if (!FX_LightningTrace(&link, start, end, owner) || link.fraction < 1.0f)
				break;
			FX_LightningArc(start, end, 2.0f / (hop + 1), 0.1f, qfalse);
			VectorCopy(support.plane.normal, normal);
			VectorMA(tangent, -DotProduct(tangent, normal), normal, tangent);
			if (VectorNormalize(tangent) < 0.1f)
				break;
			VectorCopy(end, start);
		}
	}
}

static void FX_LightningImpactSound(centity_t *cent, const trace_t *hit) {
	vec3_t contact;
	int sound;
	if (cent->lightningImpactSoundTime > cg.time &&
		cent->lightningImpactSoundTime <= cg.time + LIGHTNING_SOUND_INTERVAL + 60)
		return;
	if (cg.time < lightningSoundBudgetTime ||
		cg.time - lightningSoundBudgetTime >= LIGHTNING_SOUND_INTERVAL) {
		lightningSoundBudgetTime = cg.time;
		lightningSounds = 0;
	}
	if (lightningSounds >= LIGHTNING_SOUND_BUDGET)
		return;
	sound = (cg.time / LIGHTNING_INTERVAL + cent->currentState.number) % 3;
	cent->lightningImpactSoundTime = cg.time + LIGHTNING_SOUND_INTERVAL + sound * 30;
	if (!cgs.media.forceLightningImpactSounds[sound])
		return;
	lightningSounds++;
	VectorMA(hit->endpos, 2.0f, hit->plane.normal, contact);
	trap->S_StartSound(contact, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.forceLightningImpactSounds[sound]);
}

// Emit a stock-like dense spray independently of the slower surface response.
// If the shared budget is exhausted, the caller falls back to vanilla lightning.
qboolean FX_ForceLightningEnvironment(centity_t *cent, vec3_t origin, matrix3_t axis, qboolean wide) {
	int ray, rays, surfaceRay = -1;
	float phase;
	vec3_t end;
	trace_t hits[4];
	vec3_t directions[4];
	qboolean valid[4];

	if (!cg_lightningEnvironment.integer)
		return qfalse;
	if (cent->lightningEnvironmentTime > cg.time &&
		cent->lightningEnvironmentTime <= cg.time + LIGHTNING_EMIT_INTERVAL)
		return qtrue;
	if (cg.time < lightningBudgetTime || cg.time - lightningBudgetTime >= LIGHTNING_INTERVAL) {
		lightningBudgetTime = cg.time;
		lightningTraces = lightningEffects = 0;
	}
	// Three stable lanes preserve the previous average hand-spray density
	// without reshuffling the wall contacts whenever the ray count changes.
	rays = wide ? 3 : 1;
	// Reserve the entire hand spray before drawing anything. Falling back
	// midway would double up custom and stock bolts in the same frame.
	if (lightningTraces + rays > LIGHTNING_TRACE_BUDGET ||
		lightningEffects + rays + 1 > LIGHTNING_EFFECT_BUDGET)
		return qfalse;
	cent->lightningEnvironmentTime = cg.time + LIGHTNING_EMIT_INTERVAL;
	phase = cg.time * 0.017f + cent->currentState.number * 2.39996f;
	for (ray = 0; ray < rays; ray++) {
		VectorCopy(axis[0], directions[ray]);
		if (wide) {
			float spread = (ray / (float)(rays - 1) - 0.5f) * 1.4f;
			VectorMA(directions[ray], spread + sinf(phase * 0.3f + ray * 2.4f) * 0.05f, axis[1], directions[ray]);
			VectorMA(directions[ray], sinf(phase * 0.2f + ray) * 0.03f, axis[2], directions[ray]);
		}
		VectorNormalize(directions[ray]);
		VectorMA(origin, wide ? 512.0f : 2048.0f, directions[ray], end);
		valid[ray] = FX_LightningTrace(&hits[ray], origin, end, cent->currentState.number);
	}
	for (ray = 0; ray < rays; ray++) {
		if (!valid[ray])
			continue;
		VectorCopy(hits[ray].endpos, end);
		if (FX_LightningSurface(&hits[ray])) {
			VectorMA(end, 2.0f, hits[ray].plane.normal, end);
			// Choose the nearest impact for audio; every contact gets a visual response.
			if (surfaceRay < 0 || hits[ray].fraction < hits[surfaceRay].fraction)
				surfaceRay = ray;
		}
		FX_LightningArc(origin, end, 5.0f + sinf(phase + ray) * 2.0f,
			1.4f + sinf(phase * 1.3f + ray) * 0.6f, qtrue);
	}
	// Keep the small hand flash which was absent in the first prototype.
	FX_LightningFlash(origin, 18.0f);

	if (surfaceRay < 0 || (cent->lightningSurfaceTime > cg.time &&
		cent->lightningSurfaceTime <= cg.time + LIGHTNING_INTERVAL))
		return qtrue;
	cent->lightningSurfaceTime = cg.time + LIGHTNING_INTERVAL;
	phase = cg.time * 0.004f + cent->currentState.number * 2.39996f;
	FX_LightningImpactSound(cent, &hits[surfaceRay]);
	for (ray = 0; ray < rays; ray++)
		if (valid[ray] && FX_LightningSurface(&hits[ray]))
			FX_LightningSurfaceArcs(&hits[ray], directions[ray], cent->currentState.number, phase + ray);
	return qtrue;
}

/*
-------------------------
FX_ForceDrained
-------------------------
*/
// This effect is not generic because of possible enhancements
void FX_ForceDrained(vec3_t origin, vec3_t dir)
{
	VectorScale(dir, -1.0, dir);
	trap->FX_PlayEffectID(cgs.effects.forceDrained, origin, dir, -1, -1, qfalse);
}

