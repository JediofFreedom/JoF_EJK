#include "cgame/cg_local.h"
#include "cm_types.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static int cg_numSolidEntities;
static centity_t *cg_solidEntities[MAX_ENTITIES_IN_SNAPSHOT];
clipMap_t cmg;
int c_traces;
static cmodel_t box_model;
static cplane_t *box_planes;
static cbrush_t *box_brush;
static cmodel_t fixtureModels[32];
static cplane_t fixturePlanes[256];
static cbrushside_t fixtureSides[128];
static cbrush_t fixtureBrushes[32];
static int fixtureLeafBrushes[32];
static CCMShader fixtureShaders[1];
static int tempCalls, transformedCalls, g2Calls;
static bool rejectG2;

static void TestError(int, const char *, ...) { CHECK(false); std::abort(); }

#include "primitives.h"

// The fixture uses one-brush leaves. All geometry/plane tests, trace setup,
// symmetric/asymmetric offsets, and transformed coordinates are production code.
cmodel_t *CM_ClipHandleToModel(clipHandle_t handle, clipMap_t **local) {
    *local=&cmg;
    CHECK(handle==BOX_MODEL_HANDLE || (handle>0 && handle<32));
    return handle==BOX_MODEL_HANDLE?&box_model:&fixtureModels[handle];
}
static void CM_TestInLeaf(traceWork_t *tw,trace_t &trace,cLeaf_t *leaf,clipMap_t *local) {
    CHECK(leaf->numLeafBrushes==1);
    cbrush_t *brush=&local->brushes[local->leafbrushes[leaf->firstLeafBrush]];
    if(brush->contents & tw->contents) CM_TestBoxInBrush(tw,trace,brush);
}
static void CM_TraceThroughLeaf(traceWork_t *tw,trace_t &trace,clipMap_t *local,cLeaf_t *leaf) {
    CHECK(leaf->numLeafBrushes==1);
    cbrush_t *brush=&local->brushes[local->leafbrushes[leaf->firstLeafBrush]];
    if(brush->contents & tw->contents) CM_TraceThroughBrush(tw,trace,brush,false);
}
static void CM_TestCapsuleInCapsule(traceWork_t *,trace_t &,clipHandle_t) { CHECK(false); }
static void CM_TestBoundingBoxInCapsule(traceWork_t *,trace_t &,clipHandle_t) { CHECK(false); }
static void CM_TraceCapsuleThroughCapsule(traceWork_t *,trace_t &,clipHandle_t) { CHECK(false); }
static void CM_TraceBoundingBoxThroughCapsule(traceWork_t *,trace_t &,clipHandle_t) { CHECK(false); }
static void CM_PositionTest(traceWork_t *,trace_t &) { CHECK(false); }
static void CM_TraceThroughTree(traceWork_t *,trace_t &,clipMap_t *,int,float,float,const vec3_t,const vec3_t) { CHECK(false); }
#include "transforms.h"

static clipHandle_t TempModel(const vec3_t mins,const vec3_t maxs,int capsule) {
    ++tempCalls;return CM_TempBoxModel(mins,maxs,capsule);
}
static void TransformedTrace(trace_t *out,const vec3_t start,const vec3_t end,const vec3_t mins,const vec3_t maxs,
    clipHandle_t model,int mask,const vec3_t origin,const vec3_t angles,int capsule) {
    ++transformedCalls;CM_TransformedBoxTrace(out,start,end,mins,maxs,model,mask,origin,angles,capsule);
}
static clipHandle_t InlineModel(int index) { CHECK(index>0 && index<32);return index; }
qboolean CG_G2TraceCollide(trace_t *trace,const vec3_t,const vec3_t,const vec3_t,const vec3_t) {
    ++g2Calls;if(rejectG2)trace->entityNum=ENTITYNUM_NONE;return qtrue;
}
#include "services.h"
#include "baseline-clip.h"
#include "subject.h"

