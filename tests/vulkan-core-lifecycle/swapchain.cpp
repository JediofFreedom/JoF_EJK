#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
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
static int checks;
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
static VkResult qvkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR *out){*out=caps;return VK_SUCCESS;}
static VkResult qvkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice,VkSurfaceKHR,uint32_t *count,VkPresentModeKHR *modes){*count=1;if(modes)*modes=VK_PRESENT_MODE_FIFO_KHR;return VK_SUCCESS;}
static VkResult qvkCreateSwapchainKHR(VkDevice,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks*,VkSwapchainKHR *swapchain){selected=*info;*swapchain=(VkSwapchainKHR)4;return VK_SUCCESS;}
static VkResult qvkGetSwapchainImagesKHR(VkDevice,VkSwapchainKHR,uint32_t *count,VkImage *images){*count=2;if(images){images[0]=(VkImage)5;images[1]=(VkImage)6;}return VK_SUCCESS;}
static VkResult qvkCreateImageView(VkDevice,const VkImageViewCreateInfo*,const VkAllocationCallbacks*,VkImageView *view){*view=(VkImageView)7;return VK_SUCCESS;}
static VkResult qvkCreateSemaphore(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore *semaphore){*semaphore=(VkSemaphore)8;return VK_SUCCESS;}
static const char *vk_result_string(VkResult){return "injected result";}
#include "vk_result_check.h"
#include "swapchain.h"
static void reset(bool fbo,VkImageUsageFlags usage,VkCompositeAlphaFlagsKHR alpha){
 vk={};caps={};selected={};glConfig={640,480};gls={640,480};vk.fboActive=fbo;
 caps.currentExtent={640,480};caps.minImageExtent={1,1};caps.maxImageExtent={4096,4096};
 caps.minImageCount=2;caps.maxImageCount=8;caps.supportedUsageFlags=usage;caps.supportedCompositeAlpha=alpha;
}
int main(){
 const VkImageUsageFlags color=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,src=VK_IMAGE_USAGE_TRANSFER_SRC_BIT,dst=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 for(bool fbo:{false,true})for(VkImageUsageFlags usage:{color|src,color|src|dst})for(VkCompositeAlphaFlagsKHR alpha:{VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR),VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR),VkCompositeAlphaFlagsKHR(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR|VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)}){
  reset(fbo,usage,alpha);vk_create_swapchain(VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,{},&vk.swapchain);
  CHECK((selected.imageUsage&usage)==selected.imageUsage);CHECK(bool(selected.imageUsage&dst)==bool(usage&dst));
  CHECK(bool(vk.swapchainTransferDst)==bool(usage&dst));CHECK(selected.compositeAlpha&alpha);
  CHECK(selected.compositeAlpha==VkCompositeAlphaFlagBitsKHR(alpha&(~alpha+1)));CHECK(vk.swapchain_image_count==2);
 }
 std::printf("%d actual swapchain capability checks passed\n",checks);
}
