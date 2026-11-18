#include "vulkan/vulkan_core.h"
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}++checks;}while(0)
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#include "attachment_config.h"
#define PAD(x,a) (((x)+(a)-1)&~((a)-1))
#define VK_SET_OBJECT_NAME(...) do{}while(0)
#define ERR_FATAL 1
#define ERR_DROP 2
#define Com_Memset std::memset
typedef int qboolean;const qboolean qtrue=1,qfalse=0;
static int checks,imageCreates,requirementsQueries,binds,views,transitions,submissions;
static VkResult createResult=VK_SUCCESS,allocateResult=VK_SUCCESS,bindResult=VK_SUCCESS,viewResult=VK_SUCCESS;
static const uint64_t memoryCanary=UINT64_C(0xa18e7d4b29c0536f);
static struct {
 VkDevice device;qboolean dedicatedAllocation;VkFormat depth_format;
 #include "attachment_memory_member.h"
 uint64_t memoryAfter;
 uint32_t image_memory_count;
 qboolean fboActive,bloomActive,dglowActive,refractionActive,msaaActive;
 VkFormat bloom_format,color_format,capture_format;
 uint32_t screenMapSamples,screenMapWidth,screenMapHeight;
 VkImage bloom_image[1+VK_NUM_BLUR_PASSES*2],dglow_image[1+VK_NUM_BLUR_PASSES*2];
 VkImageView bloom_image_view[1+VK_NUM_BLUR_PASSES*2],dglow_image_view[1+VK_NUM_BLUR_PASSES*2];
 VkImage dglow_msaa_image,color_image,refraction_extract_image,msaa_image,depth_image;
 VkImageView dglow_msaa_image_view,color_image_view,refraction_extract_image_view,msaa_image_view,depth_image_view;
 struct {VkImage image;VkImageView image_view;}capture;
 struct {VkImage color_image_msaa,color_image,depth_image;VkImageView color_image_view_msaa,color_image_view,depth_image_view;}screenMap;
}vk;
static struct {int stencilBits,vidWidth,vidHeight;}glConfig;
static struct {int captureWidth,captureHeight;}gls;
static struct {int integer;}supersample;
static auto *r_ext_supersample=&supersample;
static int vkSamples=VK_SAMPLE_COUNT_1_BIT;
static struct {void Error(int,const char *format,...){char message[512];va_list args;va_start(args,format);std::vsnprintf(message,sizeof(message),format,args);va_end(args);throw std::runtime_error(message);}}ri;
static void vk_debug(const char*,...){}
static const char *vk_result_string(VkResult){return "injected";}
#include "vk_result_check.h"
struct Allocation{VkDeviceMemory memory;VkImage dedicated;VkDeviceSize bytes;};
static std::vector<Allocation> allocations;
static std::set<VkImage> required;
static std::map<VkImage,VkDeviceMemory> bound;
static bool requireAllImages;
struct Creation {uint32_t width,height;VkSampleCountFlagBits samples;VkImageUsageFlags usage;};
static std::vector<Creation> creations;
static uint32_t vk_find_memory_type_lazy(uint32_t bits,VkMemoryPropertyFlags,VkMemoryPropertyFlags*){CHECK(bits==1);return 0;}
static uint32_t vk_find_memory_type(uint32_t bits,VkMemoryPropertyFlags){CHECK(bits==1);return 0;}
static VkResult qvkCreateImage(VkDevice,const VkImageCreateInfo *info,const VkAllocationCallbacks*,VkImage *image){++imageCreates;if(createResult==VK_SUCCESS){*image=(VkImage)(uintptr_t(imageCreates));creations.push_back({info->extent.width,info->extent.height,info->samples,info->usage});}return createResult;}
static void qvkGetImageMemoryRequirements(VkDevice,VkImage image,VkMemoryRequirements *out){++requirementsQueries;CHECK(image!=VK_NULL_HANDLE);*out={};out->size=64;out->alignment=64;out->memoryTypeBits=1;}
static void qvkGetImageMemoryRequirements2KHR(VkDevice device,const VkImageMemoryRequirementsInfo2KHR *input,VkMemoryRequirements2KHR *out){qvkGetImageMemoryRequirements(device,input->image,&out->memoryRequirements);auto *dedicated=(VkMemoryDedicatedRequirementsKHR*)out->pNext;dedicated->requiresDedicatedAllocation=(requireAllImages||required.count(input->image))?VK_TRUE:VK_FALSE;}
static VkResult allocateMemory(VkDevice,const VkMemoryAllocateInfo *info,VkDeviceMemory *memory,const char*){
 if(allocateResult!=VK_SUCCESS)return allocateResult;
 const VkImage dedicated=info->pNext?((const VkMemoryDedicatedAllocateInfoKHR*)info->pNext)->image:VK_NULL_HANDLE;
 *memory=(VkDeviceMemory)(uintptr_t(100+allocations.size()));allocations.push_back({*memory,dedicated,info->allocationSize});return VK_SUCCESS;
}
#define VK_ALLOCATE_MEMORY(d,i,m,n) allocateMemory(d,i,m,n)
#define VK_ALLOCATE_MEMORY_CHECK(d,i,m,n) VK_CHECK(allocateMemory(d,i,m,n))
static VkResult qvkBindImageMemory(VkDevice,VkImage image,VkDeviceMemory memory,VkDeviceSize offset){
 ++binds;if(bindResult!=VK_SUCCESS)return bindResult;
 bool found=false;for(const auto &a:allocations)if(a.memory==memory){found=true;CHECK(offset+64<=a.bytes);if(requireAllImages||required.count(image))CHECK(a.dedicated==image&&offset==0&&a.bytes==64);}
 CHECK(found);bound[image]=memory;return VK_SUCCESS;
}
static VkResult qvkCreateImageView(VkDevice,const VkImageViewCreateInfo *info,const VkAllocationCallbacks*,VkImageView *view){++views;CHECK(bound.count(info->image)==1);if(viewResult==VK_SUCCESS)*view=(VkImageView)(uintptr_t(200+views));return viewResult;}
static VkCommandBuffer vk_begin_command_buffer(){return (VkCommandBuffer)1;}
static void vk_record_image_layout_transition(VkCommandBuffer,VkImage image,VkImageAspectFlags,VkImageLayout,VkImageLayout,uint32_t,uint32_t){CHECK(bound.count(image)==1);++transitions;}
static void vk_end_command_buffer(VkCommandBuffer,const char*){++submissions;}
#include "attachments.h"
static void reset(){vk={};vk.memoryAfter=memoryCanary;vk.dedicatedAllocation=qtrue;vk.depth_format=VK_FORMAT_D24_UNORM_S8_UINT;glConfig={8,0,0};gls={};supersample.integer=0;vkSamples=VK_SAMPLE_COUNT_1_BIT;vk_clear_attachment_pool();allocations.clear();required.clear();bound.clear();creations.clear();requireAllImages=false;imageCreates=requirementsQueries=binds=views=transitions=submissions=0;createResult=allocateResult=bindResult=viewResult=VK_SUCCESS;}
static VkImage images[3];static VkImageView imageViews[3];
static void color(int i){create_color_attachment(32,32,VK_SAMPLE_COUNT_1_BIT,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,&images[i],&imageViews[i],VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,qfalse);}
static void maxFeatures(bool dedicated){
 reset();requireAllImages=dedicated;
 vk.fboActive=vk.bloomActive=vk.dglowActive=vk.refractionActive=vk.msaaActive=qtrue;
 vkSamples=VK_SAMPLE_COUNT_8_BIT;vk.screenMapSamples=VK_SAMPLE_COUNT_8_BIT;
 vk.screenMapWidth=480;vk.screenMapHeight=270;
 vk.color_format=vk.bloom_format=VK_FORMAT_R16G16B16A16_SFLOAT;vk.capture_format=VK_FORMAT_R8G8B8A8_UNORM;
 glConfig.vidWidth=1922;glConfig.vidHeight=1082;gls.captureWidth=961;gls.captureHeight=541;supersample.integer=1;
 vk_create_attachments();
 CHECK(imageCreates==27&&requirementsQueries==27&&binds==27&&views==27&&transitions==27&&bound.size()==27);
 CHECK(vk.fboActive&&vk.bloomActive&&vk.dglowActive&&vk.refractionActive&&vk.msaaActive&&supersample.integer==1);
 int multisampled=0,depths=0;for(const auto &c:creations){CHECK(c.width&&c.height);if(c.samples==VK_SAMPLE_COUNT_8_BIT)++multisampled;if(c.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)++depths;}
 CHECK(multisampled==5&&depths==2);
 CHECK(vk.capture.image&&vk.refraction_extract_image&&vk.msaa_image&&vk.dglow_msaa_image&&vk.screenMap.color_image_msaa&&vk.depth_image);
 CHECK(num_attachments==0&&vk.memoryAfter==memoryCanary);
 CHECK(allocations.size()==(dedicated?27u:1u)&&vk.image_memory_count==allocations.size());
 CHECK(submissions==(dedicated?27:1));
}
static void capacity(){
 reset();const size_t count=ARRAY_LEN(attachments);std::vector<VkImageView> slots(count+1);
 VkMemoryRequirements requirements={};requirements.size=64;requirements.alignment=64;requirements.memoryTypeBits=1;
 for(size_t i=0;i<count;++i)vk_add_attachment_desc((VkImage)(uintptr_t(i+1)),&slots[i],VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,&requirements,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
 CHECK(num_attachments==count&&attachments[count-1].descriptor==(VkImage)(uintptr_t(count))&&attachments[count-1].image_view==&slots[count-1]);
 std::vector<unsigned char> pool(sizeof attachments);std::memcpy(pool.data(),attachments,sizeof attachments);
 bool caught=false;try{vk_add_attachment_desc((VkImage)(uintptr_t(count+1)),&slots[count],VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,&requirements,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);}catch(const std::runtime_error &error){caught=true;std::printf("Expected pool overflow: %s\n",error.what());}
 CHECK(caught&&num_attachments==count&&std::memcmp(pool.data(),attachments,sizeof attachments)==0&&slots[count]==VK_NULL_HANDLE);
 reset();requireAllImages=true;const size_t trackerCount=ARRAY_LEN(vk.image_memory);
 for(size_t i=0;i<trackerCount;++i)color(0);
 CHECK(vk.image_memory_count==trackerCount&&allocations.size()==trackerCount&&vk.memoryAfter==memoryCanary);
 std::vector<unsigned char> tracker(sizeof vk.image_memory);std::memcpy(tracker.data(),vk.image_memory,sizeof vk.image_memory);
 caught=false;try{color(0);}catch(const std::runtime_error &error){caught=true;std::printf("Expected tracker overflow: %s\n",error.what());}
 CHECK(caught&&vk.image_memory_count==trackerCount&&allocations.size()==trackerCount&&vk.memoryAfter==memoryCanary);
 CHECK(std::memcmp(tracker.data(),vk.image_memory,sizeof vk.image_memory)==0&&binds==int(trackerCount)&&views==int(trackerCount)&&transitions==int(trackerCount));
}
int main(int argc,char **argv){
 const std::string part=argc>1?argv[1]:"all";
 try{
  if(part=="all"||part=="max_features")maxFeatures(false);
  if(part=="all"||part=="max_features_dedicated")maxFeatures(true);
  if(part=="all"||part=="capacity")capacity();
 }catch(const std::runtime_error &error){std::fprintf(stderr,"Unexpected production attachment error (%s): %s\n",part.c_str(),error.what());return 1;}
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
