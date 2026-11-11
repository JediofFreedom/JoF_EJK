#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v); std::exit(1); } } while (0)
#define ARRAY_LEN(v) (sizeof(v)/sizeof((v)[0]))
#include "capacity.h"
typedef unsigned char byte;
typedef int qhandle_t;
enum {MAX_QPATH = 64, PRINT_WARNING, h_low};
struct world_t { int loaded; };
struct modelHash_t { char name[MAX_QPATH]; int handle; modelHash_t *next; };
static struct { int loads,queries,warnings,allocations; std::vector<std::string> paths; } trace;
static void Com_sprintf(char *out,size_t length,const char *format,...) {
    va_list args; va_start(args,format); std::vsnprintf(out,length,format,args); va_end(args);
}
static int Q_stricmp(const char *a,const char *b) { return std::strcmp(a,b); }
static const char *va(const char *format,...) {
    static char out[128]; va_list args; va_start(args,format); std::vsnprintf(out,sizeof(out),format,args); va_end(args); return out;
}
static struct { static void Printf(int,const char *,...) { ++trace.warnings; } } ri;
static struct Cache {
    int GetModelHandle(const char *name) {
        ++trace.queries; int instance = 0;
        CHECK(std::sscanf(name,"*%d-0",&instance) == 1);
        return instance > 0 && instance <= MAX_SUB_BSP ? 100+instance : -1;
    }
} cache,*CModelCache = &cache;
static void RE_LoadWorldMap_Actual(const char *path,world_t &world,int index) {
    ++trace.loads; trace.paths.emplace_back(path); world.loaded = index;
}
namespace vulkan {
static struct { world_t bspModels[MAX_SUB_BSP]; int guard,numBSPModels; } tr;
#include "vulkan.h"
}
namespace vanilla {
static struct { world_t bspModels[MAX_SUB_BSP]; int guard,numBSPModels; } tr;
static const int FILE_HASH_SIZE = 1;
static modelHash_t models[MAX_SUB_BSP],*mhHashTable[FILE_HASH_SIZE];
static int generateHashValue(const char *,int) { return 0; }
#include "vanilla.h"
}
namespace rend2 {
static struct { world_t *bspModels[MAX_SUB_BSP]; int numBspModels; } tr;
static world_t s_worldData,pool[MAX_SUB_BSP+1];
static void *Hunk_Alloc(size_t size,int) { CHECK(size == sizeof(world_t)); ++trace.allocations; return &pool[trace.allocations-1]; }
#include "rend2.h"
}
template<class State> static void check(qhandle_t (*add)(const char *),State &state) {
    trace = {}; state = {}; state.guard = 0x12345678;
    CHECK(add("#same") == 101 && state.numBSPModels == 1 && state.bspModels[0].loaded == 1);
    CHECK(add("#same") == 102 && state.numBSPModels == 2 && state.bspModels[1].loaded == 2);
    for (int i = 2; i < MAX_SUB_BSP; ++i) CHECK(add("#same") == 101+i && state.numBSPModels == i+1);
    CHECK(state.bspModels[MAX_SUB_BSP-1].loaded == MAX_SUB_BSP && trace.loads == MAX_SUB_BSP);
    for (const auto &path : trace.paths) CHECK(path == "maps/same.bsp");
    const State unchanged = state; const auto previous = trace;
    CHECK(add("#overflow") == 0 && !std::memcmp(&state,&unchanged,sizeof(state)));
    CHECK(trace.loads == previous.loads && trace.queries == previous.queries);
    state = {}; CHECK(add("#new-level") == 101 && state.numBSPModels == 1);
}
namespace debug {
typedef float vec3_t[3];
enum {PITCH,YAW,ROLL};
enum {qfalse,qtrue};
struct Entity { int hModel,frame,oldframe,skinNum; float backlerp,origin[3],axis[3][3]; };
static struct {
    Entity testModelEntity; char testModelName[MAX_QPATH]; int testGun;
    struct { float vieworg[3],viewaxis[3][3],viewangles[3]; } refdef;
} cg;
static struct { float value; } cg_gunX = {2},cg_gunY = {3},cg_gunZ = {4};
static std::vector<std::string> arguments;
static int bspRegistrations,modelRegistrations,draws;
static const char *CG_Argv(size_t index) { return arguments[index].c_str(); }
static void Q_strncpyz(char *out,const char *in,size_t length) { std::snprintf(out,length,"%s",in); }
static void VectorMA(const float *a,float scale,const float *b,float *out) { for (int i = 0; i < 3; ++i) out[i] = a[i]+scale*b[i]; }
static void VectorCopy(const float *a,float *b) { std::memcpy(b,a,sizeof(vec3_t)); }
static void AnglesToAxis(const float *,float axis[3][3]) { for (int i = 0; i < 3; ++i) axis[i][i] = 1; }
static struct Trap {
    int Cmd_Argc() { return int(arguments.size()); }
    int R_RegisterModel(const char *name) { return !name[0] ? 0 : name[0] == '#' ? ++bspRegistrations : (++modelRegistrations,777); }
    void Print(const char *,...) {}
    void R_AddRefEntityToScene(Entity *entity) { CHECK(entity->hModel); ++draws; }
} imports,*trap = &imports;
#include "debug.h"
static void check() {
    CG_Init_CG(); arguments = {"testmodel","#map"}; CG_TestModel_f();
    CHECK(bspRegistrations == 1 && cg.testModelEntity.hModel == 1);
    for (int i = 0; i < 100; ++i) CG_AddTestModel();
    CHECK(bspRegistrations == 1 && cg.testModelEntity.hModel == 1 && draws == 100);
    arguments = {"testmodel","#map"}; CG_TestModel_f();
    CHECK(bspRegistrations == 2 && cg.testModelEntity.hModel == 2); // Explicit new command remains a new instance.
    cg.testGun = qtrue;
    for (int i = 0; i < 3; ++i) { cg.refdef.viewaxis[i][i] = 1; cg.refdef.vieworg[i] = float(10*(i+1)); }
    CG_AddTestModel();
    CHECK(bspRegistrations == 2 && cg.testModelEntity.origin[0] == 12 && cg.testModelEntity.origin[1] == 23 && cg.testModelEntity.origin[2] == 34);
    CG_Init_CG(); CHECK(!cg.testModelEntity.hModel && !cg.testModelName[0]);
    if (cg.testModelEntity.hModel) CG_AddTestModel();
    CHECK(bspRegistrations == 2);
    arguments = {"testmodel","models/example.md3"}; CG_TestModel_f();
    for (int i = 0; i < 100; ++i) CG_AddTestModel();
    CHECK(modelRegistrations == 101 && cg.testModelEntity.hModel == 777 && draws == 201);
    arguments = {"testmodel"}; CG_TestModel_f(); CHECK(!cg.testModelEntity.hModel);
}
}
int main(int argc,char **argv) {
    for (int i = 0; i < MAX_SUB_BSP; ++i) {
        auto &model = vanilla::models[i]; Com_sprintf(model.name,sizeof(model.name),"*%d-0",i+1);
        model.handle = 101+i; model.next = i ? &vanilla::models[i-1] : nullptr;
    }
    vanilla::mhHashTable[0] = &vanilla::models[MAX_SUB_BSP-1];
    if (argc == 2 && !std::strcmp(argv[1],"vanilla")) { check(vanilla::RE_RegisterBSP,vanilla::tr); return 0; }
    if (argc == 2 && !std::strcmp(argv[1],"debug")) { debug::check(); return 0; }
    check(vulkan::RE_RegisterBSP,vulkan::tr); check(vanilla::RE_RegisterBSP,vanilla::tr);
    trace = {}; rend2::tr = {};
    for (int i = 0; i < MAX_SUB_BSP; ++i) CHECK(rend2::RE_RegisterBSP("#same") == 101+i);
    CHECK(rend2::tr.numBspModels == MAX_SUB_BSP && trace.allocations == MAX_SUB_BSP);
    const auto queries = trace.queries;
    CHECK(!rend2::RE_RegisterBSP("#overflow") && rend2::tr.numBspModels == MAX_SUB_BSP);
    CHECK(trace.allocations == MAX_SUB_BSP && trace.queries == queries);
    debug::check();
    std::puts("Actual desktop BSP capacity, distinct instances and debug testmodel lifetime checks passed.");
}
