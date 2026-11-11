#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "vulkan/vulkan_core.h"
#include "vk_texture_context.h"
#include "vk_texture_scratch.h"
#include "vk_load_profile.h"
#define STB_DXT_IMPLEMENTATION
#include "utils/stb_dxt.h"
#undef MAX
#undef MIN
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define Com_Memset std::memset
#define Com_Memcpy std::memcpy
using byte = unsigned char;
using qboolean = int;
enum {qfalse, qtrue, ERR_DROP};
enum {IMGFLAG_MIPMAP=1, IMGFLAG_PICMIP=2, IMGFLAG_NOSCALE=4, IMGFLAG_LIGHTMAP=8,
      IMGFLAG_NO_COMPRESSION=16, IMGFLAG_COLORSHIFT=32, IMGFLAG_NOLIGHTSCALE=64};
const int IMAGE_UPLOAD_SCRATCH_MIN=64, IMAGE_RESAMPLE_SCRATCH_MIN=32;
struct image_t {int width,height,flags,internalFormat;};
struct Image_Upload_Data {byte *buffer; int buffer_size,mip_levels,base_level_width,base_level_height;};
struct Cvar {int integer; bool modified;};
static Cvar setting={0}, simple={1}, *r_roundImagesDown=&setting, *r_nomip=&setting,
    *r_picmip=&setting, *r_texturebits=&setting, *r_colorMipLevels=&setting, *r_simpleMipMaps=&simple,
    *r_textureMode=&setting;
static struct {bool mapLoading; struct {unsigned count; image_t **items;} images;} tr;
static struct {int maxTextureSize; bool deviceSupportsGamma;} glConfig={4096,false};
static struct {int compressed_format; bool fboActive; struct {int filter_min,filter_max;} samplers;} vk;
static byte s_gammatable[256],s_intensitytable[256];
static struct {uint64_t imagePrepareUs;} vkLoadStats;
static struct {int upload,resample,mip,compress;} s_imageScratch={0,1,2,3};
static std::vector<byte> buffers[4];
static byte *R_ImageScratchAlloc(int *slot,size_t bytes){buffers[*slot].resize(bytes);return buffers[*slot].data();}
static struct {
    static void *Hunk_AllocateTempMemory(int size){return std::malloc(size);}
    static void Hunk_FreeTempMemory(void *data){std::free(data);}
    static void Error(int,const char *,...){throw std::runtime_error("renderer error");}
    static void Printf(int,const char *,...){}
} ri;
enum {PRINT_ALL};
struct textureMode_t {int minimize,maximize;};
static textureMode_t newMode={1,2};
static textureMode_t *GetTextureMode(const char *){return &newMode;}
static int gl_filter_min,gl_filter_max,samplersDestroyed,attachmentsRefreshed;
static std::vector<std::pair<image_t *,qboolean>> refreshed;
static void vk_wait_idle(){}
static void vk_destroy_samplers(){++samplersDestroyed;}
static void vk_update_attachment_descriptors(){++attachmentsRefreshed;}
static void vk_update_descriptor_set(image_t *image,qboolean mipmap){refreshed.push_back({image,mipmap});}
static void R_ColorShiftLightingBytes(byte *,byte *,qboolean){CHECK(false);}
#include "production.h"

