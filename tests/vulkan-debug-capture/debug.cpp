#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <array>
#include <string>
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::exit(1);}++checks;}while(0)
static int checks;
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_LEN(x) (sizeof(x)/sizeof(*(x)))
#define Com_Memset std::memset
#define VectorCopy(a,b) do{(b)[0]=(a)[0];(b)[1]=(a)[1];(b)[2]=(a)[2];}while(0)
#define VectorMA(a,s,b,c) do{for(int vc=0;vc<3;++vc)(c)[vc]=(a)[vc]+(s)*(b)[vc];}while(0)
#define VectorSubtract(a,b,c) do{for(int vc=0;vc<3;++vc)(c)[vc]=(a)[vc]-(b)[vc];}while(0)
static void CrossProduct(const float*a,const float*b,float*c){c[0]=a[1]*b[2]-a[2]*b[1];c[1]=a[2]*b[0]-a[0]*b[2];c[2]=a[0]*b[1]-a[1]*b[0];}
static float DotProduct(const float*a,const float*b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static float VectorLength(const float*a){return std::sqrt(DotProduct(a,a));}
typedef float vec3_t[3];typedef unsigned char byte;typedef byte color4ub_t[4];
typedef float vec2_t[2];
typedef int qboolean;const qboolean qtrue=1,qfalse=0;
#include "capacity.h"
#define DEPTH_RANGE_NORMAL 0
#define DEPTH_RANGE_ZERO 1
#define TESS_XYZ 1
#define TESS_ST0 2
#define TESS_RGBA0 4
#define ERR_DROP 1
#define USE_VBO
struct shaderCommands_t {
 unsigned indexes[SHADER_MAX_INDEXES];float xyz[SHADER_MAX_VERTEXES*2][4];float normal[SHADER_MAX_VERTEXES][4];
 stageVars_t svars;
 int numVertexes,numIndexes,vbo_world_index;
}tess;
static struct {void*whiteImage;byte identityLightByte;}tr;
static struct {struct{unsigned normals_debug_pipeline,surface_debug_pipeline_solid,surface_debug_pipeline_outline;}std_pipeline;}vk;
static struct {struct{struct{float modelViewMatrix[16];}world;}viewParms;}backEnd;
static struct{void Error(int,const char*,...){throw std::runtime_error("bad count");}}ri;
struct Primitive{unsigned pipeline;bool indexed;std::vector<std::array<float,3>>points;std::vector<std::array<byte,4>>colors;};
static std::vector<Primitive>draws;
static unsigned pipeline;
static void vk_bind(void*){}
static void vk_bind_pipeline(unsigned value){pipeline=value;}
static void vk_bind_index(){CHECK(tess.numIndexes>=0&&tess.numIndexes<=SHADER_MAX_INDEXES);}
static void vk_bind_geometry(unsigned){CHECK(tess.numVertexes>=0&&tess.numVertexes<=int(ARRAY_LEN(tess.xyz))&&tess.numVertexes<=int(ARRAY_LEN(tess.svars.colors[0])));}
static void vk_draw_geometry(int,qboolean indexed){
 Primitive p;p.pipeline=pipeline;p.indexed=indexed!=0;
 int count=indexed?tess.numIndexes:tess.numVertexes;
 for(int i=0;i<count;++i){unsigned index=indexed?tess.indexes[i]:i;CHECK(index<unsigned(tess.numVertexes));
  p.points.push_back({{tess.xyz[index][0],tess.xyz[index][1],tess.xyz[index][2]}});
  p.colors.push_back({{tess.svars.colors[0][index][0],tess.svars.colors[0][index][1],tess.svars.colors[0][index][2],tess.svars.colors[0][index][3]}});
 }draws.push_back(p);
}
#include "debug.h"
static void reset(){
 tess={};tr.identityLightByte=127;vk.std_pipeline.normals_debug_pipeline=1;vk.std_pipeline.surface_debug_pipeline_solid=2;vk.std_pipeline.surface_debug_pipeline_outline=3;
 std::memset(tess.svars.colors[1],0xB7,sizeof(tess.svars.colors[1]));
 std::memset(tess.svars.colors[2],0xC3,sizeof(tess.svars.colors[2]));
 std::memset(tess.svars.texcoords,0xD1,sizeof(tess.svars.texcoords));
 backEnd={};backEnd.viewParms.world.modelViewMatrix[0]=backEnd.viewParms.world.modelViewMatrix[5]=backEnd.viewParms.world.modelViewMatrix[10]=backEnd.viewParms.world.modelViewMatrix[15]=1;
 draws.clear();
}
static void checkCanaries(){for(auto &c:tess.svars.colors[1])for(byte b:c)CHECK(b==0xB7);for(auto &c:tess.svars.colors[2])for(byte b:c)CHECK(b==0xC3);}
static void normalCases(){
 for(int count:{0,1,499,500,501,999,1000}){reset();tess.numVertexes=count;
  for(int i=0;i<count;++i)for(int c=0;c<3;++c){tess.xyz[i][c]=float(i*3+c);tess.normal[i][c]=float(c+1);}
  DrawNormals(&tess);checkCanaries();std::vector<std::array<float,3>>points;for(auto &d:draws){CHECK(d.pipeline==1&&d.indexed);points.insert(points.end(),d.points.begin(),d.points.end());for(auto &color:d.colors)for(byte b:color)CHECK(b==127);}
  CHECK(points.size()==size_t(count)*2);for(int i=0;i<count;++i)for(int c=0;c<3;++c){CHECK(points[i*2][c]==float(i*3+c));CHECK(points[i*2+1][c]==float(i*3+c+2*(c+1)));}
 }
 reset();tess.numVertexes=999;tess.vbo_world_index=1;DrawNormals(&tess);CHECK(draws.empty());
}
static void polygonCases(){
 for(int count:{3,4,499,500,501,999,1000,1001,2001}){reset();std::vector<float>points(size_t(count)*3);
  for(int i=0;i<count;++i){double angle=6.283185307179586*i/count;points[i*3]=float(std::cos(angle)*10);points[i*3+1]=float(std::sin(angle)*10);points[i*3+2]=1;}
  R_DebugPolygon(5,count,points.data());checkCanaries();std::vector<std::array<float,3>>solid,outline;
  for(auto &d:draws){auto &out=d.pipeline==2?solid:outline;out.insert(out.end(),d.points.begin(),d.points.end());for(auto &color:d.colors){if(d.pipeline==2)CHECK((color==std::array<byte,4>{{255,0,255,255}}));else for(byte b:color)CHECK(b==127);}}
  CHECK(solid.size()==size_t(count-2)*3&&outline.size()==size_t(count)*2);
  for(int i=1;i<count-1;++i)for(int c=0;c<3;++c){CHECK(solid[(i-1)*3][c]==points[c]);CHECK(solid[(i-1)*3+1][c]==points[i*3+c]);CHECK(solid[(i-1)*3+2][c]==points[(i+1)*3+c]);}
  for(int i=0;i<count;++i)for(int c=0;c<3;++c){CHECK(outline[i*2][c]==points[i*3+c]);CHECK(outline[i*2+1][c]==points[((i+1)%count)*3+c]);}
 }
 reset();float triangle[9]={0,0,-1,1,0,-1,0,1,-1};R_DebugPolygon(1,3,triangle);CHECK(draws.empty());R_DebugPolygon(1,2,triangle);CHECK(draws.empty());
}
int main(int argc,char**argv){std::string part=argc>1?argv[1]:"all";if(part=="all"||part=="normals")normalCases();if(part=="all"||part=="polygons")polygonCases();std::printf("%d actual debug geometry checks passed\n",checks);}
