#include "vulkan/vulkan_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}++checks;}while(0)
#define ERR_FATAL 1
#define NUM_COMMAND_BUFFERS 2
#define Com_Memset std::memset
#define VK_SET_OBJECT_NAME(...) do{++labels;}while(0)
typedef int qboolean;const qboolean qtrue=1,qfalse=0;
static int checks,labels,operationCalls,failAt;
static VkResult injected=VK_SUCCESS;
struct Command {VkCommandBuffer command_buffer;VkSemaphore image_acquired;VkFence rendering_finished_fence;qboolean waitForFence;VkBuffer vertex_buffer,indirect_buffer;VkDeviceMemory vertex_buffer_memory,indirect_buffer_memory;};
static struct {VkDevice device;VkCommandPool command_pool;uint32_t queue_family_index;Command tess[2];VkDeviceMemory geometry_buffer_memory,indirect_buffer_memory;}vk;
static struct {void Error(int,const char*,...){throw std::runtime_error("creation error");}}ri;
static void vk_debug(const char*,...){}
static const char *va(const char*,...){return "name";}
static const char *vk_result_string(VkResult){return "injected";}
#include "vk_result_check.h"
static VkResult nextResult(){++operationCalls;return operationCalls==failAt?injected:VK_SUCCESS;}
static VkResult qvkCreateCommandPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool *out){auto result=nextResult();if(result==VK_SUCCESS)*out=(VkCommandPool)1;return result;}
static VkResult qvkAllocateCommandBuffers(VkDevice,const VkCommandBufferAllocateInfo *info,VkCommandBuffer *out){CHECK(info->commandPool==(VkCommandPool)1);auto result=nextResult();if(result==VK_SUCCESS)*out=(VkCommandBuffer)(uintptr_t(operationCalls+1));return result;}
static VkResult qvkCreateSemaphore(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore *out){auto result=nextResult();if(result==VK_SUCCESS)*out=(VkSemaphore)(uintptr_t(operationCalls+1));return result;}
static VkResult qvkCreateFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence *out){auto result=nextResult();if(result==VK_SUCCESS)*out=(VkFence)(uintptr_t(operationCalls+1));return result;}
struct vkRenderPass_t {VkRenderPass handle;const char *name;struct {uint32_t attachmentCount;const VkAttachmentDescription *attachments;uint32_t subpass_count;VkSubpassDescription subpass;uint32_t dependencyCount;const VkSubpassDependency *dependencies;}def;};
static VkResult qvkCreateRenderPass(VkDevice,const VkRenderPassCreateInfo*,const VkAllocationCallbacks*,VkRenderPass *out){auto result=nextResult();if(result==VK_SUCCESS)*out=(VkRenderPass)1;return result;}
static std::set<VkBuffer> destroyed;
static std::set<VkDeviceMemory> freed;
static void destroyBuffer(VkDevice,VkBuffer buffer){if(buffer!=VK_NULL_HANDLE)CHECK(destroyed.insert(buffer).second);}
static void freeMemory(VkDevice,VkDeviceMemory memory){if(memory!=VK_NULL_HANDLE){CHECK(freed.insert(memory).second);CHECK(destroyed.count((VkBuffer)(uintptr_t(memory)-10))==1||destroyed.size()==2);}}
#define VK_DESTROY_BUFFER(d,b) destroyBuffer(d,b)
#define VK_FREE_MEMORY(d,m) freeMemory(d,m)
#include "initialization.h"
static void reset(){vk={};vk.command_pool=(VkCommandPool)1;labels=operationCalls=failAt=0;injected=VK_SUCCESS;destroyed.clear();freed.clear();}
static void run(const std::string &part){if(part=="pool")vk_create_command_pool();else if(part=="buffers")vk_create_command_buffer();else if(part=="sync")vk_create_sync_primitives();else {vkRenderPass_t pass={};pass.name="test";vk_create_render_pass(&pass);}}
int main(int argc,char **argv){
 const std::string part=argc>1?argv[1]:"pool";
 if(part=="release"){
  for(bool dedicated:{false,true})for(bool indirect:{false,true}){
   reset();for(int i=0;i<2;++i){auto &cmd=vk.tess[i];if(indirect){cmd.indirect_buffer=(VkBuffer)(uintptr_t(i+1));if(dedicated)cmd.indirect_buffer_memory=(VkDeviceMemory)(uintptr_t(i+11));}else{cmd.vertex_buffer=(VkBuffer)(uintptr_t(i+1));if(dedicated)cmd.vertex_buffer_memory=(VkDeviceMemory)(uintptr_t(i+11));}}
   if(!dedicated){if(indirect)vk.indirect_buffer_memory=(VkDeviceMemory)20;else vk.geometry_buffer_memory=(VkDeviceMemory)20;}
   if(indirect)vk_release_indirect_buffers();else vk_release_geometry_buffers();
   CHECK(destroyed.size()==2&&freed.size()==(dedicated?2:1));
   if(indirect)vk_release_indirect_buffers();else vk_release_geometry_buffers();
   CHECK(destroyed.size()==2&&freed.size()==(dedicated?2:1));
  }
 }else{
  const int operations=part=="buffers"?2:(part=="sync"?4:1);
  for(int fail=1;fail<=operations;++fail){reset();injected=VK_ERROR_OUT_OF_DEVICE_MEMORY;failAt=fail;bool caught=false;try{run(part);}catch(const std::runtime_error&){caught=true;}CHECK(caught&&operationCalls==fail);if(fail==1)CHECK(labels==0);}
  reset();run(part);CHECK(operationCalls==operations);
 }
 std::printf("%d actual initialization checks passed (%s)\n",checks,part.c_str());
}
