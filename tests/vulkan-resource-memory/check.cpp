#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <chrono>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "vk_texture_jobs.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
#define ERR_FATAL 1
#define ERR_DROP 2
#define PRINT_DEVELOPER 3
#define PRINT_ALL 0
#define MAX_QPATH 64
#define qfalse 0
typedef int fileHandle_t;
#define Com_Memcpy std::memcpy
static std::vector<unsigned char> cacheBlob;
struct Imports {
    void Error(int, const char *, ...) { throw std::runtime_error("engine error"); }
    void Printf(int, const char *, ...) {}
    long FS_FOpenFileRead(const char *,fileHandle_t *handle,int) { *handle=cacheBlob.empty()?0:1; return long(cacheBlob.size()); }
    int FS_Read(void *out,int length,fileHandle_t) { std::memcpy(out,cacheBlob.data(),length); return length; }
    void FS_FCloseFile(fileHandle_t) {}
} ri;
static void Com_Error(int, const char *, ...) { throw std::runtime_error("engine error"); }
static void vk_debug(const char *, ...) {}
static const char *vk_result_string(VkResult) { return "injected"; }
#include "vk_result_check.h"
struct State { VkDevice device; bool dedicatedAllocation; VkDeviceSize image_chunk_size; VkPhysicalDevice physical_device; VkPipelineCache pipelineCache; } vk = {};
static bool required;
static VkResult allocationResult=VK_SUCCESS, shaderResult=VK_SUCCESS;
static int oldImageQueries,oldBufferQueries,imageQueries,bufferQueries,allocations,binds,shaderCalls;
static VkImage dedicatedImage;
static VkBuffer dedicatedBuffer;
static VkDeviceSize allocatedSize, boundOffset;
static const uint32_t expectedWords[]={0x07230203,0x10000,0,5};
static const uint8_t *originalBytes;
static bool sameShaderAddress;
static void qvkGetImageMemoryRequirements(VkDevice,VkImage,VkMemoryRequirements *r) { ++oldImageQueries; *r={256,64,1}; }
static void qvkGetBufferMemoryRequirements(VkDevice,VkBuffer,VkMemoryRequirements *r) { ++oldBufferQueries; *r={256,64,1}; }
static void qvkGetImageMemoryRequirements2KHR(VkDevice,const VkImageMemoryRequirementsInfo2KHR *,VkMemoryRequirements2KHR *r) {
    ++imageQueries; r->memoryRequirements={256,64,1};
    static_cast<VkMemoryDedicatedRequirementsKHR *>(r->pNext)->requiresDedicatedAllocation=required;
}
static void qvkGetBufferMemoryRequirements2KHR(VkDevice,const VkBufferMemoryRequirementsInfo2KHR *,VkMemoryRequirements2KHR *r) {
    ++bufferQueries; r->memoryRequirements={256,64,1};
    static_cast<VkMemoryDedicatedRequirementsKHR *>(r->pNext)->requiresDedicatedAllocation=required;
}
static uint32_t vk_find_memory_type(uint32_t bits,VkMemoryPropertyFlags) { CHECK(bits==1); return 0; }
static VkResult qvkAllocateMemory(VkDevice,const VkMemoryAllocateInfo *a,const void *,VkDeviceMemory *out) {
    ++allocations; allocatedSize=a->allocationSize;
    if (a->pNext) {
        auto *d=static_cast<const VkMemoryDedicatedAllocateInfoKHR *>(a->pNext);
        CHECK(d->sType==VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR);
        dedicatedImage=d->image; dedicatedBuffer=d->buffer;
    }
    if(allocationResult==VK_SUCCESS) *out=(VkDeviceMemory)(uintptr_t(100+allocations));
    return allocationResult;
}
#include "allocation_macros.h"
#include "vk_memory_requirements.h"
static VkResult qvkCreateShaderModule(VkDevice,const VkShaderModuleCreateInfo *d,const void *,VkShaderModule *out) {
    ++shaderCalls;
    CHECK(d->codeSize==sizeof(expectedWords));
    CHECK(reinterpret_cast<uintptr_t>(d->pCode)%alignof(uint32_t)==0);
    CHECK(std::memcmp(d->pCode,expectedWords,sizeof(expectedWords))==0);
    sameShaderAddress=reinterpret_cast<const uint8_t *>(d->pCode)==originalBytes;
    if(shaderResult==VK_SUCCESS) *out=(VkShaderModule)(uintptr_t(55));
    return shaderResult;
}
#include "shader.h"
#include "vk_pipeline_cache_file.h"
struct Cvar { int integer; } cacheCvar={1};
static Cvar *r_pipelineCache=&cacheCvar;
static bool cacheSucceeds;
static int cacheCalls;
static std::vector<size_t> cacheSizes;
static void qvkGetPhysicalDeviceProperties(VkPhysicalDevice,VkPhysicalDeviceProperties *props) { *props={}; }
static void vk_pipeline_cache_path(char *path,size_t,const VkPhysicalDeviceProperties &) { std::strcpy(path,"cache"); }
static VkResult qvkCreatePipelineCache(VkDevice,const VkPipelineCacheCreateInfo *ci,const void *,VkPipelineCache *out) {
    ++cacheCalls; cacheSizes.push_back(ci->initialDataSize);
    // Deliberately poison a failed output: Vulkan does not promise it is null.
    *out=(VkPipelineCache)(uintptr_t(44));
    return cacheSucceeds && !ci->initialDataSize ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
#include "cache.h"
struct image_t { VkImage handle; VkDeviceMemory residentMemory; size_t residentSourceBytes; };
struct ImageChunk_t { VkDeviceMemory memory; VkDeviceSize used,size; uint32_t memoryType; int items; };
#define MAX_IMAGE_CHUNKS 16
#define IMAGE_CHUNK_SIZE 1024
#define PAD(x,a) (((x)+(a)-1)&~((a)-1))
#define VK_SET_OBJECT_NAME(...)
static const char *va(const char *,...) { return "diagnostic"; }
static void vk_restart_swapchain(const char *) { throw std::runtime_error("restart"); }
struct World { ImageChunk_t image_chunks[MAX_IMAGE_CHUNKS]; int num_image_chunks; } vk_world = {};
struct Ranges { bool allocate(size_t,size_t,size_t &offset) { offset=0; return true; } };
struct ResidentImagePage {
    VkDeviceMemory memory; uint32_t memoryType; Ranges ranges;
    ResidentImagePage(VkDeviceMemory m,size_t,uint32_t t):memory(m),memoryType(t){}
};
struct ResidentTexture { VkImage image; ResidentTexture(image_t *,size_t,std::shared_ptr<ResidentImagePage>,size_t){} };
static struct Cache { size_t limit()const { return 4096; } bool evictIdle(){return false;} } residentTextures;
static std::vector<std::weak_ptr<ResidentImagePage>> residentImagePages;
static size_t residentPageCount,residentTextureLimit=4,residentPageBytes;
static std::unordered_map<image_t *,std::shared_ptr<ResidentTexture>> activeResidentTextures;
static VkResult qvkBindImageMemory(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize offset) { ++binds; boundOffset=offset; return VK_SUCCESS; }
#include "image.h"
template<class F> static void mustThrow(F f) { bool threw=false; try { f(); } catch(const std::runtime_error &) { threw=true; } CHECK(threw); }
struct CopyFailure {
    CopyFailure(){}
    CopyFailure(const CopyFailure &){throw std::bad_alloc();}
    void operator()()const {}
};
int main(int argc,char **argv) {
    CHECK(argc==2); const std::string part=argv[1];
    vk.dedicatedAllocation=true; vk.image_chunk_size=1024;
    if(part.find("shader_")==0) {
        alignas(uint32_t) uint8_t bytes[sizeof(expectedWords)+4]={};
        uint8_t *input=bytes+(part=="shader_unaligned" ? 1 : 0);
        std::memcpy(input,expectedWords,sizeof(expectedWords)); originalBytes=input;
        if(part=="shader_invalid") {
            mustThrow([&]{SHADER_MODULE(NULL,sizeof(expectedWords));});
            mustThrow([&]{SHADER_MODULE(input,0);});
            mustThrow([&]{SHADER_MODULE(input,-4);});
            mustThrow([&]{SHADER_MODULE(input,3);}); CHECK(shaderCalls==0);
        } else if(part=="shader_failure") {
            shaderResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
            mustThrow([&]{SHADER_MODULE(input,sizeof(expectedWords));}); CHECK(shaderCalls==1);
        } else {
            CHECK(SHADER_MODULE(input,sizeof(expectedWords))!=VK_NULL_HANDLE);
            CHECK(sameShaderAddress==(part=="shader_aligned"));
        }
    } else if(part=="dedicated_image" || part=="pooled_image") {
        required=part=="dedicated_image";
        image_t image={(VkImage)(uintptr_t(30)),VK_NULL_HANDLE,required?64u:0u};
        vk_world.num_image_chunks=1;
        vk_world.image_chunks[0]={(VkDeviceMemory)(uintptr_t(25)),64,1024,0,1};
        allocate_and_bind_image_memory(&image);
        CHECK(imageQueries+oldImageQueries==1 && binds==1);
        CHECK(allocations==(required?1:0));
        CHECK(vk_world.num_image_chunks==(required?2:1));
        CHECK(boundOffset==(required?0:64));
        if(required) { CHECK(dedicatedImage==image.handle); CHECK(allocatedSize==256); CHECK(activeResidentTextures.empty()); }
    } else if(part=="dedicated_buffer" || part=="pooled_buffer" || part=="legacy_buffer" || part=="allocation_failure") {
        required=part=="dedicated_buffer";
        vk.dedicatedAllocation=part!="legacy_buffer";
        VkDeviceMemory memory=VK_NULL_HANDLE;
        const VkBuffer buffer=(VkBuffer)(uintptr_t(75));
        if(part=="allocation_failure") { allocationResult=VK_ERROR_OUT_OF_DEVICE_MEMORY; mustThrow([&]{vk_allocate_buffer_memory(buffer,0,&memory,"test");}); CHECK(memory==VK_NULL_HANDLE); }
        else { vk_allocate_buffer_memory(buffer,0,&memory,"test"); CHECK(memory!=VK_NULL_HANDLE); }
        CHECK(allocations==1 && allocatedSize==256);
        CHECK(dedicatedBuffer==(required?buffer:VK_NULL_HANDLE));
        CHECK(bufferQueries==(vk.dedicatedAllocation?1:0)); CHECK(oldBufferQueries==(vk.dedicatedAllocation?0:1));
    } else if(part.find("cache_")==0) {
        cacheSucceeds=part=="cache_success" || part=="cache_retry_success";
        const bool retry=part.find("retry")!=std::string::npos;
        if(retry) {
            cacheBlob.resize(PipelineCacheFile::HeaderSize+32);
            auto *data=cacheBlob.data()+PipelineCacheFile::HeaderSize;
            PipelineCacheFile::Write32(data,32);
            PipelineCacheFile::Write32(data+4,VK_PIPELINE_CACHE_HEADER_VERSION_ONE);
            VkPhysicalDeviceProperties props={};
            PipelineCacheFile::WriteHeader(cacheBlob.data(),data,32,props);
        }
        vk_create_pipeline_cache();
        CHECK(cacheCalls==(retry?2:1));
        CHECK((vk.pipelineCache!=VK_NULL_HANDLE)==cacheSucceeds);
        CHECK(cacheSizes.back()==0);
        if(retry) CHECK(cacheSizes.front()==32);
    } else {
        TextureWorkers pool; CHECK(pool.start(1));
        std::promise<void> finished; auto future=finished.get_future();
        int work=0,failed=0,completed=0;
        if(part=="worker_enqueue_failure") {
            CopyFailure bad; CHECK(!pool.submit(bad));
        }
        CHECK(pool.submit([&]{TextureWorkers::run([&]{++work;if(part=="worker_exception")throw std::bad_alloc();},[&]{++failed;},[&]{++completed; finished.set_value();});}));
        CHECK(future.wait_for(std::chrono::seconds(5))==std::future_status::ready);
        pool.shutdown(); CHECK(work==1 && completed==1 && failed==(part=="worker_exception"?1:0));
        CHECK(!pool.submit([]{}));
    }
    std::puts("passed");
}
