#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}++checks;}while(0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MAX_SWAPCHAIN_IMAGES 8
#define MIN_SWAPCHAIN_IMAGES_FIFO 2
#define MIN_SWAPCHAIN_IMAGES_FIFO_0 3
#define MIN_SWAPCHAIN_IMAGES_IMM 2
#define MIN_SWAPCHAIN_IMAGES_MAILBOX 3
#define PRINT_WARNING 1
#define PRINT_ALL 0
#define ERR_FATAL 2
#define VK_SET_OBJECT_NAME(...) do{}while(0)
static int checks,createCalls,viewCalls,semaphoreCalls;
static uint32_t actualImages=2;
static VkResult surfaceResult=VK_SUCCESS,modesResult=VK_SUCCESS,createResult=VK_SUCCESS,imagesResult=VK_SUCCESS,viewResult=VK_SUCCESS,semaphoreResult=VK_SUCCESS;
typedef int qboolean;
const qboolean qtrue=1,qfalse=0;
static struct {int vidWidth,vidHeight;}glConfig;
static struct {uint32_t windowWidth,windowHeight;}gls;
static struct {
 qboolean fboActive,clearAttachment,swapchainTransferDst;
 VkDevice device;VkSwapchainKHR swapchain;VkSurfaceFormatKHR present_format;
 uint32_t swapchain_image_count;VkImage swapchain_images[8];VkImageView swapchain_image_views[8];
 VkSemaphore swapchain_rendering_finished[8];qboolean swapchain_images_inited[8];
 VkImageLayout initSwapchainLayout;
}vk;
static VkSurfaceCapabilitiesKHR caps;
static VkSwapchainCreateInfoKHR selected;
static struct {
 int Cvar_VariableIntegerValue(const char*){return 1;}
 void Printf(int,const char*,...){}
 void Error(int,const char*,...){throw std::runtime_error("invalid surface");}
}ri;

static void vk_debug(const char*,...){}
static const char *vk_pmode_to_str(VkPresentModeKHR){return "mock";}
static VkResult qvkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR *out){*out=caps;return surfaceResult;}
static VkResult qvkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice,VkSurfaceKHR,uint32_t *count,VkPresentModeKHR *modes){*count=1;if(modes)*modes=VK_PRESENT_MODE_FIFO_KHR;return modesResult;}
static VkResult qvkCreateSwapchainKHR(VkDevice,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks*,VkSwapchainKHR *swapchain){++createCalls;selected=*info;*swapchain=(VkSwapchainKHR)4;return createResult;}
static VkResult qvkGetSwapchainImagesKHR(VkDevice,VkSwapchainKHR,uint32_t *count,VkImage *images){if(!images){*count=actualImages;return imagesResult;}uint32_t written=MIN(*count,actualImages);for(uint32_t i=0;i<written;++i)images[i]=(VkImage)(uintptr_t(5+i));*count=written;return imagesResult!=VK_SUCCESS?imagesResult:(written<actualImages?VK_INCOMPLETE:VK_SUCCESS);}
static VkResult qvkCreateImageView(VkDevice,const VkImageViewCreateInfo*,const VkAllocationCallbacks*,VkImageView *view){++viewCalls;*view=(VkImageView)7;return viewResult;}
static VkResult qvkCreateSemaphore(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore *semaphore){++semaphoreCalls;*semaphore=(VkSemaphore)8;return semaphoreResult;}
static const char *vk_result_string(VkResult){return "injected result";}
#include "vk_result_check.h"
#include "swapchain.h"
static void reset(bool fbo,VkImageUsageFlags usage,VkCompositeAlphaFlagsKHR alpha){
 createCalls=viewCalls=semaphoreCalls=0;actualImages=2;surfaceResult=modesResult=createResult=imagesResult=viewResult=semaphoreResult=VK_SUCCESS;
 vk={};caps={};selected={};glConfig={640,480};gls={640,480};vk.fboActive=fbo;
 caps.currentExtent={640,480};caps.minImageExtent={1,1};caps.maxImageExtent={4096,4096};
 caps.minImageCount=2;caps.maxImageCount=8;caps.supportedUsageFlags=usage;caps.supportedCompositeAlpha=alpha;
}
static void create(){vk_create_swapchain(VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,{},&vk.swapchain);}
static void expectError(){bool caught=false;try{create();}catch(const std::runtime_error&){caught=true;}CHECK(caught);}
int main(int argc,char **argv){
 const std::string part=argc>1?argv[1]:"all";
 const VkImageUsageFlags color=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,src=VK_IMAGE_USAGE_TRANSFER_SRC_BIT,dst=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 if(part=="all"||part=="capabilities")for(bool fbo:{false,true})for(VkImageUsageFlags usage:{color|src,color|src|dst})for(VkCompositeAlphaFlagsKHR alpha:{VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR),VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR),VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR|VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)}){
  reset(fbo,usage,alpha);vk_create_swapchain(VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,{},&vk.swapchain);
  CHECK((selected.imageUsage&usage)==selected.imageUsage);CHECK(bool(selected.imageUsage&dst)==bool(usage&dst));
  CHECK(bool(vk.swapchainTransferDst)==bool(usage&dst));CHECK(selected.compositeAlpha&alpha);
  CHECK(selected.compositeAlpha==VkCompositeAlphaFlagBitsKHR(alpha&(~alpha+1)));CHECK(vk.swapchain_image_count==2);
 }
 if(part=="all"||part=="extent"){
  reset(true,color|src|dst,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);caps.currentExtent={320,240};expectError();CHECK(createCalls==0);
  reset(true,color|src|dst,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);caps.currentExtent={0,0};expectError();CHECK(createCalls==0);
  reset(true,color|src|dst,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);caps.currentExtent={~0U,~0U};create();CHECK(selected.imageExtent.width==640&&selected.imageExtent.height==480);
 }
 if(part=="all"||part=="capacity"){
  for(uint32_t count:{0U,9U}){reset(true,color|src|dst,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);actualImages=count;expectError();CHECK(viewCalls==0&&semaphoreCalls==0&&vk.swapchain_image_count==0);}
  reset(true,color|src|dst,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);caps.minImageCount=9;caps.maxImageCount=12;expectError();CHECK(createCalls==0);
  reset(true,color|src|dst,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);actualImages=8;create();CHECK(viewCalls==8&&semaphoreCalls==8&&vk.swapchain_image_count==8);
 }
 if(part=="all"||part=="errors"){
  for(int stage=0;stage<6;++stage){
   reset(true,color|src|dst,VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
   VkResult *results[]={&surfaceResult,&modesResult,&createResult,&imagesResult,&viewResult,&semaphoreResult};*results[stage]=VK_ERROR_OUT_OF_HOST_MEMORY;
   expectError();
   if(stage<=1)CHECK(createCalls==0);
   if(stage<=3)CHECK(viewCalls==0);
   if(stage<=4)CHECK(semaphoreCalls==0);
  }
 }
 std::printf("%d actual swapchain checks passed (%s)\n",checks,part.c_str());
}
