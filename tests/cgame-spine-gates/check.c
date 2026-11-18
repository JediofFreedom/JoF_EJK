#include "cgame/cg_local.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v) do { if (!(v)) { fprintf(stderr,"line %d: %s\n",__LINE__,#v); exit(1); } } while (0)

cgs_t cgs;
static cgameImport_t imports;
cgameImport_t *trap = &imports;
static unsigned classifierCalls,boltCalls;
static struct { int model,bolt,time; vec3_t angles,position,scale; } boltRequest;
static qboolean GetBolt(void *ghoul2,const int model,const int bolt,mdxaBone_t *matrix,
    const vec3_t angles,const vec3_t position,const int time,qhandle_t *models,vec3_t scale) {
    CHECK(ghoul2 == &imports && models == NULL);
    ++boltCalls;
    memset(&boltRequest,0,sizeof(boltRequest));
    boltRequest.model = model; boltRequest.bolt = bolt; boltRequest.time = time;
    VectorCopy(angles,boltRequest.angles); VectorCopy(position,boltRequest.position); VectorCopy(scale,boltRequest.scale);
    memset(matrix,0,sizeof(*matrix));
    matrix->matrix[0][0] = .8f; matrix->matrix[1][0] = .6f;
    matrix->matrix[0][1] = -.6f; matrix->matrix[1][1] = .8f;
    matrix->matrix[2][2] = 1;
    return qtrue;
}
#include "classifiers.h"
#include "ledge.h"
#define BG_G2ClientSpineAngles ReferenceSpine
#include "reference.h"
#undef BG_G2ClientSpineAngles
#include "subject.h"

typedef struct { vec3_t view,thoracic,upper,lower; float pitch,yaw; int correction; } Output;
typedef void (*Spine)(void *,int,vec3_t,vec3_t,entityState_t *,int,vec3_t,int,int,const vec3_t,vec3_t,vec3_t,vec3_t,vec3_t,float *,float *,int *);
static Output run(Spine function,entityState_t *entity,int ciLegs,int ciTorso,float seed) {
    vec3_t origin = {12,-9,30},lerpAngles = {17,-139,6},angles = {-33,114,8},scale = {.8f,1.1f,.9f};
    Output out;
    memset(&out,0,sizeof(out));
    VectorSet(out.view,seed,22,-8); VectorSet(out.thoracic,3,4,5);
    VectorSet(out.upper,6,7,8); VectorSet(out.lower,9,10,11);
    out.pitch = 7; out.yaw = 11; out.correction = 1200;
    function(&imports,7,origin,lerpAngles,entity,1000,out.view,ciLegs,ciTorso,angles,
        out.thoracic,out.upper,out.lower,scale,&out.pitch,&out.yaw,&out.correction);
    return out;
}
static uint64_t referenceWork,subjectWork,scenarios,corrections;
static void compare(entityState_t input,int ciLegs,int ciTorso,int requireCheap) {
    entityState_t before = input,after = input;
    Output oldOut,newOut;
    unsigned oldClassifiers,oldBolts;
    unsigned char oldRequest[sizeof(boltRequest)];
    classifierCalls = boltCalls = 0;
    memset(&boltRequest,0,sizeof(boltRequest));
    oldOut = run(ReferenceSpine,&before,ciLegs,ciTorso,(scenarios & 1) ? -0.0f : -31.5f);
    oldClassifiers = classifierCalls; oldBolts = boltCalls;
    memcpy(oldRequest,&boltRequest,sizeof(boltRequest));
    classifierCalls = boltCalls = 0;
    memset(&boltRequest,0,sizeof(boltRequest));
    newOut = run(BG_G2ClientSpineAngles,&after,ciLegs,ciTorso,(scenarios & 1) ? -0.0f : -31.5f);
    CHECK(!memcmp(&oldOut,&newOut,sizeof(oldOut)));
    CHECK(!memcmp(&before,&after,sizeof(before)) && !memcmp(&after,&input,sizeof(input)));
    CHECK(oldBolts == boltCalls && !memcmp(oldRequest,&boltRequest,sizeof(boltRequest)));
    CHECK(classifierCalls <= oldClassifiers);
    if (requireCheap) CHECK(classifierCalls == 0 && oldClassifiers > 0);
    referenceWork += oldClassifiers; subjectWork += classifierCalls;
    corrections += boltCalls; ++scenarios;
}
int main(void) {
    entityState_t entity;
    int anim,mode;
    imports.G2API_GetBoltMatrix_NoRecNoRot = GetBolt;
    memset(&entity,0,sizeof(entity));
    entity.legsAnim = BOTH_STAND1; entity.torsoAnim = BOTH_STAND2; entity.saberMove = LS_READY;
    compare(entity,BOTH_STAND1,BOTH_STAND2,0); // Live correction-enabled path.
    CHECK(corrections == 1);
    entity.torsoAnim = entity.legsAnim;
    compare(entity,BOTH_STAND1,BOTH_STAND2,1); // Current equal animations.
    entity.torsoAnim = BOTH_STAND2;
    compare(entity,BOTH_STAND1,BOTH_STAND1,1); // Previous equal animations.
    entity.eFlags = EF_DEAD;
    compare(entity,BOTH_STAND1,BOTH_STAND2,1);
    entity.eFlags = 0; entity.m_iVehicleNum = 9;
    compare(entity,BOTH_STAND1,BOTH_STAND2,1);
    entity.m_iVehicleNum = 0;
    // Exercise actual predicates across every supported animation, with current
    // and previous animation mismatches, cgame JA+ variants, death and vehicles.
    for (mode = 0; mode < 3; ++mode) {
        cgs.serverMod = mode == 0 ? SVMOD_JAPLUS : mode == 1 ? SVMOD_JAPRO : SVMOD_BASEJKA;
        for (anim = 0; anim < MAX_TOTALANIMATIONS; ++anim) {
            entity.legsAnim = anim; entity.torsoAnim = BOTH_STAND2;
            entity.saberMove = (anim & 1) ? LS_READY : LS_A_JUMP_T__B_;
            compare(entity,BOTH_STAND1,BOTH_STAND2,0);
            entity.legsAnim = BOTH_STAND1; entity.torsoAnim = anim;
            compare(entity,BOTH_STAND1,BOTH_STAND2,0);
            compare(entity,anim,BOTH_STAND2,0);
            compare(entity,BOTH_STAND1,anim,0);
            entity.legsAnim = entity.torsoAnim = anim;
            compare(entity,BOTH_STAND1,BOTH_STAND2,1);
            entity.legsAnim = BOTH_STAND1; entity.torsoAnim = anim;
            entity.eFlags = EF_DEAD; compare(entity,BOTH_STAND1,BOTH_STAND2,1);
            entity.eFlags = 0; entity.m_iVehicleNum = 42;
            compare(entity,BOTH_STAND1,BOTH_STAND2,1);
            entity.m_iVehicleNum = 0;
        }
    }
    CHECK(subjectWork < referenceWork && corrections > 100);
    printf("Spine correction: %llu exact-output scenarios, %llu bolt calls preserved; classification calls %llu -> %llu.\n",
        (unsigned long long)scenarios,(unsigned long long)corrections,
        (unsigned long long)referenceWork,(unsigned long long)subjectWork);
    return 0;
}
