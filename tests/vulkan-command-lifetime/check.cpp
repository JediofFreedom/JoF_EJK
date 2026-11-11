#include "vulkan/vulkan_core.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::exit(1);}++checks;}while(0)
static int checks;
#define ERR_FATAL 1
static struct {VkDevice device;VkQueue queue;VkCommandPool command_pool;}vk;
static struct {int uploadWaitMs,uploadSubmissions;}vkLoadStats;
static int clockMs;
static struct {void Error(int,const char*,...){throw std::runtime_error("injected driver error");}int Milliseconds(){return clockMs++;}}ri;
static const char *vk_result_string(VkResult){return "injected";}
static void vk_debug(const char*,...){}
#include "vk_result_check.h"
enum class RuntimeStage{GpuWait};struct RuntimeProfileScope{explicit RuntimeProfileScope(RuntimeStage){}};
static void vk_flush_image_uploads(){}
enum Stage{Absent,Initial,Recording,Executable,Pending,Released};
static Stage state;
static int allocationCalls,beginCalls,endCalls,submitCalls,waitCalls,freeCalls;
static VkResult allocationResult,beginResult,endResult,submitResult,waitResult;
static VkCommandBuffer buffer=(VkCommandBuffer)11;
static VkResult qvkAllocateCommandBuffers(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer*out){++allocationCalls;if(allocationResult==VK_SUCCESS){state=Initial;*out=buffer;}return allocationResult;}
static VkResult qvkBeginCommandBuffer(VkCommandBuffer cmd,const VkCommandBufferBeginInfo*){++beginCalls;CHECK(cmd==buffer&&state==Initial);if(beginResult==VK_SUCCESS)state=Recording;return beginResult;}
static VkResult qvkEndCommandBuffer(VkCommandBuffer cmd){++endCalls;CHECK(cmd==buffer&&state==Recording);if(endResult==VK_SUCCESS)state=Executable;return endResult;}
static VkResult vk_queue_submit(VkQueue,uint32_t,const VkSubmitInfo*info,VkFence){++submitCalls;CHECK(state==Executable&&info->commandBufferCount==1&&info->pCommandBuffers[0]==buffer);if(submitResult==VK_SUCCESS||submitResult==VK_ERROR_DEVICE_LOST)state=Pending;return submitResult;}
static VkResult qvkQueueWaitIdle(VkQueue){++waitCalls;if(waitResult==VK_SUCCESS&&state==Pending)state=Executable;return waitResult;}
static void qvkFreeCommandBuffers(VkDevice,VkCommandPool,uint32_t count,const VkCommandBuffer*cmd){CHECK(count==1&&cmd[0]==buffer&&state!=Absent&&state!=Pending&&state!=Released);++freeCalls;state=Released;}
#include "commands.h"
static void reset(){state=Absent;allocationCalls=beginCalls=endCalls=submitCalls=waitCalls=freeCalls=clockMs=0;vkLoadStats={};allocationResult=beginResult=endResult=submitResult=waitResult=VK_SUCCESS;}
static void finish(){vk_end_command_buffer(buffer,"fixture");}
static void expectError(void(*operation)()){bool caught=false;try{operation();}catch(const std::runtime_error&){caught=true;}CHECK(caught);}
static void start(){vk_begin_command_buffer();}
int main(int argc,char**argv){std::string part=argc>1?argv[1]:"all";
 if(part=="all"||part=="allocation"){reset();allocationResult=VK_ERROR_OUT_OF_HOST_MEMORY;expectError(start);CHECK(beginCalls==0&&freeCalls==0&&state==Absent);}
 if(part=="all"||part=="begin"){reset();beginResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;expectError(start);CHECK(endCalls==0&&submitCalls==0&&freeCalls==1&&state==Released);}
 if(part=="all"||part=="end"){reset();CHECK(vk_begin_command_buffer()==buffer);endResult=VK_ERROR_OUT_OF_HOST_MEMORY;expectError(finish);CHECK(submitCalls==0&&waitCalls==0&&freeCalls==1&&state==Released);}
 if(part=="all"||part=="submit_oom")for(VkResult oom:{VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_OUT_OF_DEVICE_MEMORY}){reset();CHECK(vk_begin_command_buffer()==buffer);submitResult=oom;expectError(finish);CHECK(waitCalls==0&&freeCalls==1&&state==Released&&vkLoadStats.uploadSubmissions==0);}
 if(part=="all"||part=="submit_lost"){reset();CHECK(vk_begin_command_buffer()==buffer);submitResult=VK_ERROR_DEVICE_LOST;expectError(finish);CHECK(waitCalls==0&&freeCalls==0&&state==Pending&&vkLoadStats.uploadSubmissions==0);}
 if(part=="all"||part=="wait")for(VkResult failure:{VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_DEVICE_LOST}){reset();CHECK(vk_begin_command_buffer()==buffer);waitResult=failure;expectError(finish);CHECK(waitCalls==1&&freeCalls==0&&state==Pending&&vkLoadStats.uploadSubmissions==0);}
 if(part=="all"||part=="backend_wait"){reset();waitResult=VK_ERROR_DEVICE_LOST;expectError(vk_queue_wait_idle);CHECK(waitCalls==1);}
 if(part=="all"||part=="success"){reset();CHECK(vk_begin_command_buffer()==buffer);finish();CHECK(allocationCalls==1&&beginCalls==1&&endCalls==1&&submitCalls==1&&waitCalls==1&&freeCalls==1&&state==Released&&vkLoadStats.uploadSubmissions==1);}
 std::printf("%d actual command lifetime checks passed\n",checks);
}
