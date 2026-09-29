#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef float vec3_t[3];
typedef vec3_t matrix3_t[3];
typedef int qboolean;
enum { qfalse = 0, ROLL = 2, CHAN_AUTO = 0, MASK_SOLID = 0x1001 };
typedef struct {
	struct { int number; } currentState;
	int lightningReferenceTime[5];
	vec3_t lightningReferenceEnd[5];
	int lightningReferenceSoundTime[5];
} centity_t;
typedef struct { float fraction; } trace_t;
static struct { int time, frametime; } cg;
static struct {
	struct { int forceLightningReference, forceLightningReferenceWide, forceLightningReferenceArc; } effects;
	struct { int forceLightningImpactSounds[3]; } media;
} cgs = { { 40, 41, 42 }, { { 11, 12, 13 } } };

static FILE *reference;
static int frame, randomCount, irandomCount, traceCount, hitMask, mainCount, expectedMain;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Frame %d, line %d: %s\n", frame, __LINE__, #x); exit(1); } } while (0)
#define VectorCopy(a,b) memcpy(b, a, sizeof(vec3_t))
static void VectorMA(const vec3_t a, float s, const vec3_t b, vec3_t out) {
	int i; for (i = 0; i < 3; i++) out[i] = a[i] + s * b[i];
}
static void VectorSubtract(const vec3_t a, const vec3_t b, vec3_t out) {
	int i; for (i = 0; i < 3; i++) out[i] = a[i] - b[i];
}
static void Tag(const char *tag) {
	char actual[32]; CHECK(fscanf(reference, "%31s", actual) == 1); CHECK(!strcmp(actual, tag));
}
static void Int(int actual) {
	int expected; CHECK(fscanf(reference, "%d", &expected) == 1); CHECK(actual == expected);
}
static void Vec(const vec3_t actual) {
	int i;
	for (i = 0; i < 3; i++) {
		float expected; CHECK(fscanf(reference, "%f", &expected) == 1);
		CHECK(fabsf(actual[i] - expected) < 0.001f);
	}
}
static int ReferenceRandom(void) {
	static const int values[] = { 0, 8192, 16384, 24576, 32767, 1234, 30000, 10000, 22222 };
	return values[randomCount++ % 9];
}
#define rand ReferenceRandom
static int Q_irand(int min, int max) {
	int result = min + (irandomCount++ * 137 + 73) % (max - min + 1);
	Tag("IRAND"); Int(min); Int(max); Int(result);
	return result;
}
static void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up) {
	float yaw = (float)(angles[1] * (3.14159265358979323846 * 2 / 360));
	float pitch = (float)(angles[0] * (3.14159265358979323846 * 2 / 360));
	CHECK(!right && !up); Tag("ANGLES"); Vec(angles);
	forward[0] = cosf(pitch) * cosf(yaw);
	forward[1] = cosf(pitch) * sinf(yaw);
	forward[2] = -sinf(pitch);
}
static void CG_Trace(trace_t *tr, vec3_t start, void *mins, void *maxs, vec3_t end, int owner, int mask) {
	CHECK(!mins && !maxs && owner == -1 && mask == MASK_SOLID);
	Tag("TRACE"); Vec(start); Vec(end);
	tr->fraction = hitMask & (1 << traceCount++) ? 0.25f : 1.0f;
}
static void PlayMain(int effect, vec3_t origin, matrix3_t axis, int a, int b, int c, int d) {
	CHECK(effect == expectedMain && a == -1 && b == -1 && c == -1 && d == -1);
	mainCount++;
}
static void PlayArc(int effect, vec3_t origin, vec3_t direction, int a, int b, qboolean portal) {
	CHECK(effect == 42 && a == -1 && b == -1 && !portal);
	Tag("FX"); Vec(origin); Vec(direction);
}
static void Sound(vec3_t origin, int entity, int channel, int sound) {
	Tag("SOUND"); Vec(origin); Int(entity); Int(channel); Int(sound);
}
static struct {
	void (*FX_PlayEntityEffectID)(int, vec3_t, matrix3_t, int, int, int, int);
	void (*FX_PlayEffectID)(int, vec3_t, vec3_t, int, int, qboolean);
	void (*S_StartSound)(vec3_t, int, int, int);
} api = { PlayMain, PlayArc, Sound }, *trap = &api;

#include "actual.h"

int main(int argc, char **argv) {
	centity_t cent;
	int reset, wide, i;
	vec3_t origin;
	matrix3_t axis = { { 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	char tag[32];
	CHECK(argc == 2);
	reference = fopen(argv[1], "r"); CHECK(reference);
	while (fscanf(reference, "%31s", tag) == 1) {
		frame++;
		CHECK(!strcmp(tag, "STEP"));
		CHECK(fscanf(reference, "%d%d%d%d%d%f%f%f%f%f%f", &reset, &wide, &cg.time, &cg.frametime, &hitMask,
			&origin[0], &origin[1], &origin[2], &axis[0][0], &axis[0][1], &axis[0][2]) == 11);
		if (reset) { memset(&cent, 0, sizeof(cent)); cent.currentState.number = 7; randomCount = irandomCount = 0; }
		traceCount = mainCount = 0;
		expectedMain = wide ? 41 : 40;
		FX_ForceLightningReference(&cent, origin, axis, wide);
		CHECK(mainCount == 1 && traceCount == (wide ? 5 : 2));
		for (i = 0; i < 5; i++) {
			Tag("STATE"); Int(i); Int(cent.lightningReferenceTime[i]);
			Vec(cent.lightningReferenceEnd[i]); Int(cent.lightningReferenceSoundTime[i]);
		}
		Tag("RANDOM"); Int(randomCount); Int(irandomCount);
	}
	CHECK(frame == 14);
	fclose(reference);
	printf("%d reference frames matched the original x86 lightning routine.\n", frame);
	return 0;
}