static int pack(int radius,int down,int up) { return radius | (down<<8) | ((up+32)<<16); }
static void reset() {
    std::memset(&cg,0,sizeof(cg));std::memset(&cgs,0,sizeof(cgs));
    std::memset(cg_entities,0,sizeof(cg_entities));
    std::memset(&cmg,0,sizeof(cmg));std::memset(fixtureModels,0,sizeof(fixtureModels));
    std::memset(fixturePlanes,0,sizeof(fixturePlanes));std::memset(fixtureBrushes,0,sizeof(fixtureBrushes));
    cmg.planes=fixturePlanes;cmg.brushsides=fixtureSides;cmg.brushes=fixtureBrushes;
    cmg.leafbrushes=fixtureLeafBrushes;cmg.shaders=fixtureShaders;cmg.numNodes=1;
    CM_InitBoxHull();cg_numSolidEntities=0;rejectG2=false;
    cg.predictedPlayerState.clientNum=63;cg.predictedVehicleState.clientNum=62;
    tempCalls=transformedCalls=g2Calls=0;
}
static centity_t *addBox(int number,float x,float y,float z,int solid=0) {
    centity_t *ent=&cg_entities[number];ent->currentState.number=number;
    ent->currentState.eType=ET_PLAYER;ent->currentState.solid=solid?solid:pack(15,24,32);
    VectorSet(ent->lerpOrigin,x,y,z);cg_solidEntities[cg_numSolidEntities++]=ent;return ent;
}
static void addBrush(int number,const vec3_t origin,const vec3_t angles) {
    const int index=1;
    const vec3_t mins={-30,-50,-16},maxs={30,50,64};
    CM_TempBoxModel(mins,maxs,0);
    fixtureBrushes[index]=*box_brush;fixtureBrushes[index].sides=&fixtureSides[6];
    for(int i=0;i<6;++i) {
        fixtureSides[6+i]=box_brush->sides[i];
        fixturePlanes[12+i]=*box_brush->sides[i].plane;
        fixtureSides[6+i].plane=&fixturePlanes[12+i];
    }
    fixtureModels[index]=box_model;fixtureModels[index].leaf.firstLeafBrush=index;
    fixtureLeafBrushes[index]=index;
    centity_t *ent=addBox(number,origin[0],origin[1],origin[2],SOLID_BMODEL);
    ent->currentState.modelindex=index;ent->currentState.eType=ET_MOVER;
    VectorCopy(origin,ent->currentState.pos.trBase);VectorCopy(angles,ent->lerpAngles);
}
struct Counts { int temp,transformed,g2; };
static Counts compare(const vec3_t start,const vec3_t end,const vec3_t mins=NULL,const vec3_t maxs=NULL,
    int skip=ENTITYNUM_NONE,int skip2=ENTITYNUM_NONE,int mask=CONTENTS_BODY,qboolean g2=qfalse,
    qboolean crosshair=qfalse,float worldFraction=1.0f,trace_t *result=NULL,Counts *baselineCounts=NULL) {
    trace_t original={};original.fraction=worldFraction;
    original.entityNum=worldFraction<1?ENTITYNUM_WORLD:ENTITYNUM_NONE;
    VectorCopy(end,original.endpos);
    trace_t baseline=original,current=original;
    tempCalls=transformedCalls=g2Calls=0;
    CG_ClipMoveToEntities_Baseline(start,mins,maxs,end,skip,skip2,mask,&baseline,g2,crosshair);
    Counts old={tempCalls,transformedCalls,g2Calls};
    tempCalls=transformedCalls=g2Calls=0;
    CG_ClipMoveToEntities(start,mins,maxs,end,skip,skip2,mask,&current,g2,crosshair);
    CHECK(!std::memcmp(&baseline,&current,sizeof(trace_t)));
    CHECK(tempCalls<=old.temp && transformedCalls<=old.transformed && g2Calls==old.g2);
    if(result)*result=current;if(baselineCounts)*baselineCounts=old;
    return {tempCalls,transformedCalls,g2Calls};
}
int main() {
    Com_Error=TestError;
    imports.CM_TempModel=TempModel;imports.CM_TransformedTrace=TransformedTrace;imports.CM_InlineModel=InlineModel;
    const vec3_t zero={0,0,0},end={100,0,0};
    const vec3_t asymMin={-16,-9,-24},asymMax={8,20,32};
    reset();addBox(1,50,0,0);addBox(2,500,0,0);
    trace_t result;Counts old;
    Counts current=compare(zero,end,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qfalse,1,&result,&old);
    CHECK(result.entityNum==1 && result.fraction<1);CHECK(old.transformed==2 && current.transformed==1);
    compare(end,zero,asymMin,asymMax);compare(zero,end,asymMin,asymMax,1);
    compare(zero,end,NULL,asymMax);compare(zero,end,asymMin,NULL);
    compare(zero,end,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_SOLID);
    compare(zero,end,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qfalse,0.1f);
    const vec3_t inside={50,0,0},outside={90,0,0};
    compare(inside,inside,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qfalse,1,&result);
    CHECK(result.allsolid && result.startsolid && result.fraction==0);
    compare(inside,outside,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qfalse,1,&result);
    CHECK(result.startsolid && !result.allsolid);
    cg_entities[1].ghoul2=&cg_entities[1];compare(zero,end,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qtrue);
    rejectG2=true;compare(zero,end,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qtrue);
    // Near the face/margin, the optimized path must keep the exact same hit.
    for(float y:{14.875f,15.0f,15.125f,15.999f,16.001f}) {
        vec3_t a={0,y,0},b={100,y,0};compare(a,b);compare(b,a,asymMin,asymMax);
    }
    reset();const vec3_t brushOrigin={50,0,0},brushAngles={15,35,10};addBrush(70,brushOrigin,brushAngles);
    addBox(2,2000,0,0);current=compare(zero,end,asymMin,asymMax);
    CHECK(current.transformed==1);compare(end,zero,asymMin,asymMax);
    // Encoded boxes ignore their angle fields, while vehicle overrides are
    // evaluated before culling and restore their original orientation pointer.
    reset();centity_t *vehicle=addBox(70,550,0,0);
    vehicleInfo_t info={};Vehicle_t data={};vec3_t orientation={4,8,12};
    info.type=VH_FIGHTER;info.length=1100;info.width=100;info.height=80;
    data.m_pVehicleInfo=&info;data.m_vOrientation=orientation;vehicle->m_pVehicle=&data;
    vehicle->currentState.eType=ET_NPC;vehicle->currentState.NPC_class=CLASS_VEHICLE;
    compare(zero,end,asymMin,asymMax,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qfalse,1,&result);
    CHECK(result.fraction<1 && data.m_vOrientation==orientation);
    VectorSet(vehicle->lerpAngles,0,90,0);compare(zero,end,asymMin,asymMax);
    info.type=VH_WALKER;compare(zero,end,asymMin,asymMax);
    // Malformed/inverted extents retain the original collision path too.
    reset();addBox(1,2000,0,0,1);current=compare(zero,end);CHECK(current.transformed==1);
    addBox(2,1000,0,0,-1);addBox(3,1500,0,0,0x7fffffff);compare(zero,end);
    compare(zero,end,asymMax,asymMin);
    reset();addBox(1,50,0,0);addBox(2,75,0,0);
    cg_entities[0].currentState.eType=ET_PLAYER;cg_entities[0].currentState.m_iVehicleNum=1;
    cg_entities[1].currentState.eType=ET_NPC;cg_entities[1].currentState.NPC_class=CLASS_VEHICLE;
    cg_entities[1].currentState.m_iVehicleNum=1;
    compare(zero,end,asymMin,asymMax,0);
    cgs.serverMod=SVMOD_JAPRO;cg.predictedPlayerState.duelInProgress=qtrue;
    cg.predictedPlayerState.duelIndex=2;compare(zero,end);
    cg.predictedPlayerState.duelInProgress=qfalse;cg.predictedPlayerState.stats[STAT_RACEMODE]=1;
    compare(zero,end);compare(zero,end,NULL,NULL,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qtrue);

    std::mt19937 random(0x5a17);
    for(int scene=0;scene<100;++scene) {
        reset();
        for(int entity=1;entity<=32;++entity) {
            const int solid=scene%5==0?int(random()):pack(1+random()%80,random()%80,int(random()%100)-20);
            auto coordinate=[&](){return (int(random()%4096)-2048)*0.5f;};
            centity_t *ent=addBox(entity,coordinate(),coordinate(),coordinate(),solid);
            VectorSet(ent->lerpAngles,coordinate(),coordinate(),coordinate());
        }
        for(int move=0;move<60;++move) {
            vec3_t a,b,lo,hi;
            for(int axis=0;axis<3;++axis) {
                a[axis]=(int(random()%2048)-1024)*0.25f;
                b[axis]=move%9==0?a[axis]:(int(random()%2048)-1024)*0.25f;
                lo[axis]=-float(random()%48);hi[axis]=float(random()%48);
            }
            compare(a,b,move%3==0?NULL:lo,move%4==0?NULL:hi,move%11==0?3:ENTITYNUM_NONE,
                move%13==0?9:ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qfalse,move%6==0?0.25f:1.0f);
        }
    }
    reset();
    for(int entity=1;entity<=64;++entity)addBox(entity,entity==1?50.0f:1000.0f+entity*20,0,0);
    long baselineTraces=0,currentTraces=0;
    for(int frame=0;frame<1000;++frame) {
        current=compare(zero,end,asymMin,asymMax,ENTITYNUM_NONE,ENTITYNUM_NONE,CONTENTS_BODY,qfalse,qfalse,1,NULL,&old);
        baselineTraces+=old.transformed;currentTraces+=current.transformed;
    }
    CHECK(baselineTraces==64000 && currentTraces==1000);
    std::printf("Exact trace/winner equivalence passed: 6000 randomized sweeps plus edge, vehicle, rotated brush and startsolid fixtures.\n");
    std::printf("64-solid representative workload: transformed/temp traces %ld -> %ld (1000 sweeps).\n",baselineTraces,currentTraces);
}
