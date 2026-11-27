#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
#define USE_VBO
typedef int qboolean;
enum { qfalse, qtrue };
enum { ERR_DROP = 1 };
enum { SHADER_MAX_VERTEXES = 1000, SHADER_MAX_INDEXES = 6 * SHADER_MAX_VERTEXES };
struct shaderStage_t;
struct shader_t {
    shader_t *remappedShader;
    qboolean isStaticShader, entityMergable;
    shaderStage_t **stages;
    int numUnfoggedPasses;
    float timeOffset, clampTime;
};
static struct {
    shader_t *shader, *originalShader;
    qboolean allowVBO;
    int fogNum, numIndexes, numVertexes, multiDrawPrimitives, numPasses, registration;
    shaderStage_t **xstages;
    float shaderTime;
    bool entityMergable, fading;
} tess;
static struct { struct { float floatTime; } refdef; } backEnd;
static int endSurfaces;
static void RB_EndSurface() { ++endSurfaces; }
static void Com_Error(int, const char *, ...) { throw std::runtime_error("overflow"); }
#include "batch-functions.h"
#include "restart-offenders.h"

static void fillBatch() { tess.numVertexes = SHADER_MAX_VERTEXES - 1; tess.numIndexes = 3; }

int main() {
    // Static shader remapped at runtime to another static, unremapped shader.
    shader_t target = {}; target.isStaticShader = qtrue; target.numUnfoggedPasses = 2;
    shader_t original = {}; original.isStaticShader = qtrue; original.remappedShader = &target; original.numUnfoggedPasses = 1;

    RB_BeginSurface(&original, 3);
    CHECK(tess.shader == &target && tess.originalShader == &original && tess.numPasses == 2);
    CHECK(!tess.allowVBO);

    // A tessellation flush in the middle of the batch must not put it back on the VBO.
    fillBatch();
    RB_CheckOverflow(4, 6);
    CHECK(endSurfaces == 1 && tess.numVertexes == 0 && tess.fogNum == 3);
    CHECK(tess.shader == &target && tess.originalShader == &original);
    CHECK(!tess.allowVBO);
    fillBatch();
    RB_CheckOverflow(4, 6);
    CHECK(endSurfaces == 2 && !tess.allowVBO && tess.shader == &target);

    // An unremapped static batch stays on the VBO across a flush.
    RB_BeginSurface(&target, 0);
    CHECK(tess.allowVBO && tess.originalShader == &target);
    fillBatch();
    RB_CheckOverflow(4, 6);
    CHECK(endSurfaces == 3 && tess.allowVBO && tess.shader == &target);

    // Clearing the remap lets the original shader use the VBO again.
    original.remappedShader = nullptr;
    RB_BeginSurface(&original, 0);
    CHECK(tess.allowVBO && tess.shader == &original);

    // The grid, face, VBO-flush and draw-buffer restarts are too entangled to
    // compile here; require that none of them restarts from the remap target.
    if (restartOffenders[0]) {
        std::fprintf(stderr, "batch restarts from tess.shader (the remap target) in: %s\n", restartOffenders);
        return 1;
    }

    std::puts("A remapped static batch keeps the world VBO off across tessellation flushes; unremapped batches keep it on.");
}
