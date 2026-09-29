#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgame/cg_local.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

cg_t cg;
cgs_t cgs;
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static snapshot_t snapshot;
static localEntity_t puffs[12];
static int puffCount, grips, pushes, rightFetches, leftFetches;
static qboolean rightValid = qtrue;
static vec3_t leftOrigin = { 1, 2, 3 }, rightOrigin = { 4, 5, 6 };

static void TestPrint(const char *fmt, ...) { (void)fmt; }
static void NORETURN TestError(int code, const char *fmt, ...) { (void)code; (void)fmt; abort(); }
void (*Com_Printf)(const char *, ...) = TestPrint;
NORETURN_PTR void (*Com_Error)(int, const char *, ...) = TestError;

localEntity_t *CG_AllocLocalEntity(void)
{
	CHECK(puffCount < ARRAY_LEN(puffs));
	memset(&puffs[puffCount], 0, sizeof(puffs[puffCount]));
	return &puffs[puffCount++];
}
static qhandle_t RegisterShader(const char *name)
{
	CHECK(!strcmp(name, "gfx/effects/forcePush"));
	return 1;
}
static void SetMatrix(mdxaBone_t *matrix, const vec3_t origin)
{
	memset(matrix, 0, sizeof(*matrix));
	matrix->matrix[0][3] = origin[0];
	matrix->matrix[1][3] = origin[1];
	matrix->matrix[2][3] = origin[2];
}
static qboolean GetBoltMatrix(void *g2, const int model, const int bolt, mdxaBone_t *matrix,
	const vec3_t angles, const vec3_t position, const int time, qhandle_t *models, vec3_t scale)
{
	(void)g2; (void)angles; (void)position; (void)models; (void)scale;
	CHECK(model == 0 && time == cg.time);
	CHECK(bolt == 1 || bolt == 2);
	if (bolt == 2) ++rightFetches; else ++leftFetches;
	SetMatrix(matrix, bolt == 2 ? rightOrigin : leftOrigin);
	return bolt == 2 ? rightValid : qtrue;
}
void BG_GiveMeVectorFromMatrix(mdxaBone_t *matrix, int flags, vec3_t out)
{
	(void)matrix; (void)flags;
	VectorClear(out);
}
static void CG_ForceGripEffect(vec3_t org) { CHECK(VectorCompare(org, leftOrigin)); ++grips; }
static void CG_ForcePushBlur(vec3_t org, centity_t *cent) { (void)cent; CHECK(VectorCompare(org, leftOrigin)); ++pushes; }

#include "hand.h"

