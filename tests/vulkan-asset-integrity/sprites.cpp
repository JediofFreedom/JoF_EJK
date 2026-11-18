#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
const char PATH_SEP='/';
struct image_t {const char *imgName;};
struct textureBundle_t {image_t *image[32];};
#include "sprite_hash.h"
int main(){
    // Animated bundles sharing even frames must still select distinct shaders.
    image_t first={"textures/grass/even"},a={"textures/grass/odd_a"},b={"textures/grass/odd_b"};
    textureBundle_t left={},right={};left.image[0]=right.image[0]=&first;
    left.image[1]=&a;right.image[1]=&b;
    CHECK(SpriteBundleHash(&left)!=SpriteBundleHash(&right));
    CHECK(SpriteBundleHash(&left)==UpdateHash(a.imgName,UpdateHash(first.imgName,0)));
    // A legal full bundle has no null terminator within its image array.
    std::vector<std::string> names(32); std::vector<image_t> images(32);
    auto *full=new textureBundle_t{}; uint32_t expected=0;
    for(size_t i=0;i<32;++i){names[i]="textures/grass/frame"+std::to_string(i);images[i].imgName=names[i].c_str();full->image[i]=&images[i];expected=UpdateHash(images[i].imgName,expected);}
    CHECK(SpriteBundleHash(full)==expected);delete full;
    textureBundle_t empty={};CHECK(SpriteBundleHash(&empty)==0);
    std::puts("Surface sprite bundle identity checks passed.");
}
