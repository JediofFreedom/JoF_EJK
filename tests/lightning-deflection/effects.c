#include "cgame/cg_local.h"
#include "cgame/fx_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)
cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
vmCvar_t cg_lightningEnvironment;
static snapshot_t snapshot;
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static int traces, sprites, arcs, sounds, lights, matrices, hidden;
static qboolean wall;
static addElectricityArgStruct_t firstArc;

void CG_Trace(trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int skip, int mask) {
  ++traces; memset(result, 0, sizeof(*result)); result->fraction = wall ? 0.5f : 1.0f;
  result->entityNum = wall ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
  VectorCopy(end, result->endpos); result->plane.normal[2] = 1;
  if (wall) result->endpos[0] = 60;
}
int CG_IsMindTricked(int a, int b, int c, int d, int client) { return hidden; }
static void AddElectricity(addElectricityArgStruct_t *arc) { if (!arcs) firstArc = *arc; ++arcs; }
static void AddSprite(addspriteArgStruct_t *sprite) { ++sprites; CHECK(sprite->life <= 65); }
static void AddLight(const vec3_t origin, float radius, float r, float g, float b) { ++lights; CHECK(b >= r); }
static void StartSound(const vec3_t origin, int entity, int channel, sfxHandle_t sound) { ++sounds; }
static qboolean HasModel(void *ghoul2, int model) { return qtrue; }
static int AddBolt(void *ghoul2, int model, const char *name) { return 0; }
static qboolean GetMatrix(void *ghoul2, const int model, const int bolt, mdxaBone_t *matrix, const vec3_t angles,
    const vec3_t position, const int time, qhandle_t *models, vec3_t scale) {
  ++matrices; memset(matrix, 0, sizeof(*matrix));
  matrix->matrix[0][3] = 20; matrix->matrix[2][3] = 25; matrix->matrix[2][1] = -1; return qtrue;
}
#include "shared.h"
#include "effects.h"

static void Reset(void) {
  memset(&cg, 0, sizeof(cg)); memset(&cgs, 0, sizeof(cgs)); memset(cg_entities, 0, sizeof(cg_entities));
  memset(lightningDeflections, 0, sizeof(lightningDeflections)); memset(&snapshot, 0, sizeof(snapshot));
  cg.time = 1000; cg.snap = &snapshot; cg.predictedPlayerState.clientNum = 3; snapshot.ps.clientNum = 3;
  traces = sprites = arcs = sounds = lights = matrices = hidden = 0; wall = qfalse;
  lightningBudgetTime = lightningSoundBudgetTime = 0; lightningTraces = lightningEffects = lightningSounds = 0;
  imports.FX_AddElectricity = AddElectricity; imports.FX_AddSprite = AddSprite;
  imports.R_AddLightToScene = AddLight; imports.S_StartSound = StartSound;
  imports.G2API_HasGhoul2ModelOnIndex = HasModel; imports.G2API_AddBolt = AddBolt; imports.G2API_GetBoltMatrix = GetMatrix;
  for (int sound = 0; sound < ARRAY_LEN(cgs.media.forceLightningEnvironmentSounds); ++sound)
    cgs.media.forceLightningEnvironmentSounds[sound] = 1;
  cg_entities[1].currentValid = qtrue; cg_entities[1].currentState.number = 1;
  cg_entities[1].currentState.clientNum = 1; cg_entities[1].currentState.weapon = WP_SABER;
  cg_entities[1].currentState.eFlags2 = EF2_LIGHTNING_DEFLECT; cg_entities[1].ghoul2 = (void *)1;
  cgs.clientinfo[1].saber[0].blade[0].length = 40;
  cg_entities[0].currentState.number = 0; cg_entities[0].lerpOrigin[0] = 100;
  cg_entities[2].currentState.number = 2; cg_entities[2].lerpOrigin[0] = 100;
  FX_RecordLightningDeflection(1, 0);
}
int main(void) {
  vec3_t origin = {100, 0, DEFAULT_VIEWHEIGHT}; matrix3_t axis = {{-1,0,0}, {0,1,0}, {0,0,1}};
  int i, before;
  Reset(); cg_lightningEnvironment.integer = 0;
  CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  CHECK(arcs >= 5 && sprites > 0 && lights == 1 && sounds == 1);
  CHECK(firstArc.end[0] == 20 && firstArc.end[2] > 42 && firstArc.end[2] < 51); // Actual animated blade contact.
  CHECK(firstArc.start[0] == 100); CHECK(matrices == 1);
  before = arcs;
  for (i = 0; i < 20; ++i) CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  CHECK(arcs == before && matrices == 1); // Emission and Ghoul2 queries are independent of render FPS.
  CHECK(lights == 21); // Contact lighting stays visible on frames between bolt emissions.
  cg_entities[1].currentState.eFlags2 = 0;
  CHECK(!FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse)); // Cancellation ignores unexpired link.
  Reset(); FX_RecordLightningDeflection(1, 2);
  CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  CHECK(FX_ForceLightningDeflection(&cg_entities[2], origin, axis, qfalse)); CHECK(lights == 2);
  Reset(); cg.time += LIGHTNING_DEFLECT_HOLD_TIME + 101;
  CHECK(!FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  Reset(); cg.time = 500; CHECK(!FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse)); // Demo rewind.
  Reset(); hidden = 1; CHECK(!FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse)); CHECK(arcs == 0);
  Reset(); cg.predictedPlayerState.duelInProgress = qtrue; cg.predictedPlayerState.duelIndex = 2;
  CHECK(!FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  Reset(); cg.predictedPlayerState.clientNum = 1;
  CHECK(!FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse)); // Local prediction already cancelled.
  Reset(); wall = qtrue; CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  CHECK(lights == 0 && sounds == 0 && firstArc.end[0] == 60); // No saber corona through walls.
  Reset(); lightningTraces = LIGHTNING_TRACE_BUDGET; lightningEffects = LIGHTNING_EFFECT_BUDGET; lightningBudgetTime = cg.time;
  CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qtrue)); CHECK(traces == 0 && arcs == 0 && sprites == 0);
  Reset(); CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qtrue));
  CHECK(arcs > 5 && traces < LIGHTNING_TRACE_BUDGET && sprites < LIGHTNING_EFFECT_BUDGET);
  Reset(); cg_entities[1].modelScale[0] = 2;
  CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  CHECK(firstArc.end[2] > 60 && firstArc.end[2] < 76); // Contact follows the drawn player's blade scale.
  Reset(); cg_entities[1].currentState.eType = ET_NPC; cg_entities[1].modelScale[0] = 2;
  cg_entities[1].npcClient = &cgs.clientinfo[1];
  CHECK(FX_ForceLightningDeflection(&cg_entities[0], origin, axis, qfalse));
  CHECK(firstArc.end[2] > 42 && firstArc.end[2] < 51); // NPC blade rendering uses its existing unscaled length.
  puts("Passed"); return 0;
}
