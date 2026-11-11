#include "cgame/cg_local.h"
#include "cgame/cg_runtime_profile.h"
#include "game/surfaceflags.h"
#include "ghoul2/G2.h"
#include <stdlib.h>
#include <string.h>

vmCvar_t cg_footsteps;
static char calls[64];
static int callCount,failed;
static float waterFraction,groundFraction;
static int material,expectedSound,expectedEffect;
static footstepType_t expectedFoot;
static void Record(char call) {
    if (callCount >= (int)sizeof(calls)-1) { failed = 1; return; }
    calls[callCount++] = call; calls[callCount] = 0;
}
void VectorCopy(const vec3_t from,vec3_t to) { memcpy(to,from,sizeof(vec3_t)); }
void BG_GiveMeVectorFromMatrix(mdxaBone_t *matrix,int axis,vec3_t result) {
    if (axis != ORIGIN) failed = 1;
    result[0] = matrix->matrix[0][3]; result[1] = matrix->matrix[1][3]; result[2] = matrix->matrix[2][3];
}
static int ProfileFootstepRand(void) { Record('R'); return 2; }
static int AddBolt(void *model,int modelIndex,const char *name) {
    Record('B');
    if (modelIndex || strcmp(name,expectedFoot == FOOTSTEP_R || expectedFoot == FOOTSTEP_HEAVY_R ? "*r_leg_foot" : "*l_leg_foot")) failed = 1;
    return 7;
}
static qboolean GetBolt(void *model,const int modelIndex,const int bolt,mdxaBone_t *matrix,
    const vec3_t angles,const vec3_t position,const int time,qhandle_t *models,vec3_t scale) {
    Record('M');
    if (modelIndex || bolt != 7 || angles[0] || angles[1] != 42 || angles[2]) failed = 1;
    memset(matrix,0,sizeof(*matrix));
    matrix->matrix[0][3] = 1; matrix->matrix[1][3] = 2; matrix->matrix[2][3] = 3;
    return qtrue;
}
static void Trace(trace_t *trace,const vec3_t start,const vec3_t end,const vec3_t mins,const vec3_t maxs,
    clipHandle_t model,int mask,int capsule) {
    Record(mask == MASK_WATER ? 'W' : 'G');
    if (start[0] != 1 || start[1] != 2 || start[2] != 18 || end[0] != 1 || end[1] != 2 || end[2] != -14 ||
        mins[0] != -7 || mins[1] != -7 || maxs[0] != 7 || maxs[1] != 7 || mins[2] || maxs[2] != 2 || model || capsule ||
        (mask != MASK_WATER && mask != MASK_PLAYERSOLID)) failed = 1;
    memset(trace,0,sizeof(*trace));
    trace->fraction = mask == MASK_WATER ? waterFraction : groundFraction;
    trace->surfaceFlags = material;
    trace->endpos[0] = 1; trace->endpos[1] = 2; trace->endpos[2] = 2;
    trace->plane.normal[2] = 1;
}
static void Sound(const vec3_t origin,int entity,int channel,sfxHandle_t sound) {
    Record('S');
    if (origin || entity != 3 || channel != CHAN_BODY || sound != cgs.media.footsteps[expectedSound][2]) failed = 1;
}
static void Effect(int effect,vec3_t origin,vec3_t direction,int volume,int radius,qboolean portal) {
    Record('F');
    if (effect != expectedEffect || origin[2] != 2 || direction[2] != 1 || portal) failed = 1;
}
void CG_ImpactMark(qhandle_t shader,const vec3_t origin,const vec3_t dir,float orientation,float red,float green,float blue,float alpha,
    qboolean alphaFade,float radius,qboolean temporary) {
    int expectedShader = expectedFoot == FOOTSTEP_HEAVY_R ? cgs.media.fshrMarkShader :
        expectedFoot == FOOTSTEP_HEAVY_L ? cgs.media.fshlMarkShader :
        expectedFoot == FOOTSTEP_R ? cgs.media.fsrMarkShader : cgs.media.fslMarkShader;
    Record('D');
    if (shader != expectedShader || origin[2] != 2 || dir[2] != 1 || orientation != 42 || radius != 6 ||
        red != 1 || green != 1 || blue != 1 || alpha != 1 || alphaFade || temporary) failed = 1;
}
#include "footsteps.inc"

static int Run(centity_t *cent,int footsteps,int surface,footstepType_t foot,const char *expected) {
    cg_footsteps.integer = footsteps; material = surface; expectedFoot = foot;
    callCount = 0; calls[0] = 0;
    CG_PlayerFootsteps(cent,foot);
    return !failed && !strcmp(calls,expected);
}
int c_profile_footstep_adapter(void) {
    cgameImport_t saved = *trap;
    centity_t cent;
    int type,variant,result = 0;
    memset(&cent,0,sizeof(cent));
    cent.currentState.clientNum = 3; cent.pe.legs.yawAngle = 42;
    failed = 0; waterFraction = 1; groundFraction = .5f;
    trap->G2API_AddBolt = AddBolt; trap->G2API_GetBoltMatrix = GetBolt; trap->CM_Trace = Trace;
    trap->S_StartSound = Sound; trap->FX_PlayEffectID = Effect;
    for (type = 0; type < FOOTSTEP_TOTAL; ++type)
        for (variant = 0; variant < 4; ++variant) cgs.media.footsteps[type][variant] = 1000+type*4+variant;
    cgs.effects.footstepSand = expectedEffect = 31;
    cgs.media.fshrMarkShader = 41; cgs.media.fshlMarkShader = 42;
    cgs.media.fsrMarkShader = 43; cgs.media.fslMarkShader = 44;
    if (!Run(&cent,0,MATERIAL_DIRT,FOOTSTEP_R,"")) goto done;
    cent.currentState.NPC_class = CLASS_ATST;
    if (!Run(&cent,3,MATERIAL_DIRT,FOOTSTEP_R,"")) goto done;
    cent.currentState.NPC_class = CLASS_NONE;
    waterFraction = .5f;
    if (!Run(&cent,3,MATERIAL_DIRT,FOOTSTEP_R,"BMW")) goto done;
    waterFraction = 1; groundFraction = 1;
    if (!Run(&cent,3,MATERIAL_DIRT,FOOTSTEP_R,"BMWG")) goto done;
    groundFraction = .5f; expectedSound = FOOTSTEP_DIRTWALK;
    if (!Run(&cent,3,MATERIAL_DIRT,FOOTSTEP_R,"BMWGRSFD")) goto done;
    expectedSound = FOOTSTEP_METALRUN;
    if (!Run(&cent,3,MATERIAL_SOLIDMETAL,FOOTSTEP_HEAVY_L,"BMWGRS")) goto done;
    expectedSound = FOOTSTEP_SANDWALK;
    if (!Run(&cent,1,MATERIAL_SAND,FOOTSTEP_L,"BMWGRS")) goto done;
    expectedSound = FOOTSTEP_STONERUN;
    if (!Run(&cent,4,MATERIAL_ROCK,FOOTSTEP_HEAVY_R,"BMWGRSD")) goto done;
    result = 1;
done:
    *trap = saved;
    return result;
}
