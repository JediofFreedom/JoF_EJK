#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include "scale.h"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}++checks;}while(0)
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define VK_SET_OBJECT_NAME(...) do{}while(0)
#define Com_Memset std::memset
#define Com_Memcpy std::memcpy
#define PRINT_WARNING 1
#define ERR_FATAL 2
#define PV_NONE 0
#define DEPTH_RANGE_COUNT 7
#define RENDER_PASS_MAIN 0
#define RENDER_PASS_REFRACTION 1
#define RENDER_PASS_SCREENMAP 2
#define RENDER_PASS_POST_BLEND 3
#define RENDER_PASS_DGLOW 4
typedef float vec4_t[4];typedef int qboolean;const qboolean qtrue=1,qfalse=0;
static int checks,clearCalls,blits,copies;
static uint64_t nextHandle;
static struct {int vidWidth,vidHeight,stencilBits;}glConfig;
static struct {int captureWidth,captureHeight,windowWidth,windowHeight;}gls;
static struct {int integer;}fboSetting,ssaaSetting;
static auto *r_fbo=&fboSetting,*r_ext_supersample=&ssaaSetting;
static int vkSamples=1;
struct Pass{VkRenderPass handle;};
struct PostPass{Pass extract,blend,blur[8];};
struct PostFramebuffer{VkFramebuffer extract,blur[8];};
struct Command{VkCommandBuffer command_buffer;uint32_t swapchain_image_index;int depth_range;}command;
static struct {
 bool active,fboActive,bloomActive,dglowActive,refractionActive,msaaActive;
 uint32_t screenMapSamples,screenMapWidth,screenMapHeight,swapchain_image_count;
 VkDevice device;Command *cmd;VkFormat bloom_format,color_format,capture_format;
 VkImage color_image,depth_image,msaa_image,refraction_extract_image,dglow_msaa_image;
 VkImageView color_image_view,depth_image_view,msaa_image_view,refraction_extract_image_view,dglow_msaa_image_view;
 VkImage bloom_image[9],dglow_image[9];VkImageView bloom_image_view[9],dglow_image_view[9];
 VkDeviceMemory image_memory[26];uint32_t image_memory_count;
 VkImageView swapchain_image_views[8];
 struct {VkImage image;VkImageView image_view;}capture;
 struct {VkImage color_image,color_image_msaa,depth_image;VkImageView color_image_view,color_image_view_msaa,depth_image_view;}screenMap;
 struct {Pass main,screenmap,gamma,capture;struct {Pass extract;}refraction;PostPass bloom,dglow;}render_pass;
 struct {VkFramebuffer main[8],gamma[8],screenmap,capture;PostFramebuffer bloom,dglow;}framebuffers;
 uint32_t renderWidth,renderHeight;float renderScaleX,renderScaleY;int renderPassIndex;
}vk;
static struct {int dirty_depth_attachment;}vk_world;
static struct {bool projection2D;struct {int viewportX,viewportY,viewportWidth,viewportHeight,portalView,scissorX,scissorY,scissorWidth,scissorHeight;}viewParms;}backEnd;
static struct {vec4_t clearColor;}tr;
static struct {void Error(int,const char*,...){throw std::runtime_error("renderer error");}}ri;
static void vk_debug(const char*,...){}
static const char *va(const char*,...){return "name";}
static const char *vk_result_string(VkResult){return "driver result";}
#include "vk_result_check.h"
struct Extent{uint32_t width,height;};
static std::map<VkImage,Extent> images;
static std::map<VkImageView,VkImage> views;
static std::map<VkFramebuffer,Extent> framebuffers;
static Extent active;static VkFramebuffer activeFramebuffer;
static void vk_clear_attachment_pool(){}
static void vk_alloc_attachment_memory(){}
static void create_color_attachment(uint32_t width,uint32_t height,VkSampleCountFlagBits,VkFormat,VkImageUsageFlags,VkImage *image,VkImageView *view,VkImageLayout,qboolean){CHECK(width>0&&height>0);*image=(VkImage)(uintptr_t(++nextHandle));*view=(VkImageView)(uintptr_t(++nextHandle));images[*image]={width,height};views[*view]=*image;}
static void create_depth_attachment(uint32_t width,uint32_t height,VkSampleCountFlagBits samples,VkImage *image,VkImageView *view,qboolean transient){create_color_attachment(width,height,samples,VK_FORMAT_D24_UNORM_S8_UINT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,image,view,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,transient);}
static VkResult qvkCreateFramebuffer(VkDevice,const VkFramebufferCreateInfo *info,const VkAllocationCallbacks*,VkFramebuffer *out){
 CHECK(info->width>0&&info->height>0);
 for(uint32_t i=0;i<info->attachmentCount;++i){CHECK(views.count(info->pAttachments[i])==1);const auto e=images.at(views.at(info->pAttachments[i]));CHECK(info->width<=e.width&&info->height<=e.height);}
 *out=(VkFramebuffer)(uintptr_t(++nextHandle));framebuffers[*out]={info->width,info->height};return VK_SUCCESS;
}
static void qvkCmdBeginRenderPass(VkCommandBuffer,const VkRenderPassBeginInfo *info,VkSubpassContents){CHECK(framebuffers.count(info->framebuffer)==1);activeFramebuffer=info->framebuffer;active=framebuffers.at(info->framebuffer);CHECK(info->renderArea.extent.width==active.width&&info->renderArea.extent.height==active.height);}
static void qvkCmdClearAttachments(VkCommandBuffer,uint32_t,const VkClearAttachment*,uint32_t count,const VkClearRect *rects){++clearCalls;for(uint32_t i=0;i<count;++i){const auto &r=rects[i].rect;CHECK(r.extent.width>0&&r.extent.height>0);CHECK(r.offset.x>=0&&r.offset.y>=0);CHECK(uint64_t(r.offset.x)+r.extent.width<=active.width&&uint64_t(r.offset.y)+r.extent.height<=active.height);}}
static void vk_record_image_layout_transition(VkCommandBuffer,VkImage image,VkImageAspectFlags,VkImageLayout,VkImageLayout,uint32_t,uint32_t){CHECK(images.count(image)==1);}
static void qvkCmdBlitImage(VkCommandBuffer,VkImage source,VkImageLayout,VkImage destination,VkImageLayout,uint32_t count,const VkImageBlit *regions,VkFilter){
 ++blits;const auto src=images.at(source),dst=images.at(destination);CHECK(count==1);CHECK(regions[0].srcOffsets[1].x==int32_t(src.width)&&regions[0].srcOffsets[1].y==int32_t(src.height));CHECK(regions[0].dstOffsets[1].x==int32_t(dst.width)&&regions[0].dstOffsets[1].y==int32_t(dst.height));
}
static void qvkCmdCopyImage(VkCommandBuffer,VkImage source,VkImageLayout,VkImage destination,VkImageLayout,uint32_t count,const VkImageCopy *regions){++copies;const auto src=images.at(source),dst=images.at(destination);CHECK(count==1);CHECK(regions[0].extent.width==src.width&&regions[0].extent.height==src.height);CHECK(regions[0].extent.width==dst.width&&regions[0].extent.height==dst.height);}
void get_scissor_rect(VkRect2D*);
#include "targets.h"
static void setup(uint32_t width,uint32_t height,bool ssaa,bool msaa=false){
 vk={};gls={int(width),int(height),int(width),int(height)};glConfig={int(width*(ssaa?2:1)),int(height*(ssaa?2:1)),8};fboSetting.integer=1;ssaaSetting.integer=ssaa?1:0;
 images.clear();views.clear();framebuffers.clear();nextHandle=1;clearCalls=blits=copies=0;backEnd={};command={};vk.cmd=&command;
 vk.active=vk.fboActive=vk.bloomActive=vk.dglowActive=vk.refractionActive=true;vk.msaaActive=msaa;vkSamples=msaa?4:1;vk.screenMapSamples=1;vk.screenMapWidth=width/4;vk.screenMapHeight=height/4;vk.swapchain_image_count=1;
 vk_create_attachments();
 // The presentation image comes from the window-system swapchain, independently
 // of renderer-created offscreen allocations.
 VkImage present;create_color_attachment(width,height,VK_SAMPLE_COUNT_1_BIT,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,&present,&vk.swapchain_image_views[0],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,qfalse);
 vk_create_framebuffers();
 backEnd.viewParms.viewportWidth=glConfig.vidWidth;backEnd.viewParms.viewportHeight=glConfig.vidHeight;
}
static void verifyTarget(bool depth=true){
 CHECK(vk.renderWidth==active.width&&vk.renderHeight==active.height);
 if(activeFramebuffer==vk.framebuffers.dglow.extract){const auto depthExtent=images.at(vk.depth_image);CHECK(active.width==depthExtent.width&&active.height==depthExtent.height);}
 CHECK(std::abs(vk.renderScaleX*glConfig.vidWidth-vk.renderWidth)<0.001f&&std::abs(vk.renderScaleY*glConfig.vidHeight-vk.renderHeight)<0.001f);
 VkRect2D viewport;get_viewport_rect(&viewport);CHECK(viewport.offset.x==0&&viewport.offset.y==0&&viewport.extent.width==active.width&&viewport.extent.height==active.height);
 if(depth){vk_world.dirty_depth_attachment=1;vk_clear_depthstencil_attachments(qtrue);}const vec4_t color={};vk_clear_color_attachments(color);
}
int main(int argc,char **argv){
 const std::string part=argc>1?argv[1]:"ssaa";
 for(const Extent extent: {Extent{961,541},Extent{1024,768},Extent{320,241}})for(bool msaa:{false,true}){
  const bool ssaa=part!="normal"&&part!="control";setup(extent.width,extent.height,ssaa,msaa);
  if(part=="control"){
   vk_refraction_extract();CHECK(blits+copies==1);vk_begin_main_render_pass();verifyTarget();vk_begin_post_refraction_extract_render_pass();verifyTarget();vk_begin_dglow_extract_render_pass();verifyTarget();
  }else if(part=="normal"||part=="ssaa"){
   vk_refraction_extract();CHECK(blits+copies==1);
   vk_begin_main_render_pass();verifyTarget();vk_begin_post_refraction_extract_render_pass();verifyTarget();vk_begin_screenmap_render_pass();verifyTarget();
   vk_begin_bloom_extract_render_pass();verifyTarget(false);vk_begin_dglow_extract_render_pass();verifyTarget();
   for(uint32_t i=0;i<8;++i){vk_begin_bloom_blur_render_pass(i);verifyTarget(false);vk_begin_dglow_blur_render_pass(i);verifyTarget(false);}
   vk_begin_post_blend_render_pass(vk.render_pass.dglow.blend.handle,qtrue);verifyTarget();
  }else if(part=="depth_alignment"){
   // Glow LOADs and depth-tests against the main pass depth image. Its world
   // fragments must address the same depth pixels, not a reduced top-left area.
   vk_begin_dglow_extract_render_pass();const auto depth=images.at(vk.depth_image);
   CHECK(vk.renderWidth==depth.width&&vk.renderHeight==depth.height);
   CHECK(vk.renderScaleX==1.0f&&vk.renderScaleY==1.0f);
   const auto glow=images.at(vk.dglow_image[0]);CHECK(glow.width==depth.width&&glow.height==depth.height);
   if(msaa){const auto glowMS=images.at(vk.dglow_msaa_image);CHECK(glowMS.width==depth.width&&glowMS.height==depth.height);}
  }else if(part=="portal"){
   vk_begin_dglow_extract_render_pass();backEnd.viewParms.portalView=1;auto &p=backEnd.viewParms;p.scissorX=glConfig.vidWidth/4;p.scissorY=glConfig.vidHeight/4;p.scissorWidth=glConfig.vidWidth/2;p.scissorHeight=glConfig.vidHeight/2;
   VkRect2D r;get_scissor_rect(&r);CHECK(r.offset.x==int64_t(p.scissorX)*active.width/glConfig.vidWidth);CHECK(r.extent.width==uint32_t((int64_t(p.scissorX)+p.scissorWidth)*active.width/glConfig.vidWidth-r.offset.x));
   vk_world.dirty_depth_attachment=1;vk_clear_depthstencil_attachments(qtrue);
   vk_begin_screenmap_render_pass();get_scissor_rect(&r);
   CHECK(r.offset.x==int64_t(p.scissorX)*active.width/glConfig.vidWidth);
   CHECK(r.extent.width==uint32_t((int64_t(p.scissorX)+p.scissorWidth)*active.width/glConfig.vidWidth-r.offset.x));
   vk_world.dirty_depth_attachment=1;vk_clear_depthstencil_attachments(qtrue);
  }else if(part=="clear_bounds"){
   vk_begin_dglow_extract_render_pass();vk_world.dirty_depth_attachment=1;vk_clear_depthstencil_attachments(qtrue);CHECK(clearCalls==1);
  }else if(part=="off_target"){
   vk_begin_dglow_extract_render_pass();backEnd.viewParms.viewportX=glConfig.vidWidth*2;vk_world.dirty_depth_attachment=1;vk_clear_depthstencil_attachments(qtrue);const vec4_t color={};vk_clear_color_attachments(color);CHECK(clearCalls==0);
  }
 }
 std::printf("%d actual target extent checks passed (%s, scale %u)\n",checks,part.c_str(),REFRACTION_EXTRACT_SCALE);
}
