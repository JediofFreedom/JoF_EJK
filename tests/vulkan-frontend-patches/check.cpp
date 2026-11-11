#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
typedef float vec3_t[3];
typedef int surfaceType_t;
const int MAX_GRID_SIZE=65,qfalse=0,qtrue=1,SF_GRID=3,h_low=0;
struct drawVert_t{vec3_t xyz,normal;};
struct srfGridMesh_s{int surfaceType,width,height,lodStitched;float *widthLodError,*heightLodError;drawVert_t verts[1];};
typedef srfGridMesh_s srfGridMesh_t;
struct surface_t{surfaceType_t *data;};
struct world_t{surface_t surfaces[2];int numsurfaces;};
static std::vector<void*> hunkAllocations;
static void *Hunk_Alloc(int size,int){void *data=std::malloc(size);CHECK(data);std::memset(data,0xcd,size);hunkAllocations.push_back(data);return data;}
static float R_ClampDenorm(float value){return value;}
static void R_FreeSurfaceGridMesh(srfGridMesh_t *grid){std::free(grid->widthLodError);std::free(grid->heightLodError);std::free(grid);}
static srfGridMesh_t *createGrid(int width,int height){auto *g=static_cast<srfGridMesh_t*>(std::calloc(1,sizeof(srfGridMesh_t)+(width*height-1)*sizeof(drawVert_t)));CHECK(g);g->surfaceType=SF_GRID;g->width=width;g->height=height;g->lodStitched=qtrue;g->widthLodError=static_cast<float*>(std::malloc(width*sizeof(float)));g->heightLodError=static_cast<float*>(std::malloc(height*sizeof(float)));return g;}
struct insertion_t{int calls,row,column;bool isColumn;vec3_t point;float error;} insertion;
static srfGridMesh_t *R_GridInsertColumn(srfGridMesh_t *grid,int column,int row,vec3_t point,float error){++insertion.calls;insertion.row=row;insertion.column=column;insertion.isColumn=true;std::memcpy(insertion.point,point,sizeof(vec3_t));insertion.error=error;return grid;}
static srfGridMesh_t *R_GridInsertRow(srfGridMesh_t *grid,int row,int column,vec3_t point,float error){++insertion.calls;insertion.row=row;insertion.column=column;insertion.isColumn=false;std::memcpy(insertion.point,point,sizeof(vec3_t));insertion.error=error;return grid;}
#include "patch-functions.h"
static void stitches(){
  int cases=0;
  for(int axis=0;axis<2;++axis)for(int targetAxis=0;targetAxis<2;++targetAxis)for(int edge=0;edge<2;++edge)for(int targetEdge=0;targetEdge<2;++targetEdge)for(int reverse=0;reverse<2;++reverse){
    auto *source=createGrid(3,3),*target=createGrid(2,2);
    auto *sourceVertices=source->verts,*targetVertices=target->verts;
    for(int i=0;i<3;++i){source->widthLodError[i]=float(10+i);source->heightLodError[i]=float(20+i);}
    for(int y=0;y<3;++y)for(int x=0;x<3;++x){auto &p=sourceVertices[y*3+x];p.xyz[0]=float(x);p.xyz[1]=float(y);p.xyz[2]=0;}
    for(int i=0;i<4;++i)for(int j=0;j<3;++j)targetVertices[i].xyz[j]=float(100+i*10+j);
    const auto atSource=[&](int step)->drawVert_t&{return sourceVertices[axis ? step*3+edge*2 : edge*6+step];};
    const auto atTarget=[&](int step)->drawVert_t&{return targetVertices[targetAxis ? step*2+targetEdge : targetEdge*2+step];};
    atTarget(0)=atSource(reverse?2:0);atTarget(1)=atSource(reverse?0:2);
    world_t world={{{reinterpret_cast<surfaceType_t*>(source)},{reinterpret_cast<surfaceType_t*>(target)}},2};
    insertion={};CHECK(R_StitchPatches(0,1,world)==qtrue);CHECK(insertion.calls==1);CHECK(insertion.isColumn==!targetAxis);CHECK(insertion.error==(axis?21.0f:11.0f));
    CHECK(!std::memcmp(insertion.point,atSource(1).xyz,sizeof(vec3_t)));CHECK(target->lodStitched==qfalse);CHECK(world.surfaces[1].data==reinterpret_cast<surfaceType_t*>(target));
    CHECK(insertion.row==(targetAxis?1:targetEdge));CHECK(insertion.column==(targetAxis?targetEdge:1));++cases;
    R_FreeSurfaceGridMesh(source);R_FreeSurfaceGridMesh(target);
  }
  std::printf("%d production patch stitches preserve midpoint LOD errors across both edge directions and orientations.\n",cases);
}
static void moves(){
  auto *source=createGrid(3,5);for(int i=0;i<3;++i)source->widthLodError[i]=float(10+i);for(int i=0;i<5;++i)source->heightLodError[i]=float(20+i);
  for(int i=0;i<15;++i){source->verts[i].xyz[0]=float(i);source->verts[i].normal[2]=1;}
  const void *oldWidth=source->widthLodError,*oldHeight=source->heightLodError;
  int otherSurface=99;world_t moved={{{reinterpret_cast<surfaceType_t*>(source)},{&otherSurface}},2};R_MovePatchSurfacesToHunk(moved);
  auto *copy=reinterpret_cast<srfGridMesh_t*>(moved.surfaces[0].data);CHECK(copy!=source&&copy->widthLodError!=oldWidth&&copy->heightLodError!=oldHeight);CHECK(copy->width==3&&copy->height==5);
  for(int i=0;i<3;++i)CHECK(copy->widthLodError[i]==float(10+i));for(int i=0;i<5;++i)CHECK(copy->heightLodError[i]==float(20+i));for(int i=0;i<15;++i)CHECK(copy->verts[i].xyz[0]==float(i)&&copy->verts[i].normal[2]==1);CHECK(moved.surfaces[1].data==&otherSurface);
  for(auto *allocation:hunkAllocations)std::free(allocation);
  std::puts("Production patch relocation retains both LOD-error arrays after temporary mesh destruction.");
}
int main(int argc,char **argv){
  if(argc==1||!std::strcmp(argv[1],"stitch"))stitches();
  if(argc==1||!std::strcmp(argv[1],"move"))moves();
}
