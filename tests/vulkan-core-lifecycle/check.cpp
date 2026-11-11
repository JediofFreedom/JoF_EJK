#include "vulkan/vulkan_core.h"
#include "vk_sync2.h"
#include "vk_sample_counts.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } ++checks; } while(0)
static int checks;
typedef unsigned char byte;
typedef int qboolean;
const qboolean qfalse=0,qtrue=1;
#define NUM_COMMAND_BUFFERS 2
#define RENDER_PASS_MAIN 0
#define PRINT_WARNING 1
#define PRINT_DEVELOPER 2
#define ERR_FATAL 3
#define ERR_DROP 4
#define DEPTH_RANGE_COUNT 7
#define PADP(p,a) ((void *)((reinterpret_cast<uintptr_t>(p)+(a)-1)&~(uintptr_t((a)-1))))
#define Com_Memset std::memset
#define Com_Memcpy std::memcpy
#define MAX(a,b) std::max(a,int(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define PRINT_ALL 0
static int minimized, nativeBegin, transitions, passes, ends, submits, presents, resets, waits, restarts;
static int heapResets, colorCalls, drawCalls, clockMs, resized, copies, mapped, invalidated;
static VkResult fenceResult=VK_SUCCESS, presentResult=VK_SUCCESS, beginResult=VK_SUCCESS, endResult=VK_SUCCESS, submitResult=VK_SUCCESS;
static std::deque<VkResult> acquisitions;
static bool minimizeDuringAcquire;
static VkSubmitInfo lastSubmit;
static VkPhysicalDeviceProperties props;
struct Cvar { int integer; } sampleCvar;
static Cvar *r_ext_multisample=&sampleCvar;
static Cvar clearCvar;
static Cvar *r_clear=&clearCvar;
static Cvar bloomCvar,glowCvar;
static Cvar *r_bloom=&bloomCvar,*r_DynamicGlow=&glowCvar;
static VkSampleCountFlags colorSamples=127,depthSamples=127,glowSamples=127;
static VkResult formatResult=VK_SUCCESS;
struct Cmd {
  qboolean waitForFence,swapchain_image_acquired;
  uint32_t swapchain_image_index;
  VkFence rendering_finished_fence;
  VkSemaphore image_acquired;
  VkCommandBuffer command_buffer;
  VkPipeline last_pipeline;
  uint32_t vertex_buffer_offset,indirect_buffer_offset,buf_offset[4],vbo_offset[4];
  VkBuffer curr_index_buffer;
  VkDeviceSize curr_index_offset;
  uint32_t num_indexes; int depth_range;
  struct { VkDescriptorSet current[8]; uint32_t start,end,offset[8]; } descriptor_set;
  VkRect2D scissor_rect;
};
struct State {
 int frame_count,cmd_index; Cmd tess[2],*cmd;
 VkDevice device; VkPhysicalDevice physical_device; VkQueue queue; VkSwapchainKHR swapchain;
 VkImage swapchain_images[8]; qboolean swapchain_images_inited[8];
 VkSemaphore swapchain_rendering_finished[8]; VkImageLayout initSwapchainLayout;
 qboolean fboActive,bloomActive,msaaActive,synchronization2,blitEnabled,clearAttachment;
 uint32_t geometry_buffer_size_new; uint32_t renderWidth,renderHeight; float renderScaleX,renderScaleY;
 int renderPassIndex,screenMapSamples;
 struct { VkImage image; } capture;
 VkFormat capture_format,color_format,depth_format; VkImage color_image;
 struct { struct { VkRenderPass handle; } capture,gamma; } render_pass;
 struct { VkFramebuffer capture,gamma[8]; } framebuffers;
 VkPipeline capture_pipeline,gamma_pipeline;
 VkPipelineLayout pipeline_layout_post_process; VkDescriptorSet color_descriptor;
} vk;
static struct { struct {int groups_count;} ss; int needScreenMap; } tr;
static struct { bool screenMapDone; int screenshotMask; struct {int msec;} pc; bool projection2D,doneSurfaces,doneBloom; } backEnd;
static struct {uint32_t windowWidth,windowHeight; int captureWidth,captureHeight;} gls;
static struct {int stencilBits,maxActiveTextures;} glConfig;
static int vkSamples,vkMaxSamples;
static int log2pad(int value,int) { int result=1; while(result<=value/2) result*=2; return result; }
static struct Interface {
 bool VK_IsMinimized(){return minimized!=0;}
 void Printf(int,const char*,...){}
 void Error(int,const char*,...) { throw std::runtime_error("renderer error"); }
 int Milliseconds(){return clockMs++;}
 float Cvar_VariableValue(const char*){return 1;}
} ri;
struct Profile {void frame(){}} vkRuntimeProfile;
enum class RuntimeStage {GpuWait,Acquire,Present,Backend};
struct RuntimeProfileScope { explicit RuntimeProfileScope(RuntimeStage){} };
static const char *vk_result_string(VkResult){return "injected status";}
static void vk_debug(const char*,...){}
#include "vk_result_check.h"
static void R_FlushTextureJobs(){}
static void vk_flush_image_uploads(){}
static bool vk_find_screenmap_drawsurfs(){return false;}
static void vk_begin_screenmap_render_pass(){++passes;}
static void vk_begin_main_render_pass(){++passes;}
static void vk_begin_render_pass(VkRenderPass,VkFramebuffer,qboolean,uint32_t,uint32_t){++passes;}
static void vk_end_render_pass(){++ends;}
static void vk_bloom(){}
static void vk_resize_geometry_buffer(){++resized;vk.geometry_buffer_size_new=0;}
static void vk_restart_swapchain(const char*) {++restarts;for(auto &cmd:vk.tess){cmd.waitForFence=0;cmd.swapchain_image_acquired=0;}}
static VkResult qvkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){++waits;return fenceResult;}
static VkResult qvkResetFences(VkDevice,uint32_t,const VkFence*){++resets;return VK_SUCCESS;}
static VkResult qvkAcquireNextImageKHR(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t *index){
 CHECK(!acquisitions.empty());auto result=acquisitions.front();acquisitions.pop_front();
 if(minimizeDuringAcquire)minimized=1;
 *index=(result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)?2:UINT32_MAX;
 return result;
}
static VkResult qvkBeginCommandBuffer(VkCommandBuffer,const VkCommandBufferBeginInfo*){++nativeBegin;return beginResult;}
static VkResult qvkEndCommandBuffer(VkCommandBuffer){return endResult;}
static void vk_record_image_layout_transition(VkCommandBuffer,VkImage,VkImageAspectFlags,VkImageLayout,VkImageLayout,uint32_t,uint32_t){++transitions;}
static void qvkCmdBindPipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline){}
static void qvkCmdBindDescriptorSets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,uint32_t,uint32_t,const VkDescriptorSet*,uint32_t,const uint32_t*){}
static void qvkCmdDraw(VkCommandBuffer,uint32_t,uint32_t,uint32_t,uint32_t){}
static VkResult vk_queue_submit(VkQueue,uint32_t,const VkSubmitInfo *info,VkFence){++submits;lastSubmit=*info;return submitResult;}
static VkResult qvkQueuePresentKHR(VkQueue,const VkPresentInfoKHR*){++presents;return presentResult;}
// Capture mocks provide actual linear image bytes and record image barrier visibility.
static byte pixels[256];
static VkImageMemoryBarrier lastHostBarrier;
static VkPipelineStageFlags captureSourceStage,captureDestinationStage;
static VkResult qvkCreateImage(VkDevice,const VkImageCreateInfo*,const VkAllocationCallbacks*,VkImage *image){*image=(VkImage)42;return VK_SUCCESS;}
static void qvkGetImageMemoryRequirements(VkDevice,VkImage,VkMemoryRequirements *req){*req={};req->size=256;req->memoryTypeBits=1;}
static uint32_t vk_find_memory_type_lazy(uint32_t,VkMemoryPropertyFlags flags,VkMemoryPropertyFlags *found){*found=flags;return 0;}
static VkResult qvkBindImageMemory(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
static void allocateMemory(VkDeviceMemory *memory){*memory=(VkDeviceMemory)43;}
#define VK_ALLOCATE_MEMORY_CHECK(d,a,m,t) allocateMemory(m)
#define VK_FREE_MEMORY(d,m) do {} while(0)
static VkCommandBuffer vk_begin_command_buffer(){return (VkCommandBuffer)44;}
static void vk_end_command_buffer(VkCommandBuffer,const char*){}
static void qvkCmdBlitImage(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageBlit*,VkFilter){++copies;}
static void qvkCmdCopyImage(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageCopy*){++copies;}
static void qvkGetImageSubresourceLayout(VkDevice,VkImage,const VkImageSubresource*,VkSubresourceLayout *layout){*layout={};layout->rowPitch=8;}
static VkResult qvkMapMemory(VkDevice,VkDeviceMemory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void **data){++mapped;*data=pixels;return VK_SUCCESS;}
static VkResult qvkInvalidateMappedMemoryRanges(VkDevice,uint32_t,const VkMappedMemoryRange*){++invalidated;return VK_SUCCESS;}
static void qvkDestroyImage(VkDevice,VkImage,const VkAllocationCallbacks*){}
static void VKAPI_CALL CaptureBarrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier *barriers){
 CHECK(count==1);lastHostBarrier=barriers[0];captureSourceStage=source;captureDestinationStage=destination;
}
static PFN_vkCmdPipelineBarrier qvkCmdPipelineBarrier=CaptureBarrier;
static PFN_vkCmdPipelineBarrier2 qvkCmdPipelineBarrier2=nullptr;
#include "frame.h"
static VkResult qvkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice,VkFormat format,VkImageType,VkImageTiling,VkImageUsageFlags usage,VkImageCreateFlags,VkImageFormatProperties *out) {
 *out={};out->sampleCounts=format==VK_FORMAT_D24_UNORM_S8_UINT?depthSamples:((usage&VK_IMAGE_USAGE_SAMPLED_BIT)?glowSamples:colorSamples);
 return formatResult;
}
#include "samples.h"
// Minimal actual-command layouts keep padding/advancement distinctions visible.
enum {RC_END_OF_LIST,RC_SET_COLOR,RC_STRETCH_PIC,RC_ROTATE_PIC,RC_ROTATE_PIC2,RC_DRAW_SURFS,RC_DRAW_BUFFER,RC_SWAP_BUFFERS,RC_WORLD_EFFECTS,RC_AUTO_MAP,RC_VIDEOFRAME,RC_CLEARCOLOR};
struct setColorCommand_t {int id;float color[4];};
struct stretchPicCommand_t {int id;void *shader;float values[8];};
struct rotatePicCommand_t {int id;void *shader;float values[9];};
struct drawSurfsCommand_t {int id;byte payload[120];};
struct drawBufferCommand_t {int id;int buffer;};
struct swapBuffersCommand_t {int id;};
struct videoFrameCommand_t {int id;int width,height;byte *capture,*encode;bool mjpeg;};
struct clearColorCommand_t {int id;};
static qboolean rb_skipUnacquiredFrame;
static void ResetGhoul2RenderableSurfaceHeap(){++heapResets;}
static const void *RB_SetColor(const void *p){++colorCalls;return (const setColorCommand_t*)p+1;}
#define DRAW_STUB(name,type) static const void *name(const void *p){++drawCalls;return (const type*)p+1;}
DRAW_STUB(RB_StretchPic,stretchPicCommand_t)
DRAW_STUB(RB_RotatePic,rotatePicCommand_t)
DRAW_STUB(RB_RotatePic2,rotatePicCommand_t)
DRAW_STUB(RB_DrawSurfs,drawSurfsCommand_t)
DRAW_STUB(RB_TakeVideoFrameCmd,videoFrameCommand_t)
DRAW_STUB(RB_WorldEffects,drawBufferCommand_t)
const void *R_DrawWireframeAutomap(const void *p){++drawCalls;return (const drawBufferCommand_t*)p+1;}
DRAW_STUB(RB_ClearColor,clearColorCommand_t)
#define DEPTH_RANGE_NORMAL 0
typedef float vec4_t[4];
static void vk_set_depthrange(int){}
static void vk_clear_color_attachments(const vec4_t){++drawCalls;}
static const void *RB_SwapBuffers(const void *p){vk_end_frame();vk_present_frame();return (const swapBuffersCommand_t*)p+1;}
#include "backend.h"
static void reset(bool fbo=true){
 vk={};vk.cmd=&vk.tess[0];vk.fboActive=fbo;vk.cmd->command_buffer=(VkCommandBuffer)9;
 vk.color_format=VK_FORMAT_R8G8B8A8_UNORM;vk.depth_format=VK_FORMAT_D24_UNORM_S8_UINT;
 vk.cmd->image_acquired=(VkSemaphore)11;vk.cmd->rendering_finished_fence=(VkFence)12;
 vk.swapchain_rendering_finished[2]=(VkSemaphore)13;vk.capture_format=VK_FORMAT_R8G8B8A8_UNORM;
 backEnd={};tr={};gls={640,480,640,480};glConfig={};
 minimized=nativeBegin=transitions=passes=ends=submits=presents=resets=waits=restarts=0;
 heapResets=colorCalls=drawCalls=clockMs=resized=copies=mapped=invalidated=0;rb_skipUnacquiredFrame=0;
 fenceResult=presentResult=beginResult=endResult=submitResult=VK_SUCCESS;minimizeDuringAcquire=false;acquisitions.clear();lastSubmit={};
 lastHostBarrier={};captureSourceStage=captureDestinationStage=0;
}
static void expectError(void(*fn)()){bool caught=false;try{fn();}catch(const std::runtime_error&){caught=true;}CHECK(caught);}
static void beginCall(){vk_begin_frame();}
static void presentCall(){vk_present_frame();}
static void endCall(){vk_end_frame();}
static void captureInvalid(){byte out[12];vk_read_pixels(out,641,2);}
template<class T>static void append(std::vector<byte> &data,int id){size_t pos=(data.size()+sizeof(void*)-1)&~(sizeof(void*)-1);data.resize(pos+sizeof(T));std::memcpy(data.data()+pos,&id,sizeof(id));}
static void frameCases(){
 for(VkResult status:{VK_SUCCESS,VK_SUBOPTIMAL_KHR}){reset();acquisitions.push_back(status);CHECK(vk_begin_frame());CHECK(vk.cmd->swapchain_image_acquired);CHECK(nativeBegin==1&&transitions==1);vk_end_frame();CHECK(lastSubmit.waitSemaphoreCount==1&&lastSubmit.signalSemaphoreCount==1);vk_present_frame();CHECK(presents==1&&!vk.tess[0].swapchain_image_acquired);}
 for(VkResult status:{VK_TIMEOUT,VK_NOT_READY})for(bool fbo:{false,true}){
  reset(fbo);acquisitions.push_back(status);CHECK(bool(vk_begin_frame())==fbo);CHECK(!vk.cmd->swapchain_image_acquired);CHECK(vk.cmd->swapchain_image_index==0&&transitions==0);CHECK(nativeBegin==int(fbo));vk_end_frame();vk_present_frame();CHECK(presents==0);CHECK(submits==int(fbo));if(fbo)CHECK(lastSubmit.waitSemaphoreCount==0&&lastSubmit.signalSemaphoreCount==0);
 }
 for(bool fbo:{false,true}){reset(fbo);minimized=1;CHECK(bool(vk_begin_frame())==fbo);CHECK(transitions==0&&acquisitions.empty());vk_end_frame();vk_present_frame();CHECK(presents==0);}
 reset();acquisitions.push_back(VK_SUCCESS);CHECK(vk_begin_frame());minimized=1;vk_end_frame();CHECK(lastSubmit.waitSemaphoreCount==1&&lastSubmit.signalSemaphoreCount==1);vk_present_frame();CHECK(presents==1);
 reset();minimized=1;CHECK(vk_begin_frame());minimized=0;vk_end_frame();CHECK(lastSubmit.waitSemaphoreCount==0&&lastSubmit.signalSemaphoreCount==0);vk_present_frame();CHECK(presents==0);
 reset();acquisitions={VK_ERROR_OUT_OF_DATE_KHR,VK_SUCCESS};CHECK(vk_begin_frame());CHECK(restarts==1&&vk.cmd->swapchain_image_acquired);
 reset();acquisitions={VK_ERROR_OUT_OF_DATE_KHR};minimizeDuringAcquire=true;CHECK(vk_begin_frame());CHECK(restarts==0&&!vk.cmd->swapchain_image_acquired);
 for(VkResult result:{VK_TIMEOUT,VK_ERROR_DEVICE_LOST}){reset();vk.cmd->waitForFence=1;fenceResult=result;expectError(beginCall);CHECK(resets==0&&nativeBegin==0&&vk.cmd->waitForFence);}
 reset();vk.cmd->waitForFence=1;acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());CHECK(resets==1&&waits==1);
 reset();acquisitions={VK_SUCCESS};beginResult=VK_ERROR_DEVICE_LOST;expectError(beginCall);CHECK(submits==0);
 reset();acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());endResult=VK_ERROR_DEVICE_LOST;expectError(endCall);CHECK(submits==0&&!vk.cmd->waitForFence);
 reset();acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());submitResult=VK_ERROR_DEVICE_LOST;expectError(endCall);CHECK(submits==1&&!vk.cmd->waitForFence);
 reset();acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());CHECK(vk_begin_frame());CHECK(nativeBegin==1);
 reset();acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());vk.geometry_buffer_size_new=32;vk_end_frame();vk_present_frame();CHECK(resized==1&&submits==0&&presents==0&&vk.cmd->swapchain_image_acquired);CHECK(vk_begin_frame());CHECK(acquisitions.empty());
 for(bool min:{false,true}){reset();acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());vk_end_frame();minimized=min;presentResult=VK_ERROR_OUT_OF_DATE_KHR;vk_present_frame();CHECK(presents==1&&restarts==int(!min));}
 reset();acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());vk_end_frame();presentResult=VK_ERROR_DEVICE_LOST;expectError(presentCall);
}
static void backendCase(){
 reset(false);minimized=1;std::vector<byte> commands;
 append<drawBufferCommand_t>(commands,RC_DRAW_BUFFER);append<setColorCommand_t>(commands,RC_SET_COLOR);
 append<stretchPicCommand_t>(commands,RC_STRETCH_PIC);append<rotatePicCommand_t>(commands,RC_ROTATE_PIC);append<rotatePicCommand_t>(commands,RC_ROTATE_PIC2);
 append<drawSurfsCommand_t>(commands,RC_DRAW_SURFS);append<drawBufferCommand_t>(commands,RC_WORLD_EFFECTS);append<drawBufferCommand_t>(commands,RC_AUTO_MAP);
 append<videoFrameCommand_t>(commands,RC_VIDEOFRAME);append<clearColorCommand_t>(commands,RC_CLEARCOLOR);append<swapBuffersCommand_t>(commands,RC_SWAP_BUFFERS);
 append<int>(commands,RC_END_OF_LIST);RB_ExecuteRenderCommands(commands.data());CHECK(colorCalls==1&&drawCalls==0&&heapResets==1&&submits==0);
 minimized=0;acquisitions={VK_SUCCESS};CHECK(vk_begin_frame());CHECK(nativeBegin==1);
}
static void sampleCases(){
 reset();props={};props.limits.framebufferColorSampleCounts=VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_4_BIT;
 props.limits.framebufferDepthSampleCounts=VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_2_BIT|VK_SAMPLE_COUNT_4_BIT;
 props.limits.framebufferStencilSampleCounts=VK_SAMPLE_COUNT_1_BIT;
 props.limits.sampledImageColorSampleCounts=props.limits.sampledImageDepthSampleCounts=127;
 for(int stencil:{0,8})for(int request:{0,2,4,8,64}){
  sampleCvar.integer=request;glConfig.stencilBits=stencil;vk.msaaActive=request!=0;
  ActualSampleSelection();int expected=request>=4&&!stencil?4:1;
  CHECK(vkSamples==expected&&vk.msaaActive==(expected>1));CHECK(vkMaxSamples==(stencil?1:4));CHECK(vk.screenMapSamples==(stencil?1:4));
 }
 glConfig.stencilBits=0;sampleCvar.integer=8;props.limits.framebufferColorSampleCounts=props.limits.framebufferDepthSampleCounts=127;
 colorSamples=VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_4_BIT;depthSamples=VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_2_BIT;
 vk.msaaActive=1;ActualSampleSelection();CHECK(vkSamples==1&&!vk.msaaActive&&vk.screenMapSamples==1);
 depthSamples=127;vk.msaaActive=1;ActualSampleSelection();CHECK(vkSamples==4&&vk.screenMapSamples==4);
 glowCvar.integer=1;glConfig.maxActiveTextures=4;glowSamples=1;vk.msaaActive=1;ActualSampleSelection();CHECK(vkSamples==1&&!vk.msaaActive);
 glowCvar.integer=0;formatResult=VK_ERROR_FORMAT_NOT_SUPPORTED;vk.msaaActive=1;ActualSampleSelection();CHECK(vkSamples==1&&!vk.msaaActive);
 formatResult=VK_SUCCESS;colorSamples=depthSamples=glowSamples=127;
 for(uint32_t mask=1;mask<128;mask+=2)for(uint32_t request=0;request<=65;++request){
  uint32_t expected=1;for(uint32_t n=2;n<=64;n*=2)if((mask&n)&&n<=request)expected=n;
  CHECK(VK_SelectSampleCount(mask,request)==expected);
 }
}
static void captureCases(){
 byte out[12];reset();std::memset(out,0xAA,sizeof(out));vk_read_pixels(out,2,2);CHECK(waits==0&&copies==0);for(auto c:out)CHECK(c==0);
 reset(false);vk.cmd->waitForFence=1;std::memset(out,0xAA,sizeof(out));vk_read_pixels(out,2,2);CHECK(waits==0&&copies==0);for(auto c:out)CHECK(c==0);
 reset();vk_read_pixels(nullptr,2,2);vk_read_pixels(out,0,2);CHECK(waits==0&&copies==0);expectError(captureInvalid);CHECK(copies==0);
 reset();vk.cmd->waitForFence=1;vk.blitEnabled=1;
 for(int i=0;i<16;++i)pixels[i]=byte(i);
 vk_read_pixels(out,2,2);CHECK(waits==1&&copies==1&&mapped==1);
 CHECK(captureSourceStage==VK_PIPELINE_STAGE_TRANSFER_BIT&&captureDestinationStage==VK_PIPELINE_STAGE_HOST_BIT);
 CHECK(lastHostBarrier.srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT&&lastHostBarrier.dstAccessMask==VK_ACCESS_HOST_READ_BIT&&lastHostBarrier.newLayout==VK_IMAGE_LAYOUT_GENERAL);
 const byte expected[]={8,9,10,12,13,14,0,1,2,4,5,6};CHECK(std::memcmp(out,expected,12)==0);
}
int main(int argc,char **argv){
 std::string part=argc>1?argv[1]:"all";
 if(part=="all"||part=="frame")frameCases();
 if(part=="all"||part=="backend")backendCase();
 if(part=="all"||part=="samples")sampleCases();
 if(part=="all"||part=="capture")captureCases();
 std::printf("%d actual core lifecycle checks passed (%s)\n",checks,part.c_str());
}
