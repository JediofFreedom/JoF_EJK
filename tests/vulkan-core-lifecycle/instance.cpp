#include "vulkan/vulkan_core.h"
#include "vk_api_version.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <deque>
#include <stdexcept>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}++checks;}while(0)
#define PRINT_ALL 0
#define PRINT_DEVELOPER 1
#define ERR_FATAL 2

typedef int qboolean;const qboolean qtrue=1,qfalse=0;
static int checks,calls,liveInstances;
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
 *count=1;if(properties){*properties={};std::strcpy(properties->extensionName,VK_KHR_SURFACE_EXTENSION_NAME);}return VK_SUCCESS;
}
static VkResult qvkCreateInstance(const VkInstanceCreateInfo *info,const VkAllocationCallbacks*,VkInstance *instance){
 ++calls;layers.push_back(info->enabledLayerCount);
 VkResult result=results.empty()?VK_SUCCESS:results.front();if(!results.empty())results.pop_front();
 if(result==VK_SUCCESS){++liveInstances;*instance=reinterpret_cast<VkInstance>(uintptr_t(liveInstances));}
 return result;
}
static const char *vk_result_string(VkResult){return "injected result";}
#include "vk_result_check.h"
#include "instance.h"
static void reset(std::initializer_list<VkResult> sequence){vk={};calls=liveInstances=0;layers.clear();results=sequence;}
int main(){
 reset({VK_SUCCESS});vk_create_instance();CHECK(calls==1&&liveInstances==1&&vk.instance!=VK_NULL_HANDLE);CHECK(layers==std::deque<uint32_t>{1});
 reset({VK_ERROR_LAYER_NOT_PRESENT,VK_SUCCESS});vk_create_instance();CHECK(calls==2&&liveInstances==1);CHECK(layers==std::deque<uint32_t>({1,1}));
 reset({VK_ERROR_LAYER_NOT_PRESENT,VK_ERROR_LAYER_NOT_PRESENT,VK_SUCCESS});vk_create_instance();CHECK(calls==3&&liveInstances==1);CHECK(layers==std::deque<uint32_t>({1,1,0}));
 reset({VK_ERROR_INCOMPATIBLE_DRIVER});bool caught=false;try{vk_create_instance();}catch(const std::runtime_error&){caught=true;}CHECK(caught&&calls==1&&liveInstances==0&&vk.instance==VK_NULL_HANDLE);
 reset({VK_ERROR_LAYER_NOT_PRESENT,VK_ERROR_LAYER_NOT_PRESENT,VK_ERROR_OUT_OF_HOST_MEMORY});caught=false;try{vk_create_instance();}catch(const std::runtime_error&){caught=true;}CHECK(caught&&calls==3&&liveInstances==0&&vk.instance==VK_NULL_HANDLE);
 std::printf("%d actual instance creation checks passed\n",checks);
}
