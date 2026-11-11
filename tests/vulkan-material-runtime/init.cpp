#include "vulkan/vulkan_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
typedef unsigned char byte;
typedef unsigned int uint;
const int NUM_COMMAND_BUFFERS=2,ERR_FATAL=1;
#define ARRAY_LEN(a) (sizeof(a)/sizeof((a)[0]))
static void require(bool value,const char *message) {if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static int fatalCalls;
struct {void Error(int,const char *,...){++fatalCalls;throw std::runtime_error("driver failure");}} ri;
static void vk_debug(const char *,...) {}
static const char *vk_result_string(VkResult) {return "injected";}
#include "vk_result_check.h"
#include "allocation-macros.h"
#define VK_SET_OBJECT_NAME(...)
static const char *va(const char *,...){return "fixture";}
static void Com_Memset(void *dst,int value,size_t count){std::memset(dst,value,count);}
struct vk_storage_buffer_t {VkBuffer buffer;VkDeviceMemory memory;VkDescriptorSet descriptor;byte *buffer_ptr;};
struct command_t {
    VkDescriptorSet uniform_descriptor;
    VkBuffer vertex_buffer,indirect_buffer;
    VkDeviceMemory vertex_buffer_memory,indirect_buffer_memory;
    byte *vertex_buffer_ptr,*indirect_buffer_ptr;
    uint vertex_buffer_offset,indirect_buffer_offset;
};
static struct {
    VkDevice device;VkDescriptorPool descriptor_pool;
    VkDescriptorSetLayout set_layout_storage,set_layout_uniform,set_layout_sampler;
    VkDescriptorSet color_descriptor,refraction_extract_descriptor,bloom_image_descriptor[2],dglow_image_descriptor[2];
    VkImageView color_image_view;
    struct {VkDescriptorSet color_descriptor;} screenMap;
    bool refractionActive,bloomActive,dglowActive,dedicatedAllocation;
    vk_storage_buffer_t storage;command_t tess[NUM_COMMAND_BUFFERS];
    VkDeviceMemory indirect_buffer_memory,geometry_buffer_memory;
    VkDeviceSize indirect_buffer_size,indirect_buffer_size_new,geometry_buffer_size,geometry_buffer_size_new;
    int stats;
} vk;
static byte mapped[8192];
static int descriptorCalls,mapCalls,bindCalls,updates,invalidUpdates,afterFailedDescriptor;
static int failDescriptorAt,failMapAt,failBindAt;
static bool descriptorFailed;
static bool requireDedicated,differentRequirements;
static int bufferCalls;
struct Allocation {VkBuffer dedicatedBuffer;VkDeviceSize size;};
static std::vector<Allocation> allocations;
static VkResult qvkCreateBuffer(VkDevice,const VkBufferCreateInfo *,const VkAllocationCallbacks *,VkBuffer *out){*out=(VkBuffer)(uintptr_t)++bufferCalls;return VK_SUCCESS;}
static void qvkGetBufferMemoryRequirements(VkDevice,VkBuffer buffer,VkMemoryRequirements *out){
    bool second=differentRequirements&&(uintptr_t)buffer==2;
    out->size=second?512:256;out->alignment=second?512:256;out->memoryTypeBits=second?2:1;
}
static void qvkGetBufferMemoryRequirements2KHR(VkDevice device,const VkBufferMemoryRequirementsInfo2KHR *in,VkMemoryRequirements2KHR *out){
    qvkGetBufferMemoryRequirements(device,in->buffer,&out->memoryRequirements);
    static_cast<VkMemoryDedicatedRequirementsKHR *>(out->pNext)->requiresDedicatedAllocation=requireDedicated?VK_TRUE:VK_FALSE;
}
static void qvkGetImageMemoryRequirements(VkDevice,VkImage,VkMemoryRequirements *){}
static void qvkGetImageMemoryRequirements2KHR(VkDevice,const VkImageMemoryRequirementsInfo2KHR *,VkMemoryRequirements2KHR *){}
static uint vk_find_memory_type(uint bits,uint){return bits==2?1:0;}
static VkResult qvkAllocateMemory(VkDevice,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *,VkDeviceMemory *out){
    Allocation a={VK_NULL_HANDLE,info->allocationSize};
    if(info->pNext){const auto *dedicated=static_cast<const VkMemoryDedicatedAllocateInfoKHR *>(info->pNext);a.dedicatedBuffer=dedicated->buffer;}
    allocations.push_back(a);*out=(VkDeviceMemory)(uintptr_t)allocations.size();return VK_SUCCESS;
}
static VkResult qvkMapMemory(VkDevice,VkDeviceMemory memory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void **out){++mapCalls;*out=mapped+((uintptr_t)memory-1)*1024;return mapCalls==failMapAt?VK_ERROR_MEMORY_MAP_FAILED:VK_SUCCESS;}
static VkResult qvkBindBufferMemory(VkDevice,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset){
    ++bindCalls;
    if(requireDedicated){const auto &a=allocations[(uintptr_t)memory-1];require(a.dedicatedBuffer==buffer&&offset==0&&a.size==256,"required dedicated buffer must bind its own exact allocation at zero");}
    return bindCalls==failBindAt?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
#include "vk_memory_requirements.h"
static VkResult qvkAllocateDescriptorSets(VkDevice,const VkDescriptorSetAllocateInfo *,VkDescriptorSet *out){
    ++descriptorCalls;
    if(descriptorCalls==failDescriptorAt){descriptorFailed=true;*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_POOL_MEMORY;}
    *out=(VkDescriptorSet)(uintptr_t)(descriptorCalls+10);return VK_SUCCESS;
}
static void recordUpdate(VkDescriptorSet set){++updates;if(!set)++invalidUpdates;if(descriptorFailed)++afterFailedDescriptor;}
static void qvkUpdateDescriptorSets(VkDevice,uint,const VkWriteDescriptorSet *desc,uint,const VkCopyDescriptorSet *){recordUpdate(desc->dstSet);}
static void vk_update_uniform_descriptor(VkDescriptorSet set,VkBuffer){recordUpdate(set);}
static void vk_update_attachment_descriptors(){
    recordUpdate(vk.color_descriptor);recordUpdate(vk.refraction_extract_descriptor);recordUpdate(vk.screenMap.color_descriptor);
    for(auto set:vk.bloom_image_descriptor)recordUpdate(set);
    for(auto set:vk.dglow_image_descriptor)recordUpdate(set);
}
#include "storage.h"
#include "descriptors.h"
#include "indirect.h"
#include "vertex.h"
static void reset(){
    vk={};descriptorCalls=mapCalls=bindCalls=updates=invalidUpdates=afterFailedDescriptor=fatalCalls=0;
    failDescriptorAt=failMapAt=failBindAt=0;descriptorFailed=false;
    requireDedicated=differentRequirements=false;bufferCalls=0;allocations.clear();
    std::memset(mapped,0xa7,sizeof(mapped));
    vk.color_image_view=(VkImageView)(uintptr_t)1;
    vk.refractionActive=vk.bloomActive=vk.dglowActive=true;
}
static void operation(int kind){
    if(kind==0)vk_create_storage_buffer(&vk.storage,256,"fixture");
    else if(kind==1)vk_create_indirect_buffer(256);
    else vk_create_vertex_buffer(256);
}
static bool failed(int kind){try{operation(kind);}catch(const std::runtime_error &){return true;}return false;}
int main(int argc,char **argv){
    require(argc==2,"expected case");std::string part=argv[1];
    if(part=="descriptor-failure"){
        for(int at=1;at<=10;++at){reset();failDescriptorAt=at;bool caught=false;
            try{vk_init_descriptors();}catch(const std::runtime_error &){caught=true;}
            require(caught&&fatalCalls==1,"every descriptor failure must stop initialization");
            require(descriptorCalls==at&&afterFailedDescriptor==0&&invalidUpdates==0,"failed descriptors must not be used or followed by further allocation");
        }
    }else if(part=="map-failure"){
        for(int kind=0;kind<3;++kind){reset();failMapAt=1;
            require(failed(kind)&&fatalCalls==1,"map failure must abort before pointer use");
            require(bindCalls==0,"map failure must stop before binding");
            require(mapped[0]==0xa7,"failed storage map must not clear output memory");
            require(vk.tess[0].vertex_buffer_ptr==nullptr&&vk.tess[0].indirect_buffer_ptr==nullptr,"failed map must not publish geometry pointers");
        }
    }else if(part=="bind-failure"){
        for(int kind=0;kind<3;++kind)for(int at=1;at<=(kind?2:1);++at){reset();failBindAt=at;
            require(failed(kind)&&fatalCalls==1,"buffer bind failure must abort initialization");
            require(bindCalls==at,"bind failure must stop further binding");
            require((kind!=1||vk.tess[at-1].indirect_buffer_ptr==nullptr)&&(kind!=2||vk.tess[at-1].vertex_buffer_ptr==nullptr),"failed buffer binding must not publish pointer");
        }
    }else if(part=="dedicated-controls"||part=="different-requirements"){
        for(int kind=0;kind<3;++kind){reset();vk.dedicatedAllocation=true;
            requireDedicated=part=="dedicated-controls";differentRequirements=part=="different-requirements";
            require(!failed(kind),"dedicated/differing allocation path must initialize");
            require(allocations.size()==size_t(kind?2:1),"each independent buffer must receive one allocation");
            for(size_t i=0;i<allocations.size();++i){
                require(allocations[i].size==VkDeviceSize(differentRequirements&&i==1?512:256),"individual memory requirements must be respected");
                require((allocations[i].dedicatedBuffer!=VK_NULL_HANDLE)==requireDedicated,"dedicated chain only when required");
            }
            if(kind)require((kind==1?vk.indirect_buffer_memory:vk.geometry_buffer_memory)==VK_NULL_HANDLE,"individual allocation must not create shared arena");
        }
    }else if(part=="dedicated-failure"){
        for(int kind=0;kind<3;++kind)for(int at=1;at<=(kind?2:1);++at)for(int type=0;type<2;++type){
            reset();vk.dedicatedAllocation=true;requireDedicated=true;
            if(type)failBindAt=at;else failMapAt=at;
            require(failed(kind)&&fatalCalls==1,"dedicated map/bind failure must stop before use");
            require(mapCalls==at&&bindCalls==at-(type?0:1),"dedicated failure must stop further map/bind calls");
        }
    }else if(part=="init-controls"){
        reset();vk_init_descriptors();require(descriptorCalls==10&&invalidUpdates==0&&updates==10,"successful descriptors unchanged");
        for(int kind=0;kind<3;++kind){reset();require(!failed(kind)&&fatalCalls==0,"successful buffer initialization unchanged");require(mapCalls==1&&bindCalls==(kind?2:1),"successful buffer map/bind counts unchanged");require(allocations.size()==1,"ordinary buffers preserve shared allocation");}
    }else require(false,"unknown case");
    return 0;
}
