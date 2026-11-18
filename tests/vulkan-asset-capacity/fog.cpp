#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define VectorCopy4(a,b) std::memcpy(b,a,sizeof(float)*4)
using vec2_t=float[2];using vec3_t=float[3];using vec4_t=float[4];using mat4_t=float[16];using mat3x4_t=float[3][4];
#define GLOBAL_SHADER_C
#include "shaders/glsl/global.h"
struct fog_t {vec4_t surface,color;struct {float depthForOpaque;} parms;bool hasSurface;};
struct world_t {int numfogs;fog_t *fogs;};
struct trRefdef_t{};
static struct {world_t *world;} tr;
static struct {uint32_t fogs_ubo_offset;} command;
static struct {decltype(command) *cmd;uint32_t uniform_fogs_item_size;} vk={&command,sizeof(vkUniformFog_t)};
static std::vector<unsigned char> uploaded;
static uint32_t vk_append_uniform(const void *data,size_t size,uint32_t reserved){CHECK(size<=reserved);const auto *bytes=static_cast<const unsigned char *>(data);uploaded.assign(bytes,bytes+size);return 64;}
#include "fog.h"
int main(){
    static_assert(sizeof(vkUniformFog_t)<=16384,"Fog UBO must fit Vulkan's guaranteed minimum range");
    for(int count : {0,1,16,17,64,256}){
        std::vector<fog_t> fogs(count+1);world_t world={count+1,fogs.data()};tr.world=&world;
        for(int i=0;i<count;++i){fogs[i+1].surface[0]=float(i+1);fogs[i+1].color[0]=float(i)/256;fogs[i+1].parms.depthForOpaque=float(i+10);fogs[i+1].hasSurface=true;}
        vk_update_fog_constants(nullptr);
        CHECK(uploaded.size()==sizeof(vec4_t)+size_t(count)*sizeof(vkUniformFogEntry_t));
        CHECK(command.fogs_ubo_offset==64);
        const auto *data=reinterpret_cast<const vkUniformFog_t *>(uploaded.data());
        CHECK(data->num_fogs==count);
        for(int i=0;i<count;++i){CHECK(data->fogs[i].plane[0]==float(i+1));CHECK(data->fogs[i].color[0]==float(i)/256);}
    }
    tr.world=nullptr;vk_update_fog_constants(nullptr);CHECK(uploaded.size()==sizeof(vec4_t));
    std::printf("All256 legal BSP fog entries fit the %zu-byte shared C++/GLSL UBO.\n",sizeof(vkUniformFog_t));
}
