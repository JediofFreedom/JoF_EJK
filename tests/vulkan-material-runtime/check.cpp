#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

typedef int qboolean;
const qboolean qfalse = 0, qtrue = 1;
typedef float vec3_t[3];
typedef float vec4_t[4];
#define RF_SETANIMINDEX 1
#define FUNCTABLE_SIZE 1024
#define FUNCTABLE_SIZE2 10
#define VectorSubtract(a,b,c) do { for (int q=0;q<3;++q) (c)[q]=(a)[q]-(b)[q]; } while(0)
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
static int Q_ftol(double value) { return static_cast<int>(value); }
static bool Q_isnan(float value) { return std::isnan(value); }
struct image_t { int id; } images[32];
struct textureBundle_t {
    image_t *image[32];
    int numImageAnimations, videoMapHandle;
    qboolean isVideoMap, isScreenMap, isLightmap, oneShotAnimMap;
    float imageAnimationSpeed;
};
struct entity_t { struct { int renderfx, skinNum; } e; } entity;
struct orientation_t { vec3_t origin, viewOrigin, axis[3]; float modelViewMatrix[16]; };
struct backend_t {
    entity_t *currentEntity;
    qboolean screenMapDone;
    orientation_t ori;
    struct { orientation_t ori; } viewParms;
} backEnd;
struct fog_t { float tcScale; qboolean hasSurface; vec4_t surface; } fog;
struct world_t { fog_t *fogs; } world;
struct {
    image_t *blackImage, *whiteImage;
    world_t *world;
} tr;
struct { int numVertexes, fogNum; double shaderTime; float xyz[4][4]; } tess;
struct cvar_t { float value; } fullbright;
static cvar_t *r_fullbright = &fullbright;
struct { int ctmu; struct { int color_descriptor; } screenMap; } vk;
const int VK_DESC_TEXTURE_BASE = 1;
static image_t *bound;
static int cinematicRuns, cinematicUploads, descriptorUpdates;
static void vk_bind(image_t *image) { bound = image; }
static void vk_update_descriptor(int, int) { ++descriptorUpdates; }
static void runCinematic(int) { ++cinematicRuns; }
static void uploadCinematic(int) { ++cinematicUploads; }
struct { void (*CIN_RunCinematic)(int); void (*CIN_UploadCinematic)(int); } ri = {runCinematic,uploadCinematic};
#include "animation.h"
#include "fog.h"

static void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static textureBundle_t makeBundle() {
    textureBundle_t b = {};
    for (int i=0;i<32;++i) { images[i].id=i; b.image[i]=&images[i]; }
    b.numImageAnimations=3;
    b.imageAnimationSpeed=1;
    return b;
}
static void select(textureBundle_t &b, int frame) {
    entity.e.skinNum=frame;
    bound=nullptr;
    R_BindAnimatedImage(&b);
}
static void animation(const char *name) {
    textureBundle_t b=makeBundle();
    backEnd.currentEntity=&entity;
    entity.e.renderfx=RF_SETANIMINDEX;
    if (!std::strcmp(name,"negative-loop") || !std::strcmp(name,"negative-one-shot")) {
        b.oneShotAnimMap=!std::strcmp(name,"negative-one-shot");
        select(b,-1);
        require(bound==&images[0],"negative explicit animation index must select first image");
        select(b,std::numeric_limits<int>::min());
        require(bound==&images[0],"minimum explicit animation index must select first image");
        return;
    }
    select(b,5); require(bound==&images[2],"positive animation must loop");
    b.oneShotAnimMap=qtrue;
    select(b,5); require(bound==&images[2],"one-shot animation must stop at last image");
    entity.e.renderfx=0; tess.shaderTime=-2.0;
    select(b,-7); require(bound==&images[0],"negative shader time must select first image");
    tess.shaderTime=1.0;
    select(b,-7); require(bound==&images[1],"time animation ignores entity index without flag");
    b.isVideoMap=qtrue;
    R_BindAnimatedImage(&b);
    require(cinematicRuns==1 && cinematicUploads==1,"video animation keeps cinematic service calls");
    b.isVideoMap=qfalse; b.isScreenMap=qtrue; tr.blackImage=&images[7];
    backEnd.screenMapDone=qfalse;
    R_BindAnimatedImage(&b); require(bound==tr.blackImage,"screenmap fallback stays black");
    backEnd.screenMapDone=qtrue;
    R_BindAnimatedImage(&b); require(descriptorUpdates==1,"completed screenmap uses descriptor");
    b.isScreenMap=qfalse; b.isLightmap=qtrue; fullbright.value=1; tr.whiteImage=&images[8];
    R_BindAnimatedImage(&b); require(bound==tr.whiteImage,"fullbright lightmap stays white");
    fullbright.value=0; b.numImageAnimations=1;
    select(b,-7); require(bound==&images[0],"single image fast path preserved");
}
static void fogCoordinates(const char *name) {
    tess.numVertexes=2;
    tr.world=&world; world.fogs=&fog;
    fog.tcScale=1;
    backEnd.ori.modelViewMatrix[10]=-1;
    for(int i=0;i<3;++i) backEnd.ori.axis[i][i]=1;
    backEnd.viewParms.ori.axis[0][0]=1;
    tess.xyz[0][2]=2; tess.xyz[1][2]=3;
    float st[4]={-5,-5,-5,-5};
    if (!std::strcmp(name,"fog-invalid-s")) fog.tcScale=std::numeric_limits<float>::quiet_NaN();
    if (!std::strcmp(name,"fog-invalid-t")) {
        fog.hasSurface=qtrue;
        fog.surface[0]=fog.surface[1]=1.0e38f;
        fog.surface[3]=2;
        tess.xyz[0][0]=1.0e38f;
        tess.xyz[0][1]=-1.0e38f;
    }
    RB_CalcFogTexCoords(st);
    if (!std::strcmp(name,"fog-invalid-s")) {
        require(st[0]==0 && st[2]==0,"invalid fog distance must be sanitized");
        require(st[1]==31.0f/32 && st[3]==31.0f/32,"invalid fog distance must preserve valid depth");
    } else if (!std::strcmp(name,"fog-invalid-t")) {
        require(st[0]==2.0f+1.0f/512 && st[2]==3.0f+1.0f/512,"valid fog distance must be preserved");
        require(st[1]==0 && st[3]==1.0f/32,"invalid fog depth must be sanitized independently");
    } else {
        require(st[0]==2.0f+1.0f/512 && st[2]==3.0f+1.0f/512,"finite fog distance unchanged");
        require(st[1]==31.0f/32 && st[3]==31.0f/32,"finite fog depth unchanged");
    }
}
int main(int argc,char **argv) {
    require(argc==2,"expected case name");
    if (std::strncmp(argv[1],"fog-",4)==0) fogCoordinates(argv[1]);
    else animation(argv[1]);
    return 0;
}