static void Draw(centity_t *cent, clientInfo_t *ci, qboolean visible, qboolean cachedRight)
{
	mdxaBone_t left, right;
	SetMatrix(&left, leftOrigin);
	SetMatrix(&right, rightOrigin);
	puffCount = grips = pushes = rightFetches = leftFetches = 0;
	DrawHandBlock(cent, ci, visible, qtrue, cachedRight, left, right);
	CHECK(leftFetches == 0); // preserve the existing left-hand matrix cache
}
static void CheckPuffs(int count, float scale, const vec3_t forward)
{
	int i;
	CHECK(puffCount == count && grips == 0 && pushes == 0);
	for (i = 0; i < count; ++i)
	{
		localEntity_t *p = &puffs[i];
		int life = p->endTime - p->startTime;
		CHECK(p->leType == LE_PUFF && p->refEntity.reType == RT_SPRITE);
		CHECK(p->startTime == cg.time && life >= 100 && life <= 150);
		CHECK(p->pos.trType == TR_LINEAR && p->pos.trTime == cg.time);
		CHECK(VectorCompare(p->pos.trBase, i < 6 ? leftOrigin : rightOrigin));
		CHECK(DotProduct(p->pos.trDelta, forward) > 40); // including upward/downward throws
		CHECK(p->color[0] >= 118 && p->color[0] <= 170);
		if (i % 3 == 2)
		{
			CHECK(p->refEntity.customShader == 2 && life == 100);
			CHECK(fabs(p->radius - 3.0f * scale) < 0.001f);
			CHECK(p->color[1] == 30 && p->color[2] == 30);
		}
		else
		{
			CHECK(p->refEntity.customShader == 1 && life >= 110);
			CHECK(p->radius >= 3.5f * scale && p->radius <= 5.0f * scale);
			CHECK(p->color[1] == 0 && p->color[2] == 0);
		}
	}
}
int main(void)
{
	centity_t cent = {0};
	clientInfo_t ci = {0};
	vec3_t forward;
	int pitch;
	imports.R_RegisterShader = RegisterShader;
	imports.G2API_GetBoltMatrix = GetBoltMatrix;
	cgs.media.redSaberGlowShader = 2;
	cg.snap = &snapshot;
	cg.time = 1000;
	cent.ghoul2 = &cent;
	ci.bolt_lhand = 1;
	ci.bolt_rhand = 2;
	cent.currentState.number = 1;
	cent.currentState.powerups = 1 << PW_DISINT_4;
	cent.currentState.forcePowersActive = DESTRUCTION_HAND_FLAG | (1 << FP_GRIP);
	for (pitch = -90; pitch <= 90; pitch += 45)
	{
		cent.lerpAngles[PITCH] = pitch;
		cent.lerpAngles[YAW] = 135;
		AngleVectors(cent.lerpAngles, forward, NULL, NULL);
		cent.currentState.weapon = WP_SABER;
		Draw(&cent, &ci, qtrue, qfalse);
		CheckPuffs(6, 1.0f, forward);
		CHECK(rightFetches == 0);
		cent.currentState.weapon = WP_MELEE;
		Draw(&cent, &ci, qtrue, qfalse);
		CheckPuffs(12, 1.15f, forward);
		CHECK(rightFetches == 1);
		Draw(&cent, &ci, qtrue, qtrue);
		CheckPuffs(12, 1.15f, forward);
		CHECK(rightFetches == 0);
	}
	ci.bolt_rhand = -1;
	Draw(&cent, &ci, qtrue, qfalse);
	CheckPuffs(6, 1.15f, forward);
	CHECK(rightFetches == 0);
	ci.bolt_rhand = 2;
	rightValid = qfalse;
	Draw(&cent, &ci, qtrue, qfalse);
	CheckPuffs(6, 1.15f, forward);
	rightValid = qtrue;
	Draw(&cent, &ci, qfalse, qfalse);
	CHECK(puffCount == 0 && grips == 0 && pushes == 0);
	cent.currentState.number = snapshot.ps.clientNum;
	cent.currentState.forcePowersActive = DESTRUCTION_HAND_FLAG; // own predicted state has no Grip
	Draw(&cent, &ci, qtrue, qfalse);
	CHECK(puffCount == 0 && grips == 0 && pushes == 0);
	cg.renderingThirdPerson = qtrue;
	Draw(&cent, &ci, qtrue, qfalse);
	CheckPuffs(12, 1.15f, forward);
	cent.currentState.powerups = 0;
	cent.bodyFadeTime = 1;
	Draw(&cent, &ci, qtrue, qfalse);
	CHECK(puffCount == 0 && grips == 0 && pushes == 0 && cent.bodyFadeTime == 0);
	cent.currentState.powerups = 1 << PW_DISINT_4;
	cent.currentState.forcePowersActive = 1 << FP_GRIP;
	Draw(&cent, &ci, qtrue, qfalse);
	CHECK(puffCount == 0 && grips == 2 && pushes == 0);
	cent.currentState.forcePowersActive = 0;
	Draw(&cent, &ci, qtrue, qfalse);
	CHECK(puffCount == 0 && grips == 0 && pushes == 1);
	puts("Destruction hand lifetime/direction/scale, marker priority, visibility, matrix reuse and stock FX checks passed.");
	return 0;
}
