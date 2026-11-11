#include "vulkan/vulkan_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}++checks;}while(0)
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define MAX_ATTACHMENTS_IN_POOL 25
#define PAD(x,a) (((x)+(a)-1)&~((a)-1))
#define ERR_FATAL 1
#define ERR_DROP 2
#define Com_Memset std::memset
typedef int qboolean;const qboolean qtrue=1,qfalse=0;
static int checks,imageCreates,requirementsQueries,binds,views,transitions,submissions;
static VkResult createResult=VK_SUCCESS,allocateResult=VK_SUCCESS,bindResult=VK_SUCCESS,viewResult=VK_SUCCESS;
static struct {VkDevice device;qboolean dedicatedAllocation;VkFormat depth_format;VkDeviceMemory image_memory[26];uint32_t image_memory_count;}vk;
static struct {int stencilBits;}glConfig;
static struct {void Error(int,const char*,...){throw std::runtime_error("driver error");}}ri;
static void vk_debug(const char*,...){}
static const char *vk_result_string(VkResult){return "injected";}
#include "vk_result_check.h"
struct Allocation{VkDeviceMemory memory;VkImage dedicated;VkDeviceSize bytes;};
static std::vector<Allocation> allocations;
static std::set<VkImage> required;
static std::map<VkImage,VkDeviceMemory> bound;
static uint32_t vk_find_memory_type_lazy(uint32_t bits,VkMemoryPropertyFlags,VkMemoryPropertyFlags*){CHECK(bits==1);return 0;}
static uint32_t vk_find_memory_type(uint32_t bits,VkMemoryPropertyFlags){CHECK(bits==1);return 0;}
static VkResult qvkCreateImage(VkDevice,const VkImageCreateInfo*,const VkAllocationCallbacks*,VkImage *image){++imageCreates;if(createResult==VK_SUCCESS)*image=(VkImage)(uintptr_t(imageCreates));return createResult;}
static void qvkGetImageMemoryRequirements(VkDevice,VkImage image,VkMemoryRequirements *out){++requirementsQueries;CHECK(image!=VK_NULL_HANDLE);*out={};out->size=64;out->alignment=64;out->memoryTypeBits=1;}
static void qvkGetImageMemoryRequirements2KHR(VkDevice device,const VkImageMemoryRequirementsInfo2KHR *input,VkMemoryRequirements2KHR *out){qvkGetImageMemoryRequirements(device,input->image,&out->memoryRequirements);auto *dedicated=(VkMemoryDedicatedRequirementsKHR*)out->pNext;dedicated->requiresDedicatedAllocation=required.count(input->image)?VK_TRUE:VK_FALSE;}
static VkResult allocateMemory(VkDevice,const VkMemoryAllocateInfo *info,VkDeviceMemory *memory,const char*){
 if(allocateResult!=VK_SUCCESS)return allocateResult;
 const VkImage dedicated=info->pNext?((const VkMemoryDedicatedAllocateInfoKHR*)info->pNext)->image:VK_NULL_HANDLE;
 *memory=(VkDeviceMemory)(uintptr_t(100+allocations.size()));allocations.push_back({*memory,dedicated,info->allocationSize});return VK_SUCCESS;
}
#define VK_ALLOCATE_MEMORY(d,i,m,n) allocateMemory(d,i,m,n)
#define VK_ALLOCATE_MEMORY_CHECK(d,i,m,n) VK_CHECK(allocateMemory(d,i,m,n))
static VkResult qvkBindImageMemory(VkDevice,VkImage image,VkDeviceMemory memory,VkDeviceSize offset){
 ++binds;if(bindResult!=VK_SUCCESS)return bindResult;
 bool found=false;for(const auto &a:allocations)if(a.memory==memory){found=true;CHECK(offset+64<=a.bytes);if(required.count(image))CHECK(a.dedicated==image&&offset==0&&a.bytes==64);}
 CHECK(found);bound[image]=memory;return VK_SUCCESS;
}
static VkResult qvkCreateImageView(VkDevice,const VkImageViewCreateInfo *info,const VkAllocationCallbacks*,VkImageView *view){++views;CHECK(bound.count(info->image)==1);if(viewResult==VK_SUCCESS)*view=(VkImageView)(uintptr_t(200+views));return viewResult;}
static VkCommandBuffer vk_begin_command_buffer(){return (VkCommandBuffer)1;}
static void vk_record_image_layout_transition(VkCommandBuffer,VkImage image,VkImageAspectFlags,VkImageLayout,VkImageLayout,uint32_t,uint32_t){CHECK(bound.count(image)==1);++transitions;}
static void vk_end_command_buffer(VkCommandBuffer,const char*){++submissions;}
#include "attachments.h"
static void reset(){vk={};vk.dedicatedAllocation=qtrue;vk.depth_format=VK_FORMAT_D24_UNORM_S8_UINT;glConfig.stencilBits=8;vk_clear_attachment_pool();allocations.clear();required.clear();bound.clear();imageCreates=requirementsQueries=binds=views=transitions=submissions=0;createResult=allocateResult=bindResult=viewResult=VK_SUCCESS;}
static VkImage images[3];static VkImageView imageViews[3];
static void color(int i){create_color_attachment(32,32,VK_SAMPLE_COUNT_1_BIT,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,&images[i],&imageViews[i],VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,qfalse);}
int main(int argc,char **argv){
 const std::string part=argc>1?argv[1]:"all";
 if(part=="all"||part=="dedicated"){
  reset();required.insert((VkImage)2);color(0);color(1);color(2);vk_alloc_attachment_memory();
  CHECK(allocations.size()==3&&allocations[1].dedicated==(VkImage)2);CHECK(binds==3&&views==3&&transitions==3);
  reset();required.insert((VkImage)1);create_depth_attachment(32,32,VK_SAMPLE_COUNT_1_BIT,&images[0],&imageViews[0],qtrue);CHECK(allocations.size()==1&&allocations[0].dedicated==(VkImage)1);
 }
 if(part=="all"||part=="shared"){
  reset();color(0);color(1);vk_alloc_attachment_memory();CHECK(allocations.size()==1&&allocations[0].dedicated==VK_NULL_HANDLE&&allocations[0].bytes==128);CHECK(binds==2&&views==2&&transitions==2&&submissions==1);
  reset();vk.dedicatedAllocation=qfalse;color(0);color(1);vk_alloc_attachment_memory();CHECK(allocations.size()==1&&requirementsQueries==2);
 }
 if(part=="all"||part=="failures"){
  for(int stage=0;stage<4;++stage){reset();VkResult *results[]={&createResult,&allocateResult,&bindResult,&viewResult};*results[stage]=VK_ERROR_OUT_OF_DEVICE_MEMORY;
   bool caught=false;try{color(0);vk_alloc_attachment_memory();}catch(const std::runtime_error&){caught=true;}CHECK(caught&&transitions==0&&submissions==0);
   if(stage==0)CHECK(requirementsQueries==0&&allocations.empty());if(stage==1)CHECK(binds==0&&vk.image_memory_count==0);if(stage==2)CHECK(views==0);
  }
 }
 std::printf("%d actual attachment checks passed (%s)\n",checks,part.c_str());
}
