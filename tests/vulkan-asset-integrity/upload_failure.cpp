#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
using byte=unsigned char;
enum {ERR_DROP,qfalse};
struct image_t {const char *imgName;int width,height;unsigned flags;int uploadWidth,uploadHeight;};
struct Image_Upload_Data {byte *buffer;int buffer_size,mip_levels,base_level_width,base_level_height;};
static bool preparationSucceeds;
static unsigned creates,uploads;
static byte pixels[16];
static bool vk_generate_image_upload_data(image_t *,byte *,Image_Upload_Data *out){
    if(!preparationSucceeds)return false;
    *out={pixels,16,1,2,2};return true;
}
static struct {static void Error(int,const char *,...){throw std::runtime_error("preparation failed");}} ri;
static void vk_create_image(image_t *,int w,int h,int levels){CHECK(w==2 && h==2 && levels==1);++creates;}
static void vk_upload_image_data(image_t *,int,int,int w,int h,int levels,byte *data,int bytes,int){CHECK(w==2 && h==2 && levels==1 && data==pixels && bytes==16);++uploads;}
#include "upload_failure.h"
int main(){
    image_t image={"texture/preparation-failure",2,2,0,0,0};
    bool failed=false;try{vk_upload_image(&image,pixels);}catch(const std::runtime_error&){failed=true;}
    CHECK(failed && !creates && !uploads && !image.uploadWidth && !image.uploadHeight);
    preparationSucceeds=true;vk_upload_image(&image,pixels);
    CHECK(creates==1 && uploads==1 && image.uploadWidth==2 && image.uploadHeight==2);
    std::puts("Preparation failure never publishes uninitialized GPU upload metadata.");
}
