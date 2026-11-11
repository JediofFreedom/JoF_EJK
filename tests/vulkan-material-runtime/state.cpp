#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
typedef unsigned char byte;
typedef byte color4ub_t[4];
typedef float vec2_t[2];
typedef int qboolean;
const qboolean qfalse=0,qtrue=1;
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define Vector4Set(a,b,c,d,e) do {(a)[0]=(b);(a)[1]=(c);(a)[2]=(d);(a)[3]=(e);} while(0)
#include "stage-layout.h"
struct { int numVertexes,numIndexes; unsigned indexes[SHADER_MAX_INDEXES]; stageVars_t svars; } tess;
#include "shadow-helpers.h"
#include "shadow.h"
static void require(bool value,const char *message) {
    if(!value) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
struct { float modelview_transform[16]; } vk_world;
struct { void *world; struct { int rdflags; } refdef; } tr;
struct { struct { int rdflags; float frametime; } refdef; struct { struct { float modelViewMatrix[16]; } world; } viewParms; } backEnd;
const int RDF_NOWORLDMODEL=1,RDF_SKYBOXPORTAL=2,PRINT_ALL=0;
static float published[16];
static int mvpUpdates;
static void Com_Memcpy(void *dst,const void *src,size_t count) { std::memcpy(dst,src,count); }
static void vk_update_mvp(const void *) { ++mvpUpdates; std::memcpy(published,vk_world.modelview_transform,64); }
struct Vector {
    float v[3];
    void Clear() { std::memset(v,0,sizeof(v)); }
    Vector &operator+=(const Vector &other) {for(int i=0;i<3;++i)v[i]+=other.v[i];return *this;}
};
static float VectorNormalize(float *v) {
    float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(length) for(int i=0;i<3;++i)v[i]/=length;
    return length;
}
static int cloudUpdates,cloudRenders,windUpdates;
struct Cloud {
    void Update() {++cloudUpdates;}
    void Render() {++cloudRenders; require(vk_world.modelview_transform[0]==7,"weather must render in world transform");}
};
struct Wind { bool mGlobal; Vector mCurrentVelocity; void Update(){++windUpdates;} };
static std::vector<Cloud> mParticleClouds;
static std::vector<Wind> mWindZones;
struct Outside {
    bool ready; int caches;
    bool Initialized() {return ready;}
    void Cache() {++caches;ready=true;}
} mOutside;
static float mMillisecondsElapsed,mSecondsElapsed,mGlobalWindSpeed;
static Vector mGlobalWindVelocity,mGlobalWindDirection;
static bool mFrozen;
static int mParticlesRendered;
static void print(int,const char *,...) {}
struct { void (*Printf)(int,const char *,...); } ri={print};
#include "weather.h"

static void shadow(bool capacity) {
    const int count=capacity?SHADER_MAX_VERTEXES:3;
    tess.numVertexes=count;
    std::memset(tess.svars.colors,0xa7,sizeof(tess.svars.colors));
    if(capacity) {
        for(int i=0;i<count;++i) R_AddEdgeDef(i,(i+1)%count,1);
    } else {
        R_AddEdgeDef(0,1,1);R_AddEdgeDef(1,0,1);
    }
    R_CalcShadowEdges();
    require(tess.numVertexes==count*2,"shadow must retain original and extruded vertices");
    for(size_t b=1;b<NUM_TEXTURE_BUNDLES;++b)
        for(size_t i=0;i<sizeof(tess.svars.colors[b]);++i)
            require(reinterpret_cast<byte *>(tess.svars.colors[b])[i]==0xa7,"shadow must not overwrite adjacent bundle colors");
    require(sizeof(tess.svars.colors[0])/sizeof(color4ub_t)>=size_t(count*2),"shadow upload must stay within its color array");
    std::vector<byte> upload(size_t(count)*2*4);
    std::memcpy(upload.data(),tess.svars.colors[0],upload.size());
    for(size_t i=0;i<upload.size();++i) require(upload[i]==(i%4==3?255:50),"every uploaded shadow color must be initialized");
    require(tess.numIndexes==(capacity?SHADER_MAX_INDEXES:0),"silhouette edge capacity and reverse pairing unchanged");
    for(int i=0;i<tess.numIndexes;++i) require(tess.indexes[i]<unsigned(count*2),"shadow indices must fit expanded vertices");
}
static void weather(const char *name) {
    tr.world=&tr; backEnd.refdef.frametime=20;
    for(int i=0;i<16;++i){vk_world.modelview_transform[i]=float(20+i);backEnd.viewParms.world.modelViewMatrix[i]=float(7+i);}
    float original[16];std::memcpy(original,vk_world.modelview_transform,64);
    mParticleClouds.resize(2);mWindZones.resize(1);mWindZones[0].mGlobal=true;
    mOutside.ready=std::strcmp(name,"weather-cache")!=0;
    if(!std::strcmp(name,"weather-bypass")) tr.refdef.rdflags=RDF_NOWORLDMODEL;
    RB_RenderWorldEffects();
    require(std::memcmp(original,vk_world.modelview_transform,64)==0,"weather must restore CPU modelview on every path");
    if(!std::strcmp(name,"weather-bypass")) {
        require(mvpUpdates==0 && cloudRenders==0 && mOutside.caches==0,"bypassed weather must leave state untouched");
    } else {
        require(mvpUpdates==2 && std::memcmp(original,published,64)==0,"weather must publish restored MVP");
        if(!std::strcmp(name,"weather-cache")) require(mOutside.caches==1 && cloudRenders==0,"initial cache path preserved");
        else require(cloudUpdates==2 && cloudRenders==2 && windUpdates==1,"normal cloud and wind updates preserved");
    }
}
int main(int argc,char **argv) {
    require(argc==2,"expected case");
    if(!std::strncmp(argv[1],"shadow-",7)) shadow(!std::strcmp(argv[1],"shadow-capacity"));
    else weather(argv[1]);
    return 0;
}