static void thinLimit(){
    for(int axis=0;axis<2;++axis) for(int maximum : {1,2,8,64}) {
        const int w=axis?1:256,h=axis?256:1;
        std::vector<byte> pixels(size_t(w)*h*4,101);
        TexturePreparationContext context={}; context.maxTextureSize=maximum; context.simpleMipMaps=1;
        image_t image={w,h,IMGFLAG_MIPMAP|IMGFLAG_NO_COMPRESSION|IMGFLAG_NOLIGHTSCALE,0};
        Image_Upload_Data upload={}; TexturePreparationBuffers scratch(65536);
        CHECK(vk_generate_image_upload_data(&image,pixels.data(),&upload,&context,&scratch));
        CHECK(upload.base_level_width==(axis?1:maximum));
        CHECK(upload.base_level_height==(axis?maximum:1));
        CHECK(upload.buffer_size>0 && upload.mip_levels>0);
        for(int i=0;i<upload.buffer_size;++i) CHECK(upload.buffer[i]==101);
    }
}
static void hqTail(){
    for(int axis=0;axis<2;++axis) for(int size : {2,4,8,32}) for(bool inPlace : {false,true}) {
        const int w=axis?1:size,h=axis?size:1;
        std::vector<unsigned> pixels(size),output(size/2,0xcdcdcdcdu);
        for(int i=0;i<size;++i) for(int c=0;c<4;++c) reinterpret_cast<byte *>(pixels.data())[i*4+c]=byte((i*29+c*11)&255);
        const auto original=pixels;
        unsigned *destination=inPlace?pixels.data():output.data();
        CHECK(R_MipMap2(destination,pixels.data(),w,h,nullptr));
        for(int i=0;i<size/2;++i) for(int c=0;c<4;++c) {
            const byte *bytes=reinterpret_cast<const byte *>(original.data());
            const unsigned expected=(bytes[((i*2-1)&(size-1))*4+c]+2*bytes[((i*2)&(size-1))*4+c]+
                2*bytes[((i*2+1)&(size-1))*4+c]+bytes[((i*2+2)&(size-1))*4+c])/6;
            CHECK(reinterpret_cast<byte *>(destination)[i*4+c]==expected);
        }
    }
}
static void ordinary(){
    // Existing two-dimensional HQ output remains the exact 4x4 wrapped filter.
    for(int size : {2,4,8,16}) {
        std::vector<unsigned> pixels(size*size),output(size*size/4);
        for(size_t i=0;i<pixels.size()*4;++i) reinterpret_cast<byte *>(pixels.data())[i]=byte(i*13+7);
        CHECK(R_MipMap2(output.data(),pixels.data(),size,size,nullptr));
        const int weights[]={1,2,2,1};
        for(int y=0;y<size/2;++y) for(int x=0;x<size/2;++x) for(int c=0;c<4;++c) {
            unsigned sum=0;
            for(int dy=0;dy<4;++dy) for(int dx=0;dx<4;++dx)
                sum+=weights[dy]*weights[dx]*reinterpret_cast<byte *>(pixels.data())[
                    (((y*2+dy-1)&(size-1))*size+((x*2+dx-1)&(size-1)))*4+c];
            CHECK(reinterpret_cast<byte *>(output.data())[(y*(size/2)+x)*4+c]==sum/36);
        }
    }
}
static void samplerRefresh(){
    image_t images[3]={{4,4,IMGFLAG_MIPMAP,0},{4,4,0,0},{4,4,IMGFLAG_NOSCALE,0}};
    image_t *items[]={images,images+1,images+2};
    tr.images.count=3;tr.images.items=items;
    vk_texture_mode("changed",qfalse);
    CHECK(samplersDestroyed==1 && attachmentsRefreshed==1);
    CHECK(refreshed.size()==3);
    for(size_t i=0;i<3;++i) {
        CHECK(refreshed[i].first==items[i]);
        CHECK(refreshed[i].second==(i==0?qtrue:qfalse));
    }
    vk_texture_mode("same",qfalse);
    CHECK(samplersDestroyed==1 && refreshed.size()==3);
}
int main(int argc,char **argv){
    CHECK(argc==2);
    if(!std::strcmp(argv[1],"thin-limit"))thinLimit();
    else if(!std::strcmp(argv[1],"hq-tail"))hqTail();
    else if(!std::strcmp(argv[1],"ordinary"))ordinary();
    else if(!std::strcmp(argv[1],"sampler-refresh"))samplerRefresh();
    else CHECK(false);
    std::puts("Vulkan asset integrity checks passed.");
}
