#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::exit(1);}++checks;}while(0)
static int checks;
typedef unsigned char byte;
#define Com_Memset std::memset
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PAD(x,a) (((x)+(a)-1)&~((a)-1))
#define PADP(x,a) ((void *)PAD(uintptr_t(x),a))
#define AVI_LINE_PADDING 4
#define LEVELSHOTSIZE 256
#define VK_NULL_HANDLE nullptr
static byte s_gammatable[256];
static struct{bool fboActive;struct{void*image;}capture;}vk;
static struct{bool deviceSupportsGamma;int vidWidth,vidHeight;}glConfig;
static struct{bool doGammaCorrectionWithShaders;}glConfigExt;
static struct{int captureWidth,captureHeight;}gls={32,24};
static struct{int integer;}quality;
static decltype(quality)*r_screenshotJpegQuality=&quality,*r_aviMotionJpegQuality=&quality;
static struct{int overbrightBits;struct{const char*baseName;}*world;}tr;
static std::vector<byte>saved;
static bool savedPNG,savedJPG;
static byte pixel;
static bool patterned;
static std::unordered_map<void*,byte*> allocations;
static void *Hunk_AllocateTempMemory(size_t size){byte*base=new byte[size+16];std::memset(base,0xE3,size+16);void*out=base+1;allocations[out]=base;return out;}
static void FreeTemp(void*ptr){auto found=allocations.find(ptr);CHECK(found!=allocations.end());delete[]found->second;allocations.erase(found);}
static struct Interface{
 void FS_WriteFile(const char*,const void*data,size_t size){const byte*p=(const byte*)data;saved.assign(p,p+size);}
 void Hunk_FreeTempMemory(void*p){FreeTemp(p);}
 void*Hunk_AllocateTempMemory(size_t size){return ::Hunk_AllocateTempMemory(size);}
 void CL_WriteAVIVideoFrame(const byte*data,size_t size){saved.assign(data,data+size);}
}ri;
static void vk_debug(const char*,...){}
static int Com_sprintf(char*dest,size_t size,const char*format,...){va_list args;va_start(args,format);int n=std::vsnprintf(dest,size,format,args);va_end(args);return n;}
#define MAX_OSPATH 256

static void RE_SavePNG(const char*,const byte*data,int width,int height,int channels){saved.assign(data,data+size_t(width)*height*channels);savedPNG=true;}
static void RE_SaveJPG(const char*,int,int width,int height,const byte*data,int){saved.assign(data,data+size_t(width)*height*3);savedJPG=true;}
static size_t RE_SaveJPGToBuffer(byte*output,size_t,int,int width,int height,const byte*data,int){std::memcpy(output,data,size_t(width)*height*3);return size_t(width)*height*3;}
struct videoFrameCommand_t{int commandId,width,height;byte*captureBuffer,*encodeBuffer;bool motionJpeg;};
static void vk_read_pixels(byte*data,unsigned width,unsigned height){
 if(patterned)for(size_t i=0;i<size_t(width)*height*3;++i)data[i]=byte(i*17+3);
 else std::memset(data,pixel,size_t(width)*height*3);
}
#include "capture.h"
static void reset(bool fbo,bool hardware,bool ssaa){
 vk.fboActive=fbo;vk.capture.image=ssaa?(void*)1:nullptr;glConfig.deviceSupportsGamma=hardware;glConfig.vidWidth=32;glConfig.vidHeight=24;glConfigExt.doGammaCorrectionWithShaders=false;
 saved.clear();savedPNG=savedJPG=false;patterned=false;tr.overbrightBits=1;
 for(int i=0;i<256;++i)s_gammatable[i]=byte(255-i);
 // readback numeric tests separately verify that raw FBO128 becomes127;
 // these caller tests start with that already corrected RGB handoff.
 pixel=fbo?127:128;
}
static void checkAll(size_t start,size_t size,byte expected){CHECK(saved.size()>=start+size);for(size_t i=0;i<size;++i)CHECK(saved[start+i]==expected);}
int main(int argc,char**argv){
 std::string part=argc>1?argv[1]:"all";
 struct World{const char*baseName;}world={"fixture"};
 for(bool fbo:{false,true})for(bool hardware:{false,true})for(bool ssaa:{false,true}){
  if(ssaa&&!fbo)continue;
  char name[]="capture";
  if(part=="all"||part=="tga"){reset(fbo,hardware,ssaa);R_TakeScreenshot(0,0,2,2,name);CHECK(saved.size()==30);checkAll(18,12,fbo?127:(hardware?127:128));}
  if(part=="all"||part=="jpeg"){reset(fbo,hardware,ssaa);R_TakeScreenshotJPEG(0,0,2,2,name);CHECK(savedJPG);checkAll(0,12,fbo?127:(hardware?127:128));}
  if(part=="all"||part=="png"){reset(fbo,hardware,ssaa);R_TakeScreenshotPNG(0,0,2,2,name);CHECK(savedPNG);checkAll(0,12,fbo?127:128);}
  if(part=="all"||part=="levelshot"){reset(fbo,hardware,ssaa);tr.world=(decltype(tr.world))&world;R_LevelShot();CHECK(saved.size()==18+256*256*3);checkAll(18,256*256*3,fbo?127:(hardware?127:128));}
  if(part=="all"||part=="video")for(bool mjpeg:{false,true}){
   reset(fbo,hardware,ssaa);byte input[12],encoded[16];std::memset(input,pixel,12);std::memset(encoded,0xE3,16);videoFrameCommand_t cmd={0,2,2,input,encoded,mjpeg};RB_TakeVideoFrameCmd(&cmd);
   if(mjpeg)checkAll(0,12,fbo?127:(hardware?127:128));
   else{CHECK(saved.size()==16);checkAll(0,6,fbo?127:(hardware?127:128));checkAll(8,6,fbo?127:(hardware?127:128));CHECK(saved[6]==0&&saved[7]==0&&saved[14]==0&&saved[15]==0);}
  }
 }
 if(part=="all"||part=="png"){reset(true,false,false);patterned=true;
 size_t offset=0;int pad=99;byte*raw=RB_ReadPixels(0,0,641,479,&offset,&pad,0);
 CHECK(offset!=0&&pad==0);CHECK((uintptr_t(raw+offset)%16)==0);FreeTemp(raw);
 char oddName[]="odd";R_TakeScreenshotPNG(0,0,641,479,oddName);CHECK(savedPNG&&saved.size()==size_t(641)*479*3);
 for(size_t i=0;i<saved.size();++i)CHECK(saved[i]==byte(i*17+3));}
 CHECK(allocations.empty());
 std::printf("%d actual capture caller checks passed\n",checks);
}
