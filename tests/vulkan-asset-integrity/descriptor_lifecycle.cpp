#include "vulkan/vulkan_core.h"
#include "vk_texture_context.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using qboolean=int;
using imgFlags_t=int;
enum {qfalse,qtrue};
enum {ERR_DROP,ERR_FATAL,PRINT_ALL,PRINT_DEVELOPER,TAG_IMAGE_T};
enum {IMGFLAG_MIPMAP=1,IMGFLAG_NOSCALE=2,IMGFLAG_CLAMPTOBORDER=4,IMGFLAG_CLAMPTOEDGE=8,IMGFLAG_NO_COMPRESSION=16,IMGFLAG_PICMIP=32};
enum {GL_NEAREST=0x2600,GL_LINEAR=0x2601,GL_LINEAR_MIPMAP_LINEAR=0x2703};
#define MAX_QPATH 64
#define Com_Memset std::memset
struct image_t {
    char *imgName;
    image_t *next;
    int width,height,flags;
    VkSamplerAddressMode wrapClampMode;
    VkImage handle;
    VkImageView view;
    VkDescriptorSet descriptor_set;
};
struct Cvar {int integer;bool modified;} smart,modeCvar;
static Cvar *r_smartpicmip=&smart,*r_textureMode=&modeCvar;
static struct {struct {unsigned count;image_t *items[16];}images;}tr={};
static struct {
    bool active;VkDevice device;VkDescriptorPool descriptor_pool;VkDescriptorSetLayout set_layout_sampler;
    struct {int filter_min,filter_max;}samplers;
}vk={};
static image_t *hashTable[1];
static std::vector<void *> allocations;
static void *Z_Malloc(size_t size,int){void *p=std::malloc(size);CHECK(p);std::memset(p,0xcc,size);allocations.push_back(p);return p;}
static int generateHashValue(const char *){return 0;}
static const char *Q_stristr(const char *a,const char *b){return std::strstr(a,b);}
static int Q_stricmpn(const char *a,const char *b,int n){return std::strncmp(a,b,n);}
static void R_AddImageToPool(image_t *image){CHECK(tr.images.count<16);tr.images.items[tr.images.count++]=image;}
static int deferred;
static struct Imports {
    void Error(int,const char *,...){throw std::runtime_error("engine error");}
    void Printf(int level,const char *format,...){if(level==PRINT_DEVELOPER && std::strstr(format,"defer sampler"))++deferred;}
}ri;
static void vk_debug(const char *,...){}
static const char *vk_result_string(VkResult){return "injected";}
#include "vk_result_check.h"
struct textureMode_t {int minimize,maximize;};
static textureMode_t selected={GL_NEAREST,GL_NEAREST};
static const textureMode_t *GetTextureMode(const char *){return &selected;}
static int gl_filter_min,gl_filter_max,generation=1,destroys,attachments,waits;
static void vk_wait_idle(){++waits;}
static void vk_destroy_samplers(){++destroys;++generation;}
static void vk_update_attachment_descriptors(){++attachments;}
struct Vk_Sampler_Def {VkSamplerAddressMode address_mode;int gl_mag_filter,gl_min_filter;qboolean noAnisotropy;};
struct Sampler {VkSampler handle;Vk_Sampler_Def definition;int generation;};
static std::vector<Sampler> samplers;
static VkSampler vk_find_sampler(const Vk_Sampler_Def *definition){
    auto handle=(VkSampler)(uintptr_t(samplers.size()+1));
    samplers.push_back({handle,*definition,generation});return handle;
}
struct Write {VkDescriptorSet descriptor;VkImageView view;Sampler sampler;};
static std::vector<Write>writes;
static int descriptorAllocations;
static VkResult qvkAllocateDescriptorSets(VkDevice,const VkDescriptorSetAllocateInfo *,VkDescriptorSet *out){
    *out=(VkDescriptorSet)(uintptr_t(++descriptorAllocations));return VK_SUCCESS;
}
static void qvkUpdateDescriptorSets(VkDevice,uint32_t count,const VkWriteDescriptorSet *write,uint32_t,const VkCopyDescriptorSet *){
    CHECK(count==1 && write->dstSet!=VK_NULL_HANDLE && write->pImageInfo->imageView!=VK_NULL_HANDLE);
    CHECK(write->descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    CHECK(write->pImageInfo->imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto &sampler=samplers[size_t((uintptr_t)write->pImageInfo->sampler)-1];
    CHECK(sampler.generation==generation);
    writes.push_back({write->dstSet,write->pImageInfo->imageView,sampler});
}
#include "descriptor_lifecycle.h"
static void makeResident(image_t *image,int levels){
    image->handle=(VkImage)(uintptr_t(200+descriptorAllocations));
    image->view=(VkImageView)(uintptr_t(300+descriptorAllocations));
    vk_assign_image_descriptor(image,levels);
}
static image_t *ready(const char *name,int flags,int levels){
    auto *image=R_AllocImage(name,8,8,flags);makeResident(image,levels);return image;
}
int main(int argc,char **argv){
    CHECK(argc==2);const std::string part=argv[1];vk.active=true;
    // Both production-registered images are fully ready before this test's
    // filtering change; non-mip descriptors must be refreshed after destruction.
    auto *mip=ready("textures/ready-mip",IMGFLAG_MIPMAP,4);
    auto *plain=ready("textures/ready-plain",0,1);
    image_t *pending=nullptr;
    if(part!="valid"){
        pending=R_AllocImage("textures/queued",8,8,IMGFLAG_MIPMAP);
        CHECK(tr.images.count==3 && tr.images.items[2]==pending);
        CHECK(!pending->handle && !pending->view && !pending->descriptor_set);
    }
    writes.clear();
    vk_texture_mode("nearest",qfalse);
    CHECK(writes.size()==2 && deferred==(part=="valid"?0:1) && destroys==1 && attachments==1 && waits==1);
    CHECK(writes[0].descriptor==mip->descriptor_set && writes[1].descriptor==plain->descriptor_set);
    CHECK(writes[0].sampler.definition.gl_min_filter==GL_NEAREST);
    CHECK(writes[1].sampler.definition.gl_min_filter==GL_LINEAR && writes[1].sampler.definition.noAnisotropy);
    if(part=="pending_complete"){
        makeResident(pending,4);
        CHECK(writes.back().descriptor==pending->descriptor_set);
        CHECK(writes.back().sampler.definition.gl_min_filter==GL_NEAREST);
        const int before=destroys;
        selected={GL_LINEAR_MIPMAP_LINEAR,GL_LINEAR};
        vk_texture_mode("trilinear",qfalse);
        CHECK(destroys==before+1 && writes.size()==6);
        CHECK(writes.back().descriptor==pending->descriptor_set);
        CHECK(writes.back().sampler.definition.gl_min_filter==GL_LINEAR_MIPMAP_LINEAR);
    }else if(part=="resident_reuse"){
        // The resident-cache hit branch restores a retained handle/view into a
        // fresh R_AllocImage then calls this actual descriptor-assignment helper.
        auto *reused=R_AllocImage("textures/reused",8,8,IMGFLAG_MIPMAP);
        reused->handle=mip->handle;reused->view=mip->view;
        vk_assign_image_descriptor(reused,4);
        CHECK(writes.back().descriptor==reused->descriptor_set && writes.back().view==mip->view);
        CHECK(writes.back().sampler.definition.gl_min_filter==GL_NEAREST);
    }else CHECK(part=="pending" || part=="valid");
    for(void *allocation:allocations)std::free(allocation);
    std::puts("descriptor lifecycle passed");
}
