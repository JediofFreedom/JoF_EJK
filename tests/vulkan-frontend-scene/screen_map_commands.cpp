#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
enum {qtrue=1,RC_DRAW_BUFFER=7,RC_OTHER=9};
struct drawSurfsCommand_t {int opaque;struct {bool needScreenMap,switchRenderPass;}refdef;};
static struct{int needScreenMap,numDrawSurfCmds;drawSurfsCommand_t *drawSurfCmd;}tr;
static int lastRenderCommand=RC_DRAW_BUFFER;
static drawSurfsCommand_t copied[4];
static size_t bytesAvailable,used;
static int allocationCalls;
static void *R_GetCommandBuffer(int bytes){++allocationCalls;CHECK(bytes>=0);if(size_t(bytes)>bytesAvailable-used)return nullptr;void *result=reinterpret_cast<char*>(copied)+used;used+=bytes;return result;}
#include "screen-map-commands.h"
static void reset(drawSurfsCommand_t *source,int count,size_t capacity){tr.needScreenMap=1;tr.numDrawSurfCmds=count;tr.drawSurfCmd=source;lastRenderCommand=RC_DRAW_BUFFER;used=0;allocationCalls=0;bytesAvailable=capacity;std::memset(copied,0xcd,sizeof(copied));for(int i=0;i<count;++i){source[i].opaque=40+i;source[i].refdef={false,false};}}
int main(){
  drawSurfsCommand_t source[3];
  reset(source,3,3*sizeof(copied[0]));copyScreenMapViews();CHECK(used==3*sizeof(copied[0]));CHECK(source[0].refdef.needScreenMap&&source[2].refdef.switchRenderPass);CHECK(!source[1].refdef.needScreenMap&&!source[1].refdef.switchRenderPass);CHECK(!tr.needScreenMap);
  for(int i=0;i<3;++i)CHECK(copied[i].opaque==40+i&&!copied[i].refdef.needScreenMap&&!copied[i].refdef.switchRenderPass);
  for(size_t capacity: {size_t(0),sizeof(copied[0]),2*sizeof(copied[0]),3*sizeof(copied[0])-1}){
    reset(source,3,capacity);copyScreenMapViews();CHECK(used==0&&allocationCalls==1&&!tr.needScreenMap);for(const auto &view:source)CHECK(!view.refdef.needScreenMap&&!view.refdef.switchRenderPass);
  }
  reset(source,0,0);copyScreenMapViews();CHECK(!allocationCalls&&!used&&!tr.needScreenMap);
  reset(source,3,3*sizeof(copied[0]));lastRenderCommand=RC_OTHER;copyScreenMapViews();CHECK(!allocationCalls&&!used&&!tr.needScreenMap);for(const auto &view:source)CHECK(!view.refdef.needScreenMap&&!view.refdef.switchRenderPass);
  std::puts("Production screen-map command duplication reserves complete batches and keeps original views on allocation failure.");
}
