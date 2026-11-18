#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
using qhandle_t=int;
const bool qtrue=true;
struct glconfig_t { int vidWidth,vidHeight,colorBits; } glConfig;
struct { int windowWidth,windowHeight; } gls;
struct refdef_t { int x,y,width,height; };
struct { int viewCluster; bool registered; refdef_t refdef; } tr;
struct stretchPicCommand_t { int commandId; void *shader; float x,y,w,h,s1,t1,s2,t2,a; } command;
using rotatePicCommand_t=stretchPicCommand_t;
enum { RC_STRETCH_PIC, RC_ROTATE_PIC, RC_ROTATE_PIC2 };
void R_Init() {} void R_SetTextureRegistration(bool) {} void R_ClearFlares() {} void RE_ClearScene() {}
bool failAllocation=false;
void *R_GetCommandBuffer(unsigned long long) { return failAllocation?nullptr:&command; }
void *R_GetShaderByHandle(int) { return nullptr; }
#include "production.h"
int main() {
 for (int multiplier: {1,2}) {
  gls={1920,1080}; glConfig={1920*multiplier,1080*multiplier,32};
  glconfig_t client={}; RE_BeginRegistration(&client);
  CHECK(client.vidWidth==1920 && client.vidHeight==1080 && client.colorBits==32);
  CHECK(glConfig.vidWidth==1920*multiplier && glConfig.vidHeight==1080*multiplier);
  int w=0,h=0; GetRealRes(&w,&h); CHECK(w==1920 && h==1080);
  // A fixed physical mouse delta traverses the same fraction of the menu.
  CHECK(96.0/(client.vidWidth/640.0)==32.0);
  // Console line width is identical at a given con_scale, with or without SSAA.
  CHECK(int(client.vidWidth/(2.0*8))-2==118);
  // 2D API coordinates already use the virtual 640x480 canvas.
  RE_StretchPic(10,20,30,40,0,0,1,1,0);
  CHECK(command.x==10 && command.y==20);
  CHECK(command.w==30 && command.h==40);
  RE_RotatePic(10,20,30,40,0,0,1,1,45,0); CHECK(command.w==30 && command.a==45);
  RE_RotatePic2(10,20,30,40,0,0,1,1,45,0); CHECK(command.h==40);
  refdef_t full={0,0,1920,1080}; SceneCoordinates(&full);
  CHECK(tr.refdef.width==glConfig.vidWidth && tr.refdef.height==glConfig.vidHeight);
  refdef_t partial={120,60,960,540}; SceneCoordinates(&partial);
  CHECK(tr.refdef.x==120*multiplier && tr.refdef.y==60*multiplier);
  CHECK(tr.refdef.width==960*multiplier && tr.refdef.height==540*multiplier);
 }
 // A custom render size can differ in aspect/scale, and adjacent edges agree.
 gls={1920,1080}; glConfig={2560,1440,32};
 refdef_t left={0,0,641,1080}; SceneCoordinates(&left); const int edge=tr.refdef.width;
 refdef_t right={641,0,1279,1080}; SceneCoordinates(&right);
 CHECK(tr.refdef.x==edge && tr.refdef.x+tr.refdef.width==2560);
 failAllocation=true; command.x=123; RE_StretchPic(0,0,0,0,0,0,1,1,0); CHECK(command.x==123);
 std::puts("Vulkan client/window scaling passed");
}
