#include "vulkan/vulkan_core.h"
#include "vk_api_version.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <deque>
#include <stdexcept>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}++checks;}while(0)
#define PRINT_ALL 0
#define PRINT_DEVELOPER 1
#define ERR_FATAL 2

typedef int qboolean;const qboolean qtrue=1,qfalse=0;
static int checks,calls,liveInstances,queryCalls,allocationCalls,outstandingAllocations,failAllocation;
static uint32_t extensionCount=1;
static VkResult extensionCountResult=VK_SUCCESS,extensionListResult=VK_SUCCESS;
static struct {VkInstance instance;uint32_t instanceApiVersion;char instance_extensions_string[2048];}vk;
static struct {void Printf(int,const char*,...){} void Error(int,const char*,...){throw std::runtime_error("instance failure");}}ri;
static void vk_debug(const char*,...){}
static char *Q_stradd(char *dest,const char *source){std::strcpy(dest,source);return dest+std::strlen(source);}
#define Q_stricmp std::strcmp
static qboolean vk_used_instance_extension(const char*){return qtrue;}
static std::deque<VkResult> results;
static std::deque<uint32_t> layers;
static PFN_vkVoidFunction VKAPI_CALL MockProc(VkInstance,const char*){return nullptr;}
static PFN_vkGetInstanceProcAddr qvkGetInstanceProcAddr=MockProc;
static VkResult qvkEnumerateInstanceExtensionProperties(const char*,uint32_t *count,VkExtensionProperties *properties){
 ++queryCalls;*count=extensionCount;if(properties&&extensionCount){*properties={};std::strcpy(properties->extensionName,VK_KHR_SURFACE_EXTENSION_NAME);}return properties?extensionListResult:extensionCountResult;
}
static VkResult qvkCreateInstance(const VkInstanceCreateInfo *info,const VkAllocationCallbacks*,VkInstance *instance){
 ++calls;layers.push_back(info->enabledLayerCount);
 VkResult result=results.empty()?VK_SUCCESS:results.front();if(!results.empty())results.pop_front();
 if(result==VK_SUCCESS){++liveInstances;*instance=reinterpret_cast<VkInstance>(uintptr_t(liveInstances));}
 return result;
}
static const char *vk_result_string(VkResult){return "injected result";}
#include "vk_result_check.h"
static void *FixtureMalloc(size_t bytes){++allocationCalls;if(allocationCalls==failAllocation)return nullptr;void *p=std::malloc(bytes);if(p)++outstandingAllocations;return p;}
static void FixtureFree(void *p){if(p){--outstandingAllocations;std::free(p);}}
#define malloc FixtureMalloc
#define free FixtureFree
#include "instance.h"
#undef malloc
#undef free
static void reset(std::initializer_list<VkResult> sequence){CHECK(outstandingAllocations==0);vk={};calls=liveInstances=queryCalls=allocationCalls=failAllocation=0;extensionCount=1;extensionCountResult=extensionListResult=VK_SUCCESS;layers.clear();results=sequence;}
int main(int argc,char **argv){
 std::string part=argc>1?argv[1]:"creation";
 if(part=="queries"){
  for(int stage=0;stage<3;++stage){reset({VK_SUCCESS});if(stage==0)extensionCountResult=VK_ERROR_OUT_OF_HOST_MEMORY;if(stage==1)extensionListResult=VK_INCOMPLETE;if(stage==2)extensionCount=0;
   bool caught=false;try{vk_create_instance();}catch(const std::runtime_error&){caught=true;}
   CHECK(caught&&calls==0&&liveInstances==0&&outstandingAllocations==0);
  }
  std::printf("%d actual instance query checks passed\n",checks);return 0;
 }
 if(part=="allocations"){
  for(int failed:{1,2}){reset({VK_SUCCESS});failAllocation=failed;bool caught=false;try{vk_create_instance();}catch(const std::runtime_error&){caught=true;}
   CHECK(caught&&calls==0&&liveInstances==0&&outstandingAllocations==0);
  }
  std::printf("%d actual instance allocation checks passed\n",checks);return 0;
 }

 reset({VK_SUCCESS});vk_create_instance();CHECK(calls==1&&liveInstances==1&&vk.instance!=VK_NULL_HANDLE);CHECK(layers==std::deque<uint32_t>{1});
 reset({VK_ERROR_LAYER_NOT_PRESENT,VK_SUCCESS});vk_create_instance();CHECK(calls==2&&liveInstances==1);CHECK(layers==std::deque<uint32_t>({1,1}));
 reset({VK_ERROR_LAYER_NOT_PRESENT,VK_ERROR_LAYER_NOT_PRESENT,VK_SUCCESS});vk_create_instance();CHECK(calls==3&&liveInstances==1);CHECK(layers==std::deque<uint32_t>({1,1,0}));
 reset({VK_ERROR_INCOMPATIBLE_DRIVER});bool caught=false;try{vk_create_instance();}catch(const std::runtime_error&){caught=true;}CHECK(caught&&calls==1&&liveInstances==0&&vk.instance==VK_NULL_HANDLE);
 reset({VK_ERROR_LAYER_NOT_PRESENT,VK_ERROR_LAYER_NOT_PRESENT,VK_ERROR_OUT_OF_HOST_MEMORY});caught=false;try{vk_create_instance();}catch(const std::runtime_error&){caught=true;}CHECK(caught&&calls==3&&liveInstances==0&&vk.instance==VK_NULL_HANDLE);
 std::printf("%d actual instance creation checks passed\n",checks);
}
