#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
#define PAD(v,a) (((v)+(a)-1)&~((a)-1))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define Com_Memcpy std::memcpy
using VkDeviceSize=uint64_t;
using VkDescriptorSet=uintptr_t;
enum {VK_PIPELINE_BIND_POINT_GRAPHICS};
enum {VK_DESC_TEXTURE0};
const uintptr_t VK_NULL_HANDLE=0;
enum {ERR_DROP,VK_DESC_UNIFORM,VK_DESC_UNIFORM_MAIN_BINDING,VK_DESC_UNIFORM_GLOBAL_BINDING};
struct vkUniform_t {unsigned char data[64];};
struct vkUniformGlobal_t {unsigned char data[128];};
static struct {VkDeviceSize vertex_buffer_offset;unsigned char *vertex_buffer_ptr;uintptr_t uniform_descriptor;uint32_t camera_ubo_offset,fogs_ubo_offset;uintptr_t command_buffer;struct {VkDescriptorSet current[1];} descriptor_set;} command;
static struct {decltype(command) *cmd;VkDeviceSize uniform_alignment,geometry_buffer_size;uint32_t geometry_buffer_size_new,uniform_item_size,uniform_global_item_size;uintptr_t pipeline_layout_surface_sprite;struct {VkDescriptorSet descriptor;} surface_sprites_ssbo[1];} vk={&command,64,256,0,64,128};
static struct {static void Error(int,const char *,...){throw std::runtime_error("range error");}} ri;
#define QINLINE inline
#include "log2pad.h"
static std::vector<uint32_t> publishedOffsets;
static void vk_reset_descriptor(int){}
static void vk_update_descriptor(int,uintptr_t){}
static void vk_update_descriptor_offset(int,uint32_t offset){publishedOffsets.push_back(offset);}
#include "uniform.h"
static unsigned descriptorBinds;
static void qvkCmdBindDescriptorSets(uintptr_t,int,uintptr_t,uint32_t,uint32_t,const VkDescriptorSet *,uint32_t count,const uint32_t *offsets){CHECK(count==7 && offsets[0]!=~0U);++descriptorBinds;}
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define SS_UNPACK_SSBO_OFFSET(x) 0
#define SS_UNPACK_SSBO_INDEX(x) 0
static void spriteSubmission(){
    const int fog_stage=0;
    vkUniform_t uniform={};struct {int fogNum;} tess={0};
    struct {VkDescriptorSet descriptor_set;} fog={};struct {decltype(fog) *fogImage;} tr={&fog};
    struct {struct {uint32_t ssbo_bits;} def;} storage={};auto *group=&storage;
    std::vector<unsigned char> memory(512,0xcd);command.vertex_buffer_ptr=memory.data();
    command.vertex_buffer_offset=224;vk.geometry_buffer_size_new=0;
    do {
#include "sprite_uniform.h"
    } while(false);
    CHECK(!descriptorBinds && vk.geometry_buffer_size_new>=320);
    command.vertex_buffer_offset=0;vk.geometry_buffer_size_new=0;
    do {
#include "sprite_uniform.h"
    } while(false);
    CHECK(descriptorBinds==1);
}
int main(){
    std::vector<unsigned char> memory(320,0xcd),payload(128,0x31);command.vertex_buffer_ptr=memory.data();
    command.vertex_buffer_offset=224;uint32_t offset=0;
    CHECK(vk_append_uniform(payload.data(),64,64)==~0U);
    CHECK(vk.geometry_buffer_size_new>=320 && command.vertex_buffer_offset==224);
    for(auto v:memory)CHECK(v==0xcd);
    vk.geometry_buffer_size_new=0;command.vertex_buffer_offset=224;
    CHECK(!vk_reserve_uniform(64,&offset) && vk.geometry_buffer_size_new>=320);
    vk.geometry_buffer_size_new=0;command.vertex_buffer_offset=224;
    vkUniform_t main={};CHECK(vk_push_uniform(&main)==~0U && publishedOffsets.empty());
    vk.geometry_buffer_size_new=0;command.vertex_buffer_offset=224;
    vkUniformGlobal_t global={};CHECK(vk_push_uniform_global(&global)==~0U && publishedOffsets.empty());
    // A reservation reaching the exact capacity is valid and leaves guards intact.
    vk.geometry_buffer_size_new=0;command.vertex_buffer_offset=128;
    CHECK(vk_append_uniform(payload.data(),64,128)==128 && command.vertex_buffer_offset==256);
    CHECK(!vk.geometry_buffer_size_new && memory[128]==0x31 && memory[191]==0x31 && memory[256]==0xcd);
    // The actual renderer log2pad cannot round values above2GiB without
    // wrapping its loop; the producer must reject them before calling it.
    command.vertex_buffer_offset=uint64_t(1)<<31;
    bool rejected=false;try{vk_append_uniform(payload.data(),64,64);}catch(const std::runtime_error&){rejected=true;}
    CHECK(rejected && !vk.geometry_buffer_size_new);
    spriteSubmission();
    std::puts("Uniform exhaustion schedules growth without publishing invalid offsets.");
}
