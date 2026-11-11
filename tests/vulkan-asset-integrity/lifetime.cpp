#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <stdexcept>
#include "vulkan/vulkan_core.h"
#include "vk_load_profile.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
struct image_t {int internalFormat;VkImage handle;VkImageView view;VkDeviceMemory residentMemory;size_t residentSourceBytes;int mipLevels;};
static struct {VkDevice device;} vk;
static struct {uint64_t imageGpuCreateUs;} vkLoadStats;
static std::unordered_map<image_t *,int> activeResidentTextures;
static unsigned waits,destroyed,invalidated;
static VkResult idleResult=VK_SUCCESS;
static void vk_flush_image_uploads(){}
static VkResult qvkDeviceWaitIdle(VkDevice){++waits;return idleResult;}
// The historical helper logged failures and continued; retain that contract
// when compiling the failing-before production snapshot.
static void vk_wait_idle(){vk_flush_image_uploads();(void)qvkDeviceWaitIdle(vk.device);}
static const char *vk_result_string(VkResult){return "test failure";}
enum {ERR_DROP};
static struct {static void Error(int,const char *,...){throw std::runtime_error("renderer error");}} ri;
static void R_InvalidateResidentTexture(image_t *){++invalidated;}
static void qvkDestroyImage(VkDevice,VkImage,const VkAllocationCallbacks *){CHECK(waits==1);++destroyed;}
static void qvkDestroyImageView(VkDevice,VkImageView,const VkAllocationCallbacks *){CHECK(waits==1);++destroyed;}
#include "replace_image.h"
int main(){
    // Cinematic scratch resize replaces an ordinary image used by earlier frames.
    image_t scratch={};scratch.handle=(VkImage)(uintptr_t)1;scratch.view=(VkImageView)(uintptr_t)2;
    vk_create_image(&scratch,256,128,1);
    CHECK(waits==1 && destroyed==2 && !scratch.handle && !scratch.view);
    waits=destroyed=invalidated=0;
    image_t fresh={};vk_create_image(&fresh,64,64,1);
    CHECK(waits==0 && destroyed==0);
    image_t resident={};resident.handle=(VkImage)(uintptr_t)3;resident.view=(VkImageView)(uintptr_t)4;
    resident.residentMemory=(VkDeviceMemory)(uintptr_t)5;resident.residentSourceBytes=64;
    activeResidentTextures[&resident]=1;
    vk_create_image(&resident,128,128,1);
    CHECK(waits==1 && destroyed==0 && invalidated==1 && activeResidentTextures.empty());
    CHECK(!resident.handle && !resident.view && !resident.residentMemory && !resident.residentSourceBytes);
    waits=destroyed=0;idleResult=VK_ERROR_OUT_OF_HOST_MEMORY;
    scratch.handle=(VkImage)(uintptr_t)6;scratch.view=(VkImageView)(uintptr_t)7;
    bool failed=false;try{vk_create_image(&scratch,512,512,1);}catch(const std::runtime_error&){failed=true;}
    CHECK(failed && waits==1 && !destroyed && scratch.handle && scratch.view);
    std::puts("Replacing an existing image waits; fresh image creation does not.");
}
