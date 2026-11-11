#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define Q_min(a,b) std::min(a,b)
#define Q_max(a,b) std::max(a,b)
typedef unsigned char byte;
typedef int qhandle_t;
const int DEFAULT_MAX_POLYS=8,DEFAULT_MAX_POLYVERTS=24,HEIGHT_RESOLUTION=256,h_low=0;
struct srfPoly_t {int fields[4];};
struct polyVert_t {float xyz[3];};
struct backEndData_t {int unrelated[8];srfPoly_t *polys;polyVert_t *polyVerts;};
static backEndData_t *backEndData;
struct cvar_t {int integer;};
static cvar_t polygonSetting,vertexSetting;
static cvar_t *r_maxpolys=&polygonSetting,*r_maxpolyverts=&vertexSetting;
static int max_polys,max_polyverts;
static size_t allocated;
static void *Hunk_Alloc(size_t size,int){CHECK(size>=sizeof(backEndData_t)&&size<=sizeof(backEndData_t)+sizeof(srfPoly_t)*DEFAULT_MAX_POLYS+sizeof(polyVert_t)*DEFAULT_MAX_POLYVERTS);allocated=size;void *p=std::malloc(size);CHECK(p);return p;}
#include "polygon-allocation.h"
struct detail_t {qhandle_t shader=0;qhandle_t GetShader()const{return shader;}void SetShader(qhandle_t value){shader=value;}};
struct CTRLandScape {detail_t mHeightDetails[HEIGHT_RESOLUTION];void SetShaders(const int height,const qhandle_t shader);};
#include "terrain-functions.h"
static void polygons(){
  for(int polygons: {INT_MIN,-1,0,1,8,9,INT_MAX})for(int vertices: {INT_MIN,-1,0,1,3,24,25,INT_MAX}){
    polygonSetting.integer=polygons;vertexSetting.integer=vertices;allocatePolygons();
    CHECK(max_polys==std::max(0,std::min(polygons,DEFAULT_MAX_POLYS)));CHECK(max_polyverts==std::max(0,std::min(vertices,DEFAULT_MAX_POLYVERTS)));
    CHECK(allocated==sizeof(backEndData_t)+sizeof(srfPoly_t)*max_polys+sizeof(polyVert_t)*max_polyverts);
    CHECK(reinterpret_cast<byte*>(backEndData->polys)==reinterpret_cast<byte*>(backEndData)+sizeof(backEndData_t));CHECK(reinterpret_cast<byte*>(backEndData->polyVerts)==reinterpret_cast<byte*>(backEndData)+sizeof(backEndData_t)+sizeof(srfPoly_t)*max_polys);std::free(backEndData);
  }
  std::puts("Production renderer polygon allocation bounds negative/overflowing cvars and retains zero/default settings.");
}
static void terrain(){
  for(int height: {INT_MIN,-1,0,1,255,256,INT_MAX}){
    auto *terrain=new CTRLandScape;terrain->mHeightDetails[100].shader=9;terrain->SetShaders(height,4);
    for(int i=0;i<256;++i)CHECK(terrain->mHeightDetails[i].shader==(i==100?9: i>=std::max(0,height)?4:0));delete terrain;
  }
  CTRLandScape terrain;terrain.SetShaders(-1,0);for(const auto &entry:terrain.mHeightDetails)CHECK(entry.shader==0);
  std::puts("Production terrain altitude mapping bounds negative file heights and preserves prior/default shaders.");
}
int main(int argc,char **argv){if(argc==1||!std::strcmp(argv[1],"polygons"))polygons();if(argc==1||!std::strcmp(argv[1],"terrain"))terrain();}
