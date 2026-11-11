#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include "vulkan/vulkan_core.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
#define Com_Memset std::memset
#define USE_VBO
#define USE_VBO_SS
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#include "shader_enum.h"
using vec4_t=float[4];using vec2_t=float[2];using color4ub_t=unsigned char[4];
struct sprite_t {float fields[16];};
struct Vk_Pipeline_Def {Vk_Shader_Type shader_type; bool vbo_ghoul2,vbo_mdv; unsigned surface_sprite_flags;};
static bool is_ghoul2_vbo,is_mdv_vbo;
static unsigned num_binds,num_attrs;
static VkVertexInputBindingDescription bindings[10];
static VkVertexInputAttributeDescription attribs[8];
static unsigned get_mdxm_stride(){return 64;}
static unsigned get_mdv_stride(){return 56;}
enum {ERR_DROP};
static struct {static void Error(int,const char *,...){throw std::runtime_error("pipeline error");}} ri;
#include "vertex_inputs.h"
struct VBO_t {VkBuffer buffer; VkDeviceSize offsets[10];};
static struct {VBO_t *vbo_model;} tess;
static VkBuffer shade_bufs[10];
static int bind_base,bind_count;
static struct {VkCommandBuffer command_buffer;VkDeviceSize vbo_offset[10];} command;
static struct {decltype(command) *cmd;} vk={&command};
enum {TESS_ST1=1,TESS_ST2=2,TESS_ENV=4};
static unsigned boundCount;static VkDeviceSize boundOffsets[10];
static void qvkCmdBindVertexBuffers(VkCommandBuffer,unsigned first,unsigned count,const VkBuffer *,const VkDeviceSize *offsets){
    CHECK(first==0 && count==10);boundCount=count;std::copy(offsets,offsets+count,boundOffsets);
}
#include "model_bindings.h"
static void inputs(){
    CHECK(TYPE_GENERIC_END>=TYPE_BLEND3_DST_COLOR_SRC_ALPHA_ENV);
    unsigned cases=0,maxBinds=0,maxAttrs=0;
    for(int mode=0;mode<3;++mode) for(int type=0;type<=TYPE_BLEND3_DST_COLOR_SRC_ALPHA_ENV;++type){
        Vk_Pipeline_Def def={static_cast<Vk_Shader_Type>(type),mode==1,mode==2,0};
        vk_push_vertex_input_binding_attribute(&def);
        CHECK(num_binds<=ARRAY_LEN(bindings) && num_attrs<=ARRAY_LEN(attribs));
        std::set<unsigned> usedBindings,locations;
        for(unsigned i=0;i<num_binds;++i)CHECK(usedBindings.insert(bindings[i].binding).second);
        bool st0=false;
        for(unsigned i=0;i<num_attrs;++i){
            CHECK(locations.insert(attribs[i].location).second);
            CHECK(usedBindings.count(attribs[i].binding));
            if(attribs[i].location==2 && attribs[i].binding==2 && attribs[i].format==VK_FORMAT_R32G32_SFLOAT)st0=true;
        }
        if(mode && (type==TYPE_REFRACTION || type>=TYPE_GENERIC_BEGIN)){
            CHECK(st0);
            CHECK(locations.count(5));
            if(mode==1)CHECK(locations.count(8) && locations.count(9));
        }
        maxBinds=std::max(maxBinds,num_binds);maxAttrs=std::max(maxAttrs,num_attrs);++cases;
    }
    Vk_Pipeline_Def sprites={TYPE_SINGLE_TEXTURE,false,false,1};
    vk_push_vertex_input_binding_attribute(&sprites);
    CHECK(num_binds==6 && num_attrs==6);
    VBO_t vbo={};vbo.offsets[2]=96;tess.vbo_model=&vbo;
    vk_vbo_bind_geometry_mdv(TESS_ENV);CHECK(boundCount==10 && boundOffsets[2]==96);
    vk_vbo_bind_geometry_ghoul2(TESS_ENV);CHECK(boundCount==10 && boundOffsets[2]==96);
    std::printf("%u pipeline cases; maximum %u bindings / %u attributes; model UV draw bindings passed.\n",cases,maxBinds,maxAttrs);
}
struct vec2 {float x,y;};struct vec3 {float x,y,z;};
static float observedSt;
static vec3 DeformPosition(vec3 position,vec3,vec2 st){observedSt=st.x;return position;}
static void deform(){
    for(float coordinate : {0.0f,0.25f,0.75f,1.0f}){
        vec3 position={1,2,3},normal={0,0,1};
        vec2 in_tex_coord0={coordinate,0.5f},frag_tex_coord0={std::numeric_limits<float>::quiet_NaN(),0};
        observedSt=-1;
#include "deform_call.h"
        CHECK(observedSt==coordinate);
    }
}
int main(int argc,char **argv){CHECK(argc==2);if(!std::strcmp(argv[1],"inputs"))inputs();else if(!std::strcmp(argv[1],"deform"))deform();else CHECK(false);}
