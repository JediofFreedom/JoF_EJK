#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while (0)
typedef int qboolean;
typedef int qhandle_t;
typedef float vec3_t[3];
enum { qfalse, qtrue, SF_POLY, PRINT_ALL, PRINT_DEVELOPER, PRINT_WARNING, ERR_DROP, RT_ENT_CHAIN=7, RT_MODEL=1, RT_MAX_MP_REF_ENTITY_TYPE=12, MAX_REFENTITIES=8 };
#define S_COLOR_YELLOW ""
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define VectorCopy(a,b) std::memcpy(b,a,sizeof(vec3_t))
#define VectorCompare(a,b) ((a)[0]==(b)[0]&&(a)[1]==(b)[1]&&(a)[2]==(b)[2])
#define LUMA(r,g,b) ((r)*0.2126f+(g)*0.7152f+(b)*0.0722f)
#define LERP(a,b,f) ((a)+((b)-(a))*(f))
struct polyVert_t { vec3_t xyz; float st[2]; unsigned char color[4]; };
struct srfPoly_t { int surfaceType,hShader,numVerts; polyVert_t *verts; int fogIndex; };
struct fog_t { vec3_t bounds[2]; };
struct world_t { int numfogs; fog_t *fogs; };
struct refEntity_t { int renderfx,reType; void *ghoul2; int opaque; };
struct CGhoul2Info { bool mModel; };
typedef std::vector<CGhoul2Info> CGhoul2Info_v;
struct dlight_t { vec3_t origin,origin2; float radius,color[3]; int additive; bool linear; };
struct backend_t { srfPoly_t polys[8]; polyVert_t polyVerts[24]; struct {refEntity_t e; qboolean lightingCalculated;} entities[MAX_REFENTITIES]; dlight_t dlights[16]; };
static backend_t storage, *backEndData=&storage;
static struct { bool registered; world_t *world; } tr;
static int r_numpolys,r_numpolyverts,r_numentities,r_numdlights;
static int max_polyverts=24,max_polys=8;
struct cvar_t { float value; };
static cvar_t saturation={1},grey={0},scale={1},intensityScale={1};
static cvar_t *r_dlightSaturation=&saturation,*r_mapGreyScale=&grey,*r_dlightScale=&scale,*r_dlightIntensity=&intensityScale;
static void print(int,const char*,...) {}
static struct {void(*Printf)(int,const char*,...);} ri={print};
static void Com_Error(int,const char*,...) {std::abort();}
static void AddPointToBounds(const vec3_t p,vec3_t lo,vec3_t hi) {for(int i=0;i<3;++i){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}}
#include "scene-functions.h"
static void reset(){std::memset(&storage,0,sizeof(storage));tr={true,nullptr};r_numpolys=r_numpolyverts=r_numentities=r_numdlights=0;max_polys=8;max_polyverts=24;scale.value=intensityScale.value=1;}
static bool near(float a,float b){return std::fabs(a-b)<0.000001f;}
static void polygons(){
  polyVert_t vertices[24]={};for(int i=0;i<24;++i){vertices[i].xyz[0]=float(i);vertices[i].color[0]=static_cast<unsigned char>(i);}
  reset();RE_AddPolyToScene(1,3,vertices,2);CHECK(r_numpolys==2&&r_numpolyverts==6);CHECK(!std::memcmp(storage.polyVerts,vertices,6*sizeof(vertices[0])));
  reset();RE_AddPolyToScene(1,24,vertices,1);CHECK(r_numpolys==1&&r_numpolyverts==24);CHECK(!std::memcmp(storage.polyVerts,vertices,sizeof(vertices)));
  reset();r_numpolyverts=21;RE_AddPolyToScene(1,3,vertices,2);CHECK(r_numpolys==1&&r_numpolyverts==24);
  for(int count: {INT_MIN,-3,-1,0,1,2,INT_MAX}){reset();RE_AddPolyToScene(1,count,vertices,1);CHECK(!r_numpolys&&!r_numpolyverts);}
  reset();RE_AddPolyToScene(1,3,nullptr,1);RE_AddPolyToScene(1,3,vertices,0);RE_AddPolyToScene(1,3,vertices,-1);CHECK(!r_numpolys);
  reset();r_numpolyverts=3;RE_AddPolyToScene(1,INT_MAX,vertices,1);CHECK(!r_numpolys&&r_numpolyverts==3);
  fog_t fogs[2]={};for(int i=0;i<3;++i){fogs[1].bounds[0][i]=-1;fogs[1].bounds[1][i]=30;}world_t world={2,fogs};reset();tr.world=&world;RE_AddPolyToScene(1,3,vertices,1);CHECK(storage.polys[0].fogIndex==1);
}
static void entities(){
  reset();RE_AddRefEntityToScene(nullptr);CHECK(!r_numentities);CGhoul2Info_v empty;refEntity_t entity={0,RT_MODEL,&empty,42};RE_AddRefEntityToScene(&entity);CHECK(r_numentities==1&&storage.entities[0].e.opaque==42);CHECK(!storage.entities[0].lightingCalculated);
}
static void lights(){
  const vec3_t start={1,2,3},end={2,3,4};
  for(float sat: {0.0f,0.25f,0.5f,1.0f})for(float unrelated: {-1.0f,0.0f,0.6f,1.0f}){
    reset();saturation.value=sat;grey.value=unrelated;scale.value=0.75f;intensityScale.value=0.8f;
    RE_AddDynamicLightToScene(start,100,1,0.25f,0.05f,0);RE_AddLinearLightToScene(start,end,100,1,0.25f,0.05f);CHECK(r_numdlights==2);
    CHECK(!storage.dlights[0].linear&&storage.dlights[1].linear);CHECK(near(storage.dlights[0].radius,storage.dlights[1].radius));
    for(int k=0;k<3;++k)CHECK(near(storage.dlights[0].color[k],storage.dlights[1].color[k]));
    RE_AddLinearLightToScene(start,start,100,1,0.25f,0.05f);CHECK(!storage.dlights[2].linear);for(int k=0;k<3;++k)CHECK(near(storage.dlights[0].color[k],storage.dlights[2].color[k]));
  }
}
int main(int argc,char **argv){
  if(argc==1||!std::strcmp(argv[1],"polygons"))polygons();
  if(argc==1||!std::strcmp(argv[1],"entities"))entities();
  if(argc==1||!std::strcmp(argv[1],"lights"))lights();
  std::puts("Production Vulkan polygon/entity bounds and point/linear light color equivalence passed.");
}
